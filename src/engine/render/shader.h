#pragma once

#include "engine/render/gl_handle.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ghost::engine {
class Shader {
public:

    Shader(std::filesystem::path vertexPath, std::filesystem::path fragmentPath);

    void reloadIfChanged();
    void use() const;
    GLuint id() const { return m_program.id(); }

    static std::string expandIncludes(const std::filesystem::path& path, std::vector<std::filesystem::path>& files);

    void set(const char* name, int value) const;
    void set(const char* name, const glm::ivec2& value) const;
    void set(const char* name, const glm::vec2& value) const;
    void set(const char* name, float value) const;
    void set(const char* name, const glm::vec4& value) const;
    void set(const char* name, const glm::ivec4& value) const;
    void set(const char* name, const glm::vec3& value) const;
    void set(const char* name, const glm::mat3& value) const;
    void set(const char* name, const glm::mat4& value) const;
    void set(const char* name, std::span<const glm::vec3> values) const;

private:
    std::optional<GlProgram> build();
    GLint location(const char* name) const;

    std::filesystem::path m_vertexPath;
    std::filesystem::path m_fragmentPath;
    struct WatchedFile {
        std::filesystem::path path;
        std::filesystem::file_time_type time;
    };
    std::vector<WatchedFile> m_watched;
    GlProgram m_program;
};

}
