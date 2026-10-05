#include "engine/render/post_process.h"

#include "engine/assets/asset_path.h"
#include "engine/debug/log.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace ghost::engine {
namespace {
constexpr float kDepthSplit = 0.02f;

GlTexture makeTexture(glm::ivec2 size, GLenum format, bool linear = false) {
    GLuint id = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &id);
    glTextureStorage2D(id, 1, format, std::max(size.x, 1), std::max(size.y, 1));
    const GLint filter = linear ? GL_LINEAR : GL_NEAREST;
    glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, filter);
    glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, filter);
    glTextureParameteri(id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return GlTexture(id);
}

GlFramebuffer makeFramebuffer(const GlTexture& color, const GlTexture* depth = nullptr) {
    GLuint id = 0;
    glCreateFramebuffers(1, &id);
    glNamedFramebufferTexture(id, GL_COLOR_ATTACHMENT0, color.id(), 0);
    if (depth) {
        glNamedFramebufferTexture(id, GL_DEPTH_ATTACHMENT, depth->id(), 0);
    }
    if (glCheckNamedFramebufferStatus(id, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        GHOST_ERROR("PostProcess: framebuffer incomplete");
    }
    return GlFramebuffer(id);
}

Shader postShader(const char* fragment) {
    return Shader(assetPath("shaders/post/fullscreen.vert"), assetPath(std::string("shaders/post/") + fragment));
}

}

PostProcess::PostProcess(float zNear, float zFar)
    : m_zNear(zNear), m_zFar(zFar), m_downAvg(postShader("down_avg.frag")), m_downMin(postShader("down_min.frag")),
      m_bright(postShader("bright.frag")), m_blur(postShader("blur.frag")),
      m_composite(postShader("composite.frag")) {
    GLuint vao = 0;
    glCreateVertexArrays(1, &vao);
    m_emptyVao = GlVertexArray(vao);
    applyPreset(m_settings.preset);
}

void PostProcess::applyPreset(GradePreset preset) {
    m_settings.preset = preset;
    Grade& g = m_settings.grade;
    switch (preset) {
    case GradePreset::Off:
        g = Grade{};
        m_settings.bits = 5;
        break;
    case GradePreset::Campfire:
        g = Grade{0.40f, 1.18f, 0.85f, {0.77f, 1.08f, 0.92f}, {1.32f, 0.95f, 0.55f}, 0.35f, 0.38f};
        m_settings.bits = 4;
        break;
    case GradePreset::Vermis:
        g = Grade{0.05f, 1.35f, 0.70f, {0.36f, 1.24f, 0.51f}, {1.30f, 0.99f, 0.25f}, 0.85f, 0.35f};
        m_settings.bits = 3;
        break;
    case GradePreset::Ghost:
        g = Grade{0.20f, 1.15f, 0.70f, {0.81f, 1.04f, 1.17f}, {1.10f, 0.99f, 0.85f}, 0.30f, 0.40f};
        m_settings.bits = 4;
        break;
    case GradePreset::Daylight:
        g = Grade{-0.4f, 1.15f, 1.65f, {0.88f, 1.0f, 1.12f}, {1.08f, 1.0f, 0.9f}, 0.05f, 0.32f};
        m_settings.bits = 5;
        break;
    }
}

void PostProcess::setCamera(const glm::mat4& view, const glm::mat4& proj) {
    const glm::mat3 rotation{view};
    const glm::vec3 forward = -glm::vec3(view[0][2], view[1][2], view[2][2]);
    if (m_haveForward && m_settings.stableRotation) {
        const glm::vec3 seen = rotation * m_lastForward;
        const glm::vec4 clip = proj * glm::vec4(seen, 0.0f);
        if (seen.z < -0.2f && clip.w > 1e-4f) {
            const glm::vec2 moved = glm::vec2(clip) / clip.w * 0.5f * glm::vec2(m_size);
            if (glm::length(moved) < 0.5f * static_cast<float>(m_size.x)) {
                m_gridShift -= moved;
            } else {
                m_historyValid = false;
            }
        } else {
            m_historyValid = false;
        }
    }
    if (!m_settings.stableRotation) {
        m_gridShift = glm::vec2(0.0f);
    }
    m_lastForward = forward;
    m_haveForward = true;
    m_gridOrigin = glm::ivec2(glm::round(m_gridShift));
}

