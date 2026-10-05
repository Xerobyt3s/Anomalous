#include "render/tree.h"
#include "core/arena.h"
#include "core/log.h"
#include "platform/gl_loader.h"
#include "render/device.h"

namespace anom {
bool TreeRenderer::init(Arena& scratch)
{
    glCreateBuffers(1, &instance_vbo_);
    glNamedBufferStorage(instance_vbo_, kTreeMaxInstances * sizeof(Mat4), nullptr,
                         GL_DYNAMIC_STORAGE_BIT);

    u32 triangles = 0;
    for (u32 v = 0; v < kTreeVariants; v++) {
        ArenaScope scope(scratch);
        TreeData data;
        if (!tree_generate(v, scratch, data)) {
            log_error("tree: generation failed for variant %u", v);
            return false;
        }

        Variant& var = variants_[v];
        glCreateBuffers(1, &var.vbo);
        glNamedBufferStorage(var.vbo, static_cast<GLsizeiptr>(data.vertices.size_bytes()),
                             data.vertices.data(), 0);
        glCreateBuffers(1, &var.ebo);
        glNamedBufferStorage(var.ebo, static_cast<GLsizeiptr>(data.indices.size_bytes()),
                             data.indices.data(), 0);

        glCreateVertexArrays(1, &var.vao);
        glVertexArrayVertexBuffer(var.vao, 0, var.vbo, 0, sizeof(TreeVertex));
        glVertexArrayElementBuffer(var.vao, var.ebo);
        glEnableVertexArrayAttrib(var.vao, 0);
        glVertexArrayAttribFormat(var.vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(TreeVertex, pos));
        glVertexArrayAttribBinding(var.vao, 0, 0);
        glEnableVertexArrayAttrib(var.vao, 1);
        glVertexArrayAttribFormat(var.vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(TreeVertex, normal));
        glVertexArrayAttribBinding(var.vao, 1, 0);
        glEnableVertexArrayAttrib(var.vao, 2);
        glVertexArrayAttribFormat(var.vao, 2, 2, GL_FLOAT, GL_FALSE, offsetof(TreeVertex, uv));
        glVertexArrayAttribBinding(var.vao, 2, 0);
        glEnableVertexArrayAttrib(var.vao, 7);
        glVertexArrayAttribFormat(var.vao, 7, 1, GL_FLOAT, GL_FALSE, offsetof(TreeVertex, leaf));
        glVertexArrayAttribBinding(var.vao, 7, 0);

        glVertexArrayVertexBuffer(var.vao, 1, instance_vbo_, 0, sizeof(Mat4));
        glVertexArrayBindingDivisor(var.vao, 1, 1);
        for (u32 col = 0; col < 4; col++) {
            const u32 attrib = 3 + col;
            glEnableVertexArrayAttrib(var.vao, attrib);
            glVertexArrayAttribFormat(var.vao, attrib, 4, GL_FLOAT, GL_FALSE,
                                      col * 4 * sizeof(f32));
            glVertexArrayAttribBinding(var.vao, attrib, 1);
        }

        var.index_count = static_cast<u32>(data.indices.size());
        var.bark_index_count = data.bark_index_count;
        var.bounds = data.bounds;
        triangles += var.index_count / 3;
    }

    ready_ = true;
    log_info("tree: %u variants, %u tris total", kTreeVariants, triangles);
    return true;
}

void TreeRenderer::shutdown()
{
    for (Variant& var : variants_) {
        if (var.vao) {
            glDeleteVertexArrays(1, &var.vao);
            glDeleteBuffers(1, &var.vbo);
            glDeleteBuffers(1, &var.ebo);
        }
        var = Variant{};
    }
    if (instance_vbo_) {
        glDeleteBuffers(1, &instance_vbo_);
        instance_vbo_ = 0;
    }
    ready_ = false;
}

void TreeRenderer::begin_frame()
{
    for (Variant& var : variants_) {
        var.count = 0;
    }
}

void TreeRenderer::submit(u32 variant, const Mat4& model)
{
    Variant& var = variants_[variant % kTreeVariants];
    if (var.count < kTreeMaxInstances) {
        var.models[var.count++] = model;
    }
}

void TreeRenderer::draw(RenderDevice& device)
{
    if (!ready_) {
        return;
    }
    const bool shadow_pass = device.shadow_pass_active();
    const u32 program = device.shaders().program(shadow_pass ? "tree_shadow" : "tree");
    if (!program) {
        return;
    }

    device.flush_meshes();
    if (shadow_pass) {
        const Mat4 light_vp = device.shadow_matrix();
        glProgramUniformMatrix4fv(program, 0, 1, GL_FALSE, light_vp.m);
    }
    device.use_program(program);
    device.set_cull(false);

    for (const Variant& var : variants_) {
        if (var.count == 0 || !var.vao) {
            continue;
        }
        u32 visible = 0;
        for (u32 i = 0; i < var.count; i++) {
            if (frustum_test_aabb(device.frustum(), transform(var.models[i], var.bounds))) {
                visible_[visible++] = var.models[i];
            }
        }
        if (visible == 0) {
            continue;
        }
        glNamedBufferSubData(instance_vbo_, 0, static_cast<GLsizeiptr>(visible * sizeof(Mat4)),
                             visible_);
        device.bind_vao(var.vao);
        const u32 count = shadow_pass ? var.bark_index_count : var.index_count;
        glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT,
                                nullptr, static_cast<GLsizei>(visible));
    }
    device.set_cull(true);
}

}
