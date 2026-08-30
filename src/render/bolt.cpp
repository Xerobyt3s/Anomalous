#include "render/bolt.h"
#include "core/arena.h"
#include "platform/gl_loader.h"
#include "render/device.h"

namespace anom {

bool BoltRenderer::init(Arena& arena)
{
    verts_ = arena.push_array<Vertex>(kBoltMaxVerts);
    if (!verts_) {
        return false;
    }

    glCreateBuffers(1, &vbo_);
    glNamedBufferStorage(vbo_, kBoltMaxVerts * sizeof(Vertex), nullptr, GL_DYNAMIC_STORAGE_BIT);
    glCreateVertexArrays(1, &vao_);
    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(Vertex));
    glEnableVertexArrayAttrib(vao_, 0);
    glVertexArrayAttribFormat(vao_, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, pos));
    glVertexArrayAttribBinding(vao_, 0, 0);
    glEnableVertexArrayAttrib(vao_, 1);
    glVertexArrayAttribFormat(vao_, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, dir));
    glVertexArrayAttribBinding(vao_, 1, 0);
    glEnableVertexArrayAttrib(vao_, 2);
    glVertexArrayAttribFormat(vao_, 2, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, params));
    glVertexArrayAttribBinding(vao_, 2, 0);
    return true;
}

void BoltRenderer::shutdown()
{
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        glDeleteBuffers(1, &vbo_);
        vao_ = 0;
        vbo_ = 0;
    }
}

u32 BoltRenderer::subdivide(Vec3* points, u32 count, u32 capacity, u32 levels, f32 displacement,
                            u32& rng)
{
    auto rnd = [&rng]() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return static_cast<f32>(rng & 0xFFFFFFu) / 16777216.0f - 0.5f;
    };

    for (u32 level = 0; level < levels; level++) {
        if (count < 2 || count * 2 > capacity) {
            break;
        }
        for (u32 i = count; i-- > 1;) {
            points[i * 2] = points[i];
        }
        for (u32 i = 0; i + 1 < count; i++) {
            const Vec3 a = points[i * 2];
            const Vec3 b = points[i * 2 + 2];
            const Vec3 along = b - a;
            const f32 len = length(along);
            Vec3 offset = normalize(Vec3{rnd(), rnd(), rnd()});
            if (len > 1e-3f) {
                const Vec3 unit = along * (1.0f / len);
                offset = offset - unit * dot(offset, unit);
            }
            points[i * 2 + 1] = (a + b) * 0.5f + offset * (len * displacement);
        }
        count = count * 2 - 1;
    }
    return count;
}

void BoltRenderer::channel(std::span<const Vec3> points, f32 half_width, f32 intensity)
{
    if (!verts_ || points.size() < 2) {
        return;
    }
    const f32 span = static_cast<f32>(points.size() - 1);

    for (u32 i = 0; i + 1 < points.size(); i++) {
        if (count_ + 6 > kBoltMaxVerts) {
            return;
        }
        const Vec3 a = points[i];
        const Vec3 b = points[i + 1];
        const Vec3 delta = b - a;
        if (dot(delta, delta) < 1e-8f) {
            continue;
        }
        const Vec3 dir = normalize(delta);

        const f32 t0 = static_cast<f32>(i) / span;
        const f32 t1 = static_cast<f32>(i + 1) / span;
        const f32 w0 = half_width * (1.0f - 0.55f * t0);
        const f32 w1 = half_width * (1.0f - 0.55f * t1);

        const Vertex corners[4] = {
            {a, dir, Vec4{-1.0f, w0, intensity, t0}},
            {a, dir, Vec4{1.0f, w0, intensity, t0}},
            {b, dir, Vec4{-1.0f, w1, intensity, t1}},
            {b, dir, Vec4{1.0f, w1, intensity, t1}},
        };
        verts_[count_++] = corners[0];
        verts_[count_++] = corners[1];
        verts_[count_++] = corners[2];
        verts_[count_++] = corners[2];
        verts_[count_++] = corners[1];
        verts_[count_++] = corners[3];
    }
}

void BoltRenderer::draw(RenderDevice& device, Vec3 colour, f32 core_radiance)
{
    if (count_ == 0 || device.shadow_pass_active()) {
        return;
    }
    const u32 program = device.shaders().program("bolt");
    if (!program) {
        return;
    }
    device.flush_meshes();

    glNamedBufferSubData(vbo_, 0, static_cast<GLsizeiptr>(count_ * sizeof(Vertex)), verts_);
    glProgramUniform3f(program, 0, colour.x, colour.y, colour.z);
    glProgramUniform1f(program, 1, core_radiance);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    device.set_cull(false);
    device.use_program(program);
    device.bind_vao(vao_);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(count_));
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    device.set_cull(true);
}

} // namespace anom