void PostProcess::setDistortion(std::span<const DistortionSource> sources, float time) {
    m_distortion.assign(sources.begin(), sources.end());
    m_time = time;
}

void PostProcess::resize(glm::ivec2 size) {
    size = glm::max(size, glm::ivec2(1));
    const int s = std::max(1, static_cast<int>(std::lround(size.x / m_settings.virtualWidth)));
    if (size == m_size && s == m_s && m_sceneFbo) {
        return;
    }
    m_size = size;
    m_s = s;
    createTargets();
}

void PostProcess::createTargets() {
    m_levelSize[0] = m_size;

    m_levelSize[1] = (m_size + glm::ivec2(m_s - 1)) / m_s + glm::ivec2(8);
    for (std::size_t i = 2; i < m_levelSize.size(); ++i) {
        m_levelSize[i] = (m_levelSize[i - 1] + glm::ivec2(1)) / 2;
    }

    m_color[0] = makeTexture(m_size, GL_RGBA16F);
    m_depth = makeTexture(m_size, GL_DEPTH_COMPONENT24);
    m_sceneFbo = makeFramebuffer(m_color[0], &m_depth);

    m_colorCopy = makeTexture(m_size, GL_RGBA16F, true);
    m_resolved = makeTexture(m_size, GL_RGBA16F);
    m_resolvedFbo = makeFramebuffer(m_resolved);
    m_resolvedValid = false;

    m_depthCopy = makeTexture(m_size, GL_DEPTH_COMPONENT24);
    GLuint copyFbo = 0;
    glCreateFramebuffers(1, &copyFbo);
    glNamedFramebufferTexture(copyFbo, GL_DEPTH_ATTACHMENT, m_depthCopy.id(), 0);
    glNamedFramebufferDrawBuffer(copyFbo, GL_NONE);
    glNamedFramebufferReadBuffer(copyFbo, GL_NONE);
    m_depthCopyFbo = GlFramebuffer(copyFbo);

    for (std::size_t i = 1; i < m_color.size(); ++i) {
        m_color[i] = makeTexture(m_levelSize[i], GL_RGBA16F);
        m_colorFbo[i - 1] = makeFramebuffer(m_color[i]);
        m_minDepth[i - 1] = makeTexture(m_levelSize[i], GL_R32F);
        m_minDepthFbo[i - 1] = makeFramebuffer(m_minDepth[i - 1]);
    }

    for (auto& set : m_history) {
        for (std::size_t k = 0; k < set.size(); ++k) {
            set[k] = makeTexture(m_levelSize[k + 1], GL_R8UI);
        }
    }
    m_historyValid = false;

    m_bloomSize = glm::max(m_size / 4, glm::ivec2(1));

    m_bloom[0] = makeTexture(m_bloomSize, GL_RGBA16F, true);
    m_bloom[1] = makeTexture(m_bloomSize, GL_RGBA16F);
    m_bloomFbo[0] = makeFramebuffer(m_bloom[0]);
    m_bloomFbo[1] = makeFramebuffer(m_bloom[1]);
}

void PostProcess::beginScene() const {
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFbo.id());
    glViewport(0, 0, m_size.x, m_size.y);
}

float PostProcess::depthSplit() { return kDepthSplit; }

unsigned PostProcess::copySceneDepth() {
    glBlitNamedFramebuffer(m_sceneFbo.id(), m_depthCopyFbo.id(), 0, 0, m_size.x, m_size.y, 0, 0, m_size.x, m_size.y,
                           GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    return m_depthCopy.id();
}

unsigned PostProcess::copySceneColor() {
    glCopyImageSubData(m_color[0].id(), GL_TEXTURE_2D, 0, 0, 0, 0, m_colorCopy.id(), GL_TEXTURE_2D, 0, 0, 0, 0, m_size.x,
                       m_size.y, 1);
    return m_colorCopy.id();
}

void PostProcess::resolveScene(const std::function<void(GLuint color, GLuint depth)>& draw) {
    resetDepthRange();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, m_resolvedFbo.id());
    glViewport(0, 0, m_size.x, m_size.y);
    glBindVertexArray(m_emptyVao.id());
    draw(m_color[0].id(), m_depth.id());
    m_resolvedValid = true;
}

