#pragma once

#include "engine/render/gl_handle.h"
#include "engine/render/shader.h"

#include <glm/glm.hpp>

#include <array>
#include <functional>
#include <span>
#include <vector>

namespace ghost::engine {
class PostProcess {
public:
    enum class Mode { DepthAware, Classic, Native };
    enum class Filter { Point, Center2, Box };
    enum class FloorMode { Off, Hard, Soft };
    enum class GradePreset { Off, Campfire, Vermis, Ghost, Daylight };

    struct Grade {
        float exposure = 0.0f;
        float contrast = 1.0f;
        float saturation = 1.0f;
        glm::vec3 tintShadow{1.0f};
        glm::vec3 tintLight{1.0f};
        float tintAmount = 0.0f;
        float vignette = 0.0f;
    };

    struct Settings {
        bool enabled = true;
        Mode mode = Mode::DepthAware;
        Filter filter = Filter::Point;
        FloorMode floor = FloorMode::Soft;
        float virtualWidth = 533.0f;

        float k = 2.15f;
        float gamma = 0.6f;
        float nearLayerDepth = 1.6f;
        int bits = 4;
        bool bayer8 = false;

        bool stableRotation = true;

        float hysteresis = 0.15f;
        bool bloom = true;
        float bloomStrength = 0.6f;
        float bloomThreshold = 0.72f;
        GradePreset preset = GradePreset::Campfire;
        Grade grade;
        float flash = 0.0f;
        bool distortion = true;
        float distortionScale = 1.0f;

        glm::vec3 aura{0.0f};
        glm::vec3 rushColor{0.8f, 0.95f, 1.0f};

        glm::vec2 hurt{0.0f};

        glm::vec2 screenChroma{0.0f};
        float hazeChroma = 0.0f;
        float hazeShimmer = 0.0f;
        float swirl = 0.0f;
    };

    enum class DistortionKind { Haze, Ring, Lens, Streak, Chroma };
    struct DistortionSource {
        glm::vec2 center{0.0f};
        float radius = 10.0f;
        float strength = 2.0f;
        DistortionKind kind = DistortionKind::Haze;
        float ringRadius = 0.0f;
        float seed = 0.0f;
        glm::vec2 end{0.0f};
        float endRadius = 0.0f;

        float depth = 0.0f;
        float endDepth = 0.0f;
    };
    static constexpr std::size_t kMaxDistortions = 8;

    PostProcess(float zNear, float zFar);

    void resize(glm::ivec2 size);

    void beginScene() const;

    void finish();

    static void useWorldDepthRange();
    static void useNearDepthRange();
    static void resetDepthRange();

    Settings& settings() { return m_settings; }
    void applyPreset(GradePreset preset);

    unsigned copySceneDepth();
    unsigned copySceneColor();
    void resolveScene(const std::function<void(GLuint color, GLuint depth)>& draw);
    GLuint sceneDepth() const { return m_depth.id(); }
    float zNear() const { return m_zNear; }
    float zFar() const { return m_zFar; }
    static float depthSplit();

    void setDistortion(std::span<const DistortionSource> sources, float time);

    void setCamera(const glm::mat4& view, const glm::mat4& proj);
    void debugUi();

private:
    void createTargets();
    void reloadShaders();

    float m_zNear;
    float m_zFar;
    Settings m_settings;
    glm::ivec2 m_size{0};
    int m_s = 3;
    std::array<glm::ivec2, 5> m_levelSize{};
    std::vector<DistortionSource> m_distortion;
    float m_time = 0.0f;

    glm::vec2 m_gridShift{0.0f};
    glm::ivec2 m_gridOrigin{0};
    glm::vec3 m_lastForward{0.0f};
    bool m_haveForward = false;

    std::array<std::array<GlTexture, 4>, 2> m_history;
    int m_historyFrame = 0;
    bool m_historyValid = false;
    Mode m_lastMode = Mode::DepthAware;

    Shader m_downAvg;
    Shader m_downMin;
    Shader m_bright;
    Shader m_blur;
    Shader m_composite;
    GlVertexArray m_emptyVao;

    GlFramebuffer m_sceneFbo;
    GlTexture m_depth;
    GlTexture m_depthCopy;
    GlFramebuffer m_depthCopyFbo;
    std::array<GlTexture, 5> m_color;
    std::array<GlFramebuffer, 4> m_colorFbo;
    std::array<GlTexture, 4> m_minDepth;
    std::array<GlFramebuffer, 4> m_minDepthFbo;
    GlTexture m_colorCopy;
    GlTexture m_resolved;
    GlFramebuffer m_resolvedFbo;
    bool m_resolvedValid = false;
    std::array<GlTexture, 2> m_bloom;
    std::array<GlFramebuffer, 2> m_bloomFbo;
    glm::ivec2 m_bloomSize{1};
};

}
