#include "render/shader_set.h"
#include "core/log.h"
#include "engine/assets/asset_path.h"

#include <exception>
#include <system_error>

namespace anom {
namespace {

std::filesystem::path stage_path(std::string_view name, const char* ext)
{
    std::string file(name);
    file += ext;
    return ghost::engine::assetPath("shaders") / file;
}

std::filesystem::file_time_type stamp(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path, ec);
    return ec ? std::filesystem::file_time_type{} : time;
}

} // namespace

bool ShaderSet::load(Entry& entry)
{
    const std::filesystem::path vert = stage_path(entry.vertex, ".vert");
    const std::filesystem::path frag = stage_path(entry.name, ".frag");
    try {
        entry.shader = std::make_unique<ghost::engine::Shader>(vert, frag);
        return true;
    } catch (const std::exception& e) {
        log_error("shaders: %s", e.what());
        entry.failed_vert = stamp(vert);
        entry.failed_frag = stamp(frag);
        return false;
    }
}

const ghost::engine::Shader* ShaderSet::get(std::string_view name, std::string_view vertex)
{
    for (Entry& entry : entries_) {
        if (entry.name == name) {
            return entry.shader.get();
        }
    }
    Entry& entry = entries_.emplace_back();
    entry.name.assign(name);
    entry.vertex.assign(vertex.empty() ? name : vertex);
    load(entry);
    return entry.shader.get();
}

u32 ShaderSet::program(std::string_view name)
{
    const ghost::engine::Shader* shader = get(name);
    return shader ? shader->id() : 0u;
}

void ShaderSet::reload_if_changed(f64 now)
{
    if (now - last_poll_ < kReloadInterval) {
        return;
    }
    last_poll_ = now;
    for (Entry& entry : entries_) {
        if (entry.shader) {
            const GLuint before = entry.shader->id();
            entry.shader->reloadIfChanged();
            reload_count_ += entry.shader->id() != before ? 1u : 0u;
            continue;
        }
        if (stamp(stage_path(entry.vertex, ".vert")) != entry.failed_vert
            || stamp(stage_path(entry.name, ".frag")) != entry.failed_frag) {
            reload_count_ += load(entry) ? 1u : 0u;
        }
    }
}

} // namespace anom