void PostProcess::useWorldDepthRange() { glDepthRange(kDepthSplit, 1.0); }
void PostProcess::useNearDepthRange() { glDepthRange(0.0, kDepthSplit); }
void PostProcess::resetDepthRange() { glDepthRange(0.0, 1.0); }

void PostProcess::reloadShaders() {
    m_downAvg.reloadIfChanged();
    m_downMin.reloadIfChanged();
    m_bright.reloadIfChanged();
    m_blur.reloadIfChanged();
    m_composite.reloadIfChanged();
}

void PostProcess::finish() {
    reloadShaders();
    const Settings& st = m_settings;

    resetDepthRange();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(m_emptyVao.id());
    const GLuint source = m_resolvedValid ? m_resolved.id() : m_color[0].id();

    const int period = 8 * m_s;
    const glm::ivec2 wrapped{((m_gridOrigin.x % period) + period) % period, ((m_gridOrigin.y % period) + period) % period};

    m_downAvg.use();
    m_downAvg.set("uSrc", 0);
    const std::array<int, 4> kernels{m_s, 2, 2, 2};
    for (std::size_t i = 0; i < kernels.size(); ++i) {
        int filter = static_cast<int>(st.filter);
        if (st.filter == Filter::Center2 && i > 0) {
            filter = static_cast<int>(Filter::Point);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, m_colorFbo[i].id());
        glViewport(0, 0, m_levelSize[i + 1].x, m_levelSize[i + 1].y);
        glBindTextureUnit(0, i == 0 ? source : m_color[i].id());
        m_downAvg.set("uK", kernels[i]);
        m_downAvg.set("uFilter", filter);
        m_downAvg.set("uOrigin", i == 0 ? wrapped : glm::ivec2(0));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    m_downMin.use();
    m_downMin.set("uSrc", 0);
    m_downMin.set("uNF", glm::vec2(m_zNear, m_zFar));
    m_downMin.set("uSplit", kDepthSplit);
    m_downMin.set("uNearLayerDepth", st.nearLayerDepth);
    for (std::size_t i = 0; i < kernels.size(); ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_minDepthFbo[i].id());
        glViewport(0, 0, m_levelSize[i + 1].x, m_levelSize[i + 1].y);
        glBindTextureUnit(0, i == 0 ? m_depth.id() : m_minDepth[i - 1].id());
        m_downMin.set("uK", kernels[i]);
        m_downMin.set("uLinearize", i == 0 ? 1 : 0);
        m_downMin.set("uOrigin", i == 0 ? wrapped : glm::ivec2(0));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    if (st.bloom) {
        glViewport(0, 0, m_bloomSize.x, m_bloomSize.y);
        m_bright.use();
        m_bright.set("uSrc", 0);
        m_bright.set("uThresh", st.bloomThreshold);
        glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFbo[0].id());
        glBindTextureUnit(0, source);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        m_blur.use();
        m_blur.set("uSrc", 0);
        glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFbo[1].id());
        glBindTextureUnit(0, m_bloom[0].id());
        m_blur.set("uDir", glm::ivec2(1, 0));
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFbo[0].id());
        glBindTextureUnit(0, m_bloom[1].id());
        m_blur.set("uDir", glm::ivec2(0, 1));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_size.x, m_size.y);
    m_composite.use();
    const std::array<GLuint, 11> textures{source,             m_color[1].id(),    m_color[2].id(),
                                          m_color[3].id(),    m_color[4].id(),    m_minDepth[0].id(),
                                          m_minDepth[1].id(), m_minDepth[2].id(), m_minDepth[3].id(),
                                          m_bloom[0].id(),    m_depth.id()};
    const std::array<const char*, 11> names{"uC0", "uC1", "uC2", "uC3", "uC4",
                                            "uD1", "uD2", "uD3", "uD4", "uBloom", "uDepth"};
    for (std::size_t i = 0; i < textures.size(); ++i) {
        glBindTextureUnit(static_cast<GLuint>(i), textures[i]);
        m_composite.set(names[i], static_cast<int>(i));
    }
    const Mode mode = st.enabled ? st.mode : Mode::Native;
    m_composite.set("uBloomStr", st.bloom ? st.bloomStrength : 0.0f);
    m_composite.set("uS", m_s);
    m_composite.set("uMode", static_cast<int>(mode));
    m_composite.set("uFloorV", st.floor == FloorMode::Hard ? 1 : (st.floor == FloorMode::Soft ? 2 : 0));
    m_composite.set("uKn", st.k * static_cast<float>(m_s));
    m_composite.set("uGamma", st.gamma);
    const Grade& g = st.grade;
    m_composite.set("uExposure", g.exposure);
    m_composite.set("uContrast", g.contrast);
    m_composite.set("uSaturation", g.saturation);
    m_composite.set("uTintS", g.tintShadow);
    m_composite.set("uTintL", g.tintLight);
    m_composite.set("uTintAmt", g.tintAmount);
    m_composite.set("uVignette", g.vignette);
    m_composite.set("uBits", st.bits);
    m_composite.set("uBayer8", st.bayer8 ? 1 : 0);
    m_composite.set("uFlash", st.flash);
    m_composite.set("uAura", st.aura);
    m_composite.set("uRushColor", st.rushColor);
    m_composite.set("uHurt", st.hurt);
    m_composite.set("uScreenChroma", st.screenChroma);
    m_composite.set("uHazeChroma", st.hazeChroma);
    m_composite.set("uHazeShimmer", st.hazeShimmer);
    m_composite.set("uSwirl", st.swirl);
    m_composite.set("uTime", m_time);
    m_composite.set("uNF", glm::vec2(m_zNear, m_zFar));
    m_composite.set("uSplit", kDepthSplit);
    m_composite.set("uOrigin", wrapped);
    m_composite.set("uOriginFull", m_gridOrigin);
    m_composite.set("uShift1", (m_gridOrigin - wrapped) / m_s);
    const std::size_t distortions = st.distortion ? std::min(m_distortion.size(), kMaxDistortions) : 0;
    m_composite.set("uDistCount", static_cast<int>(distortions));
    for (std::size_t i = 0; i < distortions; ++i) {
        const DistortionSource& d = m_distortion[i];
        const std::string index = std::to_string(i);
        m_composite.set(("uDistA[" + index + "]").c_str(),
                        glm::vec3(d.center.x, d.center.y, d.radius));
        m_composite.set(("uDistC[" + index + "]").c_str(), glm::vec3(d.end.x, d.end.y, d.endRadius));
        m_composite.set(("uDistD[" + index + "]").c_str(), glm::vec2(d.depth, d.endDepth));
        m_composite.set(("uDistB[" + index + "]").c_str(),
                        glm::vec3(d.strength * st.distortionScale,
                                  d.kind == DistortionKind::Ring ? d.ringRadius
                                                                 : (d.kind == DistortionKind::Lens     ? -2.0f
                                                                    : d.kind == DistortionKind::Streak ? -3.0f
                                                                    : d.kind == DistortionKind::Chroma ? -4.0f
                                                                                                       : -1.0f),
                                  d.seed));
    }

    const int readSet = m_historyFrame;
    const int writeSet = 1 - m_historyFrame;
    for (int k = 0; k < 4; ++k) {
        glBindImageTexture(static_cast<GLuint>(k), m_history[static_cast<std::size_t>(readSet)][static_cast<std::size_t>(k)].id(), 0, GL_FALSE, 0, GL_READ_ONLY, GL_R8UI);
        glBindImageTexture(static_cast<GLuint>(4 + k), m_history[static_cast<std::size_t>(writeSet)][static_cast<std::size_t>(k)].id(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R8UI);
    }
    if (mode != m_lastMode) {
        m_historyValid = false;
    }
    m_lastMode = mode;
    m_composite.set("uHistoryValid", m_historyValid ? 1 : 0);
    m_composite.set("uHysteresis", st.hysteresis);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    m_historyFrame = writeSet;
    m_historyValid = true;
    m_resolvedValid = false;

    glBindVertexArray(0);
    glBindTextureUnit(0, 0);
    glBindTextureUnit(10, 0);
}

