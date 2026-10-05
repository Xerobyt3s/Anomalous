#pragma once

#include <glad/glad.h>

#include <utility>

namespace ghost::engine {
template <typename Deleter>
class GlHandle {
public:
    GlHandle() = default;
    explicit GlHandle(GLuint id) : m_id(id) {}
    ~GlHandle() { reset(); }

    GlHandle(const GlHandle&) = delete;
    GlHandle& operator=(const GlHandle&) = delete;
    GlHandle(GlHandle&& other) noexcept : m_id(std::exchange(other.m_id, 0)) {}
    GlHandle& operator=(GlHandle&& other) noexcept {
        if (this != &other) {
            reset();
            m_id = std::exchange(other.m_id, 0);
        }
        return *this;
    }

    GLuint id() const { return m_id; }
    explicit operator bool() const { return m_id != 0; }

    void reset() {
        if (m_id != 0) {
            Deleter{}(m_id);
            m_id = 0;
        }
    }

private:
    GLuint m_id = 0;
};

namespace gl_detail {
struct BufferDeleter {
    void operator()(GLuint id) const { glDeleteBuffers(1, &id); }
};
struct VertexArrayDeleter {
    void operator()(GLuint id) const { glDeleteVertexArrays(1, &id); }
};
struct ShaderDeleter {
    void operator()(GLuint id) const { glDeleteShader(id); }
};
struct TextureDeleter {
    void operator()(GLuint id) const { glDeleteTextures(1, &id); }
};
struct FramebufferDeleter {
    void operator()(GLuint id) const { glDeleteFramebuffers(1, &id); }
};
struct ProgramDeleter {
    void operator()(GLuint id) const { glDeleteProgram(id); }
};
}

using GlBuffer = GlHandle<gl_detail::BufferDeleter>;
using GlVertexArray = GlHandle<gl_detail::VertexArrayDeleter>;
using GlShader = GlHandle<gl_detail::ShaderDeleter>;
using GlProgram = GlHandle<gl_detail::ProgramDeleter>;
using GlTexture = GlHandle<gl_detail::TextureDeleter>;
using GlFramebuffer = GlHandle<gl_detail::FramebufferDeleter>;

}
