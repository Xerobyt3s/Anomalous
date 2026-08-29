#include "render/shader.h"
#include "assets/watcher.h"
#include "core/arena.h"
#include "core/log.h"
#include "platform/filesystem.h"
#include "platform/gl_loader.h"

#include <algorithm>

namespace anom {
namespace {

constexpr std::string_view kShaderDir = "shaders/";

std::string_view trim_left(std::string_view s)
{
    const std::size_t i = s.find_first_not_of(" \t");
    return i == std::string_view::npos ? std::string_view{} : s.substr(i);
}

bool read_file_text(std::string_view path, std::string& out)
{
    std::FILE* file = fs::open(path, "rb");
    if (!file) {
        return false;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(file);
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    const std::size_t got = std::fread(out.data(), 1, out.size(), file);
    std::fclose(file);
    out.resize(got);
    return true;
}

std::string_view include_target(std::string_view line)
{
    const std::string_view trimmed = trim_left(line);
    if (!trimmed.starts_with("#include")) {
        return {};
    }
    const std::size_t open = trimmed.find('"');
    if (open == std::string_view::npos) {
        return {};
    }
    const std::size_t close = trimmed.find('"', open + 1);
    if (close == std::string_view::npos) {
        return {};
    }
    return trimmed.substr(open + 1, close - open - 1);
}

} // namespace

bool ShaderLibrary::preprocess(std::string_view path, std::string& out,
                               std::vector<std::string>& dependencies,
                               std::vector<std::string>& included, i32 depth)
{
    if (depth > kMaxIncludeDepth) {
        log_error("shader: #include nested too deeply at %.*s", static_cast<int>(path.size()),
                  path.data());
        return false;
    }

    const std::string path_string(path);
    if (std::find(included.begin(), included.end(), path_string) != included.end()) {
        return true;
    }
    included.push_back(path_string);

    std::string source;
    if (!read_file_text(path, source)) {
        log_error("shader: cannot open %.*s", static_cast<int>(path.size()), path.data());
        return false;
    }
    if (std::find(dependencies.begin(), dependencies.end(), path_string) == dependencies.end()) {
        dependencies.push_back(path_string);
    }

    const int file_index = static_cast<int>(included.size()) - 1;
    int line_number = 0;
    bool need_line_directive = true;
    std::size_t cursor = 0;

    while (cursor <= source.size()) {
        const std::size_t newline = source.find('\n', cursor);
        std::string_view line = newline == std::string::npos
                                    ? std::string_view(source).substr(cursor)
                                    : std::string_view(source).substr(cursor, newline - cursor);
        cursor = newline == std::string::npos ? source.size() + 1 : newline + 1;
        line_number++;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        if (line.starts_with("#version")) {
            out.append(line);
            out.push_back('\n');
            need_line_directive = true;
            continue;
        }

        const std::string_view target = include_target(line);
        if (!target.empty()) {
            std::string include_path(kShaderDir);
            include_path.append(target);
            if (!preprocess(include_path, out, dependencies, included, depth + 1)) {
                return false;
            }
            need_line_directive = true;
            continue;
        }

        if (need_line_directive) {
            out.append("#line ");
            out.append(std::to_string(line_number));
            out.push_back(' ');
            out.append(std::to_string(file_index));
            out.push_back('\n');
            need_line_directive = false;
        }
        out.append(line);
        out.push_back('\n');

        if (newline == std::string::npos) {
            break;
        }
    }
    return true;
}

void ShaderLibrary::init(FileWatcher& watcher, Arena& scratch)
{
    watcher_ = &watcher;
    scratch_ = &scratch;
}

void ShaderLibrary::shutdown()
{
    for (u32 i = 0; i < kMaxShaders; i++) {
        if (entries_[i].used && entries_[i].program) {
            glDeleteProgram(entries_[i].program);
        }
        entries_[i] = Entry{};
    }
    count_ = 0;
}

u32 ShaderLibrary::compile_stage(u32 stage_type, std::string_view path,
                                 std::vector<std::string>& dependencies)
{
    std::string source;
    std::vector<std::string> included;
    if (!preprocess(path, source, dependencies, included)) {
        return 0;
    }

    const u32 shader = glCreateShader(stage_type);
    const auto* text = source.c_str();
    const GLint length = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &text, &length);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char info[4096];
        GLsizei info_len = 0;
        glGetShaderInfoLog(shader, sizeof(info), &info_len, info);
        log_error("shader compile failed %.*s: %.*s", static_cast<int>(path.size()), path.data(),
                  static_cast<int>(info_len), info);
        for (std::size_t i = 0; i < included.size(); i++) {
            log_error("  source %zu: %s", i, included[i].c_str());
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

u32 ShaderLibrary::build(Entry& entry, std::vector<std::string>* out_dependencies)
{
    std::vector<std::string> dependencies;

    const u32 vs = compile_stage(GL_VERTEX_SHADER, entry.vert_path.view(), dependencies);
    const u32 fs = compile_stage(GL_FRAGMENT_SHADER, entry.frag_path.view(), dependencies);
    if (out_dependencies) {
        *out_dependencies = dependencies;
    }
    if (!vs || !fs) {
        if (vs) { glDeleteShader(vs); }
        if (fs) { glDeleteShader(fs); }
        return 0;
    }

    const u32 program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDetachShader(program, vs);
    glDetachShader(program, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char info[4096];
        GLsizei info_len = 0;
        glGetProgramInfoLog(program, sizeof(info), &info_len, info);
        log_error("shader link failed %s: %.*s", entry.name.c_str(),
                  static_cast<int>(info_len), info);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

void ShaderLibrary::rewatch(Entry& entry, const std::vector<std::string>& dependencies)
{
    if (!watcher_) {
        return;
    }
    for (u32 i = 0; i < entry.watch_count; i++) {
        watcher_->remove(entry.watches[i]);
    }
    entry.watch_count = 0;

    for (const std::string& dependency : dependencies) {
        if (entry.watch_count >= kMaxDependencies) {
            log_warn("shader %s: more than %u dependencies, hot reload incomplete",
                     entry.name.c_str(), kMaxDependencies);
            break;
        }
        const u32 id = watcher_->add(dependency, &on_source_changed, &entry);
        if (id != FileWatcher::kInvalidId) {
            entry.watches[entry.watch_count++] = id;
        }
    }
}

void ShaderLibrary::on_source_changed(void* user, std::string_view path)
{
    auto* entry = static_cast<Entry*>(user);
    ShaderLibrary* self = entry->owner;

    std::vector<std::string> dependencies;
    const u32 program = self->build(*entry, &dependencies);
    if (!program) {
        log_warn("shader reload failed, keeping previous: %s", entry->name.c_str());
        return;
    }
    if (entry->program) {
        glDeleteProgram(entry->program);
    }
    entry->program = program;
    self->reload_count_++;
    self->rewatch(*entry, dependencies);
    log_info("shader reloaded: %s (%.*s)", entry->name.c_str(),
             static_cast<int>(path.size()), path.data());
}

u32 ShaderLibrary::program(std::string_view name)
{
    Entry* free_slot = nullptr;
    for (u32 i = 0; i < kMaxShaders; i++) {
        Entry& entry = entries_[i];
        if (entry.used && entry.name == name) {
            return entry.program;
        }
        if (!entry.used && !free_slot) {
            free_slot = &entry;
        }
    }
    if (!free_slot) {
        log_error("shaders: exhausted at capacity %u, cannot load %.*s", kMaxShaders,
                  static_cast<int>(name.size()), name.data());
        return 0;
    }

    Entry& entry = *free_slot;
    entry.name.assign(name);
    entry.vert_path.format("shaders/%.*s.vert", static_cast<int>(name.size()), name.data());
    entry.frag_path.format("shaders/%.*s.frag", static_cast<int>(name.size()), name.data());
    entry.owner = this;
    entry.used = true;
    count_++;

    std::vector<std::string> dependencies;
    entry.program = build(entry, &dependencies);
    rewatch(entry, dependencies);

    if (entry.program) {
        log_info("shader loaded: %s", entry.name.c_str());
    }
    return entry.program;
}

} // namespace anom