void PostProcess::debugUi() {
    Settings& st = m_settings;
    ImGui::SetNextWindowPos({ImGui::GetIO().DisplaySize.x - 350.0f, 330.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({340.0f, 0.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Post process");
    ImGui::Checkbox("Enabled (F2)", &st.enabled);
    ImGui::SameLine();
    ImGui::TextDisabled("1 virtual px = %d px", m_s);

    int mode = static_cast<int>(st.mode);
    if (ImGui::Combo("Mode", &mode, "Depth-aware\0Classic uniform\0Native (reference)\0")) {
        st.mode = static_cast<Mode>(mode);
    }
    int filter = static_cast<int>(st.filter);
    if (ImGui::Combo("Filter", &filter, "Point (crunchy)\0Center 2x2\0Box (smooth)\0")) {
        st.filter = static_cast<Filter>(filter);
    }
    int floorMode = static_cast<int>(st.floor);
    if (ImGui::Combo("Far floor", &floorMode, "Off (native allowed)\0Hard (1 virtual px)\0Soft (dithered)\0")) {
        st.floor = static_cast<FloorMode>(floorMode);
    }
    ImGui::SliderFloat("Virtual width", &st.virtualWidth, 160.0f, 1280.0f, "%.0f px");
    ImGui::SliderFloat("Block k", &st.k, 0.05f, 20.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Block gamma", &st.gamma, -2.0f, 3.0f, "%.2f");
    ImGui::SliderFloat("Gun depth", &st.nearLayerDepth, 0.1f, 20.0f, "%.2f m", ImGuiSliderFlags_Logarithmic);
    ImGui::TextDisabled("Block at 1 m: %.1f  3 m: %.1f  10 m: %.1f virtual px", st.k,
                        st.k * std::pow(3.0f, -st.gamma), st.k * std::pow(10.0f, -st.gamma));

    ImGui::SeparatorText("Color");
    int preset = static_cast<int>(st.preset);
    if (ImGui::Combo("Grade preset", &preset, "Off\0Campfire\0Vermis\0Ghost\0Daylight\0")) {
        applyPreset(static_cast<GradePreset>(preset));
    }
    ImGui::SliderInt("Bits", &st.bits, 2, 8);
    ImGui::Checkbox("8x8 dither", &st.bayer8);
    Grade& g = st.grade;
    ImGui::SliderFloat("Exposure", &g.exposure, -2.0f, 2.0f, "%.2f stops");
    ImGui::SliderFloat("Contrast", &g.contrast, 0.5f, 2.0f);
    ImGui::SliderFloat("Saturation", &g.saturation, 0.0f, 1.5f);
    ImGui::ColorEdit3("Shadow ink", &g.tintShadow.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::ColorEdit3("Light ink", &g.tintLight.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::SliderFloat("Duotone", &g.tintAmount, 0.0f, 1.0f);
    ImGui::SliderFloat("Vignette", &g.vignette, 0.0f, 1.0f);

    ImGui::SeparatorText("Heat haze");
    ImGui::Checkbox("Stable rotation (grid follows the world)", &st.stableRotation);
    ImGui::SliderFloat("Block hysteresis", &st.hysteresis, 0.0f, 0.5f);
    ImGui::Checkbox("Distortion", &st.distortion);
    ImGui::SliderFloat("Distortion strength", &st.distortionScale, 0.0f, 4.0f);
    ImGui::TextDisabled("%zu sources this frame", m_distortion.size());

    ImGui::SeparatorText("Bloom");
    ImGui::Checkbox("Bloom", &st.bloom);
    ImGui::SliderFloat("Strength", &st.bloomStrength, 0.0f, 2.0f);
    ImGui::SliderFloat("Threshold", &st.bloomThreshold, 0.0f, 1.5f);
    ImGui::End();
}

}
