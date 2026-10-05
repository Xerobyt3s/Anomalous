#include "terminal/term_render.h"
#include "core/log.h"
#include "platform/gl_loader.h"
#include "render/device.h"
#include "terminal/disks.h"
#include "terminal/screen.h"

namespace anom {
namespace {

constexpr f32 kPersistDecay = 0.80f;

constexpr i32 kWireVpX = 392;
constexpr i32 kWireVpY = 116;
constexpr i32 kWireVpW = 208;
constexpr i32 kWireVpH = 224;

constexpr f32 kBackground[4] = {0.027f, 0.086f, 0.035f, 1.0f};
constexpr f32 kBlack[4] = {0.0f, 0.0f, 0.0f, 1.0f};

} // namespace

bool TermRenderer::init(FontChain& fonts, Arena& scratch)
{
    if (!font_.init(fonts, scratch)) {
        return false;
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &color_tex_);
    glTextureStorage2D(color_tex_, 1, GL_RGBA8, kTermTexW, kTermTexH);
    glTextureParameteri(color_tex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(color_tex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(color_tex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(color_tex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glCreateTextures(GL_TEXTURE_2D, 1, &depth_tex_);
    glTextureStorage2D(depth_tex_, 1, GL_DEPTH_COMPONENT24, kTermTexW, kTermTexH);

    glCreateFramebuffers(1, &fbo_);
    glNamedFramebufferTexture(fbo_, GL_COLOR_ATTACHMENT0, color_tex_, 0);
    glNamedFramebufferTexture(fbo_, GL_DEPTH_ATTACHMENT, depth_tex_, 0);
    if (glCheckNamedFramebufferStatus(fbo_, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        log_error("terminal: framebuffer incomplete");
        return false;
    }

    for (u32 i = 0; i < 2; i++) {
        glCreateTextures(GL_TEXTURE_2D, 1, &persist_tex_[i]);
        glTextureStorage2D(persist_tex_[i], 5, GL_RGBA8, kTermTexW, kTermTexH);
        glTextureParameteri(persist_tex_[i], GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTextureParameteri(persist_tex_[i], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(persist_tex_[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(persist_tex_[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glCreateFramebuffers(1, &persist_fbo_[i]);
        glNamedFramebufferTexture(persist_fbo_[i], GL_COLOR_ATTACHMENT0, persist_tex_[i], 0);
    }

    glCreateBuffers(1, &text_vbo_);
    glNamedBufferStorage(text_vbo_, sizeof(glyphs_), nullptr, GL_DYNAMIC_STORAGE_BIT);
    glCreateVertexArrays(1, &text_vao_);
    glVertexArrayVertexBuffer(text_vao_, 0, text_vbo_, 0, sizeof(GlyphInst));
    glVertexArrayBindingDivisor(text_vao_, 0, 1);
    static const struct { u32 index; i32 size; u32 offset; } kGlyphAttribs[4] = {
        {0, 2, 0}, {1, 1, 8}, {2, 1, 12}, {3, 1, 16},
    };
    for (const auto& attrib : kGlyphAttribs) {
        glEnableVertexArrayAttrib(text_vao_, attrib.index);
        glVertexArrayAttribFormat(text_vao_, attrib.index, attrib.size, GL_FLOAT, GL_FALSE,
                                  attrib.offset);
        glVertexArrayAttribBinding(text_vao_, attrib.index, 0);
    }

    glCreateBuffers(1, &point_vbo_);
    glNamedBufferStorage(point_vbo_, kTermMaxPoints * sizeof(TermPoint), nullptr,
                         GL_DYNAMIC_STORAGE_BIT);
    glCreateVertexArrays(1, &point_vao_);
    glVertexArrayVertexBuffer(point_vao_, 0, point_vbo_, 0, sizeof(TermPoint));
    glEnableVertexArrayAttrib(point_vao_, 0);
    glVertexArrayAttribFormat(point_vao_, 0, 4, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(point_vao_, 0, 0);

    glCreateBuffers(1, &line_vbo_);
    glNamedBufferStorage(line_vbo_, kTermMaxLineVerts * sizeof(TermPoint), nullptr,
                         GL_DYNAMIC_STORAGE_BIT);
    glCreateVertexArrays(1, &line_vao_);
    glVertexArrayVertexBuffer(line_vao_, 0, line_vbo_, 0, sizeof(TermPoint));
    glEnableVertexArrayAttrib(line_vao_, 0);
    glVertexArrayAttribFormat(line_vao_, 0, 4, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(line_vao_, 0, 0);

    glCreateVertexArrays(1, &empty_vao_);

    glCreateTextures(GL_TEXTURE_2D, 1, &pic_tex_);
    glTextureStorage2D(pic_tex_, 1, GL_RGB8, kPhotoWidth, kPhotoHeight);
    glTextureParameteri(pic_tex_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(pic_tex_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(pic_tex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(pic_tex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    clear_pending_ = true;
    ready_ = true;
    return true;
}

void TermRenderer::shutdown()
{
    font_.shutdown();
    ready_ = false;
}

u32 TermRenderer::build_glyphs(const Screen& screen)
{
    u32 n = 0;
    for (u32 row = 0; row < kTermRows; row++) {
        for (u32 col = 0; col < kTermCols; col++) {
            const u16 glyph = screen.glyph(row, col);
            const u8 color = screen.color(row, col);
            const f32 x = static_cast<f32>(kTermOriginX + col * kTermCellW);
            const f32 y = static_cast<f32>(kTermOriginY + row * kTermCellH + 1);

            if (color == TC_BG) {
                glyphs_[n++] = GlyphInst{x, y, static_cast<f32>(kTermFontBlock),
                                         static_cast<f32>(TC_GREEN), 8.0f};
            }
            if (glyph <= 32 || glyph == kTermFontWideCont) {
                continue;
            }
            bool wide = false;
            const u32 slot = font_.slot(glyph, &wide);
            glyphs_[n++] = GlyphInst{x, y, static_cast<f32>(slot), static_cast<f32>(color),
                                     wide ? 16.0f : 8.0f};
        }
    }
    return n;
}

void TermRenderer::draw_points(RenderDevice& device, const TermScene& scene, f32 time)
{
    const u32 program = device.shaders().program("term_map");
    if (!program || scene.point_count == 0 || !scene.points) {
        return;
    }
    if (scene.points_dirty) {
        glNamedBufferSubData(point_vbo_, 0,
                             static_cast<GLsizeiptr>(scene.point_count * sizeof(TermPoint)),
                             scene.points);
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glUseProgram(program);
    glProgramUniformMatrix4fv(program, 0, 1, GL_FALSE, scene.vp3d.m);
    glProgramUniform3f(program, 4, scene.point_center.x, scene.point_center.y,
                       scene.point_center.z);
    glProgramUniform1f(program, 5, time);
    glProgramUniform1f(program, 6, scene.sweep);
    glProgramUniform1f(program, 7, scene.point_reveal);
    glBindVertexArray(point_vao_);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(scene.point_count));
    glDisable(GL_PROGRAM_POINT_SIZE);
    glDisable(GL_DEPTH_TEST);
}

void TermRenderer::draw_lines(RenderDevice& device, const TermScene& scene)
{
    const u32 program = device.shaders().program("term_grid");
    if (!program || scene.line_vertex_count < 2 || !scene.lines) {
        return;
    }
    const u32 count = scene.line_vertex_count < kTermMaxLineVerts ? scene.line_vertex_count
                                                                 : kTermMaxLineVerts;
    glNamedBufferSubData(line_vbo_, 0, static_cast<GLsizeiptr>(count * sizeof(TermPoint)),
                         scene.lines);

    glDisable(GL_DEPTH_TEST);
    glUseProgram(program);
    glProgramUniformMatrix4fv(program, 0, 1, GL_FALSE, scene.vp3d.m);
    glBindVertexArray(line_vao_);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(count));
}

void TermRenderer::draw_photo(RenderDevice& device, const DiskStore& disks,
                              const TermScene& scene)
{
    const u32 program = device.shaders().program("term_pic");
    const u8* pixels = scene.photo > 0 ? disks.photo(scene.photo) : nullptr;
    if (!program || !pixels) {
        return;
    }
    if (pic_uploaded_ != scene.photo) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTextureSubImage2D(pic_tex_, 0, 0, 0, kPhotoWidth, kPhotoHeight, GL_RGB,
                            GL_UNSIGNED_BYTE, pixels);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        pic_uploaded_ = scene.photo;
    }

    glDisable(GL_DEPTH_TEST);
    glUseProgram(program);
    glProgramUniform4f(program, 0, -0.755f, -0.76f, 0.755f, 0.75f);
    glProgramUniform1f(program, 1, scene.image_reveal);
    glBindTextureUnit(0, pic_tex_);
    glBindVertexArray(empty_vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void TermRenderer::draw_video(RenderDevice& device, const TermScene& scene)
{
    const u32 program = device.shaders().program("term_pic");
    if (!program || !scene.video_texture) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glUseProgram(program);
    glProgramUniform4f(program, 0, -0.755f, 0.75f, 0.755f, -0.76f);
    glProgramUniform1f(program, 1, scene.image_reveal);
    glBindTextureUnit(0, scene.video_texture);
    glBindVertexArray(empty_vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void TermRenderer::draw_wires(RenderDevice& device, const TermScene& scene)
{
    const u32 program = device.shaders().program("term_wire");
    if (!program || scene.wire_count == 0) {
        return;
    }

    glViewport(kWireVpX, kWireVpY, kWireVpW, kWireVpH);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(program);
    for (u32 i = 0; i < scene.wire_count; i++) {
        const TermWire& wire = scene.wires[i];
        const GpuMesh* mesh = device.assets().mesh(wire.mesh.view());
        if (!mesh || !mesh->loaded) {
            continue;
        }
        const Mat4 mvp = scene.vp3d * wire.model;
        glProgramUniformMatrix4fv(program, 0, 1, GL_FALSE, mvp.m);
        glProgramUniform3f(program, 4, wire.color.x, wire.color.y, wire.color.z);
        glBindVertexArray(mesh->vao());
        for (u32 s = 0; s < mesh->submesh_count; s++) {
            const GpuSubmesh& sub = mesh->submeshes[s];
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sub.index_count), GL_UNSIGNED_INT,
                           reinterpret_cast<const void*>(
                               static_cast<u64>(sub.first_index) * sizeof(u32)));
        }
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glViewport(0, 0, kTermTexW, kTermTexH);
}

void TermRenderer::render(RenderDevice& device, const DiskStore& disks, const Screen& screen,
                          const TermScene& scene, f32 time)
{
    if (!ready_) {
        return;
    }
    const u32 text_program = device.shaders().program("term_text");
    const u32 persist_program = device.shaders().program("term_persist");
    if (!text_program || !persist_program) {
        return;
    }

    constexpr f32 depth_one = 1.0f;
    if (clear_pending_) {
        clear_pending_ = false;
        for (u32 i = 0; i < 2; i++) {
            glClearNamedFramebufferfv(persist_fbo_[i], GL_COLOR, 0, kBlack);
            glGenerateTextureMipmap(persist_tex_[i]);
        }
    }
    glClearNamedFramebufferfv(fbo_, GL_COLOR, 0, kBackground);
    glClearNamedFramebufferfv(fbo_, GL_DEPTH, 0, &depth_one);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, kTermTexW, kTermTexH);

    draw_points(device, scene, time);
    draw_lines(device, scene);
    draw_photo(device, disks, scene);
    draw_video(device, scene);
    draw_wires(device, scene);

    const u32 count = build_glyphs(screen);
    if (count > 0) {
        glNamedBufferSubData(text_vbo_, 0, static_cast<GLsizeiptr>(count * sizeof(GlyphInst)),
                             glyphs_);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(text_program);
        glBindTextureUnit(0, font_.texture());
        glBindVertexArray(text_vao_);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(count));
        glDisable(GL_BLEND);
    }

    const u32 next = 1 - persist_idx_;
    glBindFramebuffer(GL_FRAMEBUFFER, persist_fbo_[next]);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(persist_program);
    glProgramUniform1f(persist_program, 0, kPersistDecay);
    glBindTextureUnit(0, color_tex_);
    glBindTextureUnit(1, persist_tex_[persist_idx_]);
    glBindVertexArray(empty_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glGenerateTextureMipmap(persist_tex_[next]);
    persist_idx_ = next;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    glEnable(GL_DEPTH_TEST);
}

} // namespace anom
