#include "engine/render/shader.h"

#include "engine/assets/asset_path.h"
#include "engine/debug/log.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ghost::engine {
namespace fs = std::filesystem;

namespace {
std::string readFile(const fs::path& path) { return readAsset(path).value_or(std::string()); }

fs::file_time_type modifiedTime(const fs::path& path) {
    std::error_code ec;
    const auto time = fs::last_write_time(path, ec);
    return ec ? fs::file_time_type{} : time;
}

std::string expandIncludes(const fs::path& path, std::vector<fs::path>& files, int depth = 0) {
    files.push_back(path);
    const std::string source = readFile(path);
    if (depth > 8) {
        return source;
    }
    std::istringstream in(source);
    std::ostringstream out;
    std::string line;
    while (std::getline(in, line)) {
        const auto start = line.find_first_not_of(" \t");
        if (start != std::string::npos && line.compare(start, 8, "#include") == 0) {
            const auto open = line.find('"');
            const auto close = line.rfind('"');
            if (open != std::string::npos && close > open) {
                out << expandIncludes(path.parent_path() / line.substr(open + 1, close - open - 1), files, depth + 1)
                    << '\n';
                continue;
            }
        }
        out << line << '\n';
    }
    return out.str();
}

std::optional<GlShader> compileStage(GLenum stage, const fs::path& path, std::vector<fs::path>& files) {
    const std::string source = expandIncludes(path, files);
    if (source.find_first_not_of(" \t\r\n") == std::string::npos) {
        GHOST_ERROR("Shader source missing or empty: {}", path.string());
        return std::nullopt;
    }

    GlShader shader(glCreateShader(stage));
    const char* text = source.c_str();
    glShaderSource(shader.id(), 1, &text, nullptr);
    glCompileShader(shader.id());

    GLint ok = 0;
    glGetShaderiv(shader.id(), GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        glGetShaderiv(shader.id(), GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length), '\0');
        glGetShaderInfoLog(shader.id(), length, nullptr, log.data());
        GHOST_ERROR("Shader compile failed: {}\n{}", path.string(), log);
        return std::nullopt;
    }
    return shader;
}

}

Shader::Shader(fs::path vertexPath, fs::path fragmentPath)
    : m_vertexPath(std::move(vertexPath)), m_fragmentPath(std::move(fragmentPath)) {
    auto program = build();
    if (!program) {
        throw std::runtime_error("Failed to build shader: " + m_vertexPath.filename().string() + " + " +
                                 m_fragmentPath.filename().string());
    }
    m_program = std::move(*program);
}

std::optional<GlProgram> Shader::build() {
    std::vector<fs::path> files;
    auto vertex = compileStage(GL_VERTEX_SHADER, m_vertexPath, files);
    auto fragment = compileStage(GL_FRAGMENT_SHADER, m_fragmentPath, files);

    m_watched.clear();
    for (const fs::path& file : files) {
        m_watched.push_back({file, modifiedTime(file)});
    }
    if (!vertex || !fragment) {
        return std::nullopt;
    }

    GlProgram program(glCreateProgram());
    glAttachShader(program.id(), vertex->id());
    glAttachShader(program.id(), fragment->id());
    glLinkProgram(program.id());
    glDetachShader(program.id(), vertex->id());
    glDetachShader(program.id(), fragment->id());

    GLint ok = 0;
    glGetProgramiv(program.id(), GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        glGetProgramiv(program.id(), GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length), '\0');
        glGetProgramInfoLog(program.id(), length, nullptr, log.data());
        GHOST_ERROR("Shader link failed: {}\n{}", m_vertexPath.filename().string(), log);
        return std::nullopt;
    }
    return program;
}

void Shader::reloadIfChanged() {
    const bool changed = std::any_of(m_watched.begin(), m_watched.end(), [](const WatchedFile& f) {
        return modifiedTime(f.path) != f.time;
    });
    if (!changed) {
        return;
    }
    if (auto program = build()) {
        m_program = std::move(*program);
        GHOST_INFO("Reloaded shader {} + {}", m_vertexPath.filename().string(), m_fragmentPath.filename().string());
    }
}

std::string Shader::expandIncludes(const fs::path& path, std::vector<fs::path>& files) {
    return ::ghost::engine::expandIncludes(path, files);
}

void Shader::use() const { glUseProgram(m_program.id()); }

GLint Shader::location(const char* name) const { return glGetUniformLocation(m_program.id(), name); }

void Shader::set(const char* name, int value) const { glProgramUniform1i(m_program.id(), location(name), value); }

void Shader::set(const char* name, const glm::ivec2& value) const {
    glProgramUniform2i(m_program.id(), location(name), value.x, value.y);
}

void Shader::set(const char* name, const glm::vec2& value) const {
    glProgramUniform2f(m_program.id(), location(name), value.x, value.y);
}

void Shader::set(const char* name, float value) const {
    glProgramUniform1f(m_program.id(), location(name), value);
}

void Shader::set(const char* name, const glm::vec4& value) const {
    glProgramUniform4fv(m_program.id(), location(name), 1, glm::value_ptr(value));
}

void Shader::set(const char* name, const glm::ivec4& value) const {
    glProgramUniform4i(m_program.id(), location(name), value.x, value.y, value.z, value.w);
}

void Shader::set(const char* name, const glm::vec3& value) const {
    glProgramUniform3fv(m_program.id(), location(name), 1, glm::value_ptr(value));
}

void Shader::set(const char* name, std::span<const glm::vec3> values) const {
    glProgramUniform3fv(m_program.id(), location(name), static_cast<GLsizei>(values.size()), glm::value_ptr(values.front()));
}

void Shader::set(const char* name, const glm::mat3& value) const {
    glProgramUniformMatrix3fv(m_program.id(), location(name), 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::set(const char* name, const glm::mat4& value) const {
    glProgramUniformMatrix4fv(m_program.id(), location(name), 1, GL_FALSE, glm::value_ptr(value));
}

}
