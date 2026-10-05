#pragma once

#include "core/types.h"
#include "engine/render/shader.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace anom {

class ShaderSet {
public:
    static constexpr f64 kReloadInterval = 0.25;

    const ghost::engine::Shader* get(std::string_view name, std::string_view vertex = {});
    u32 program(std::string_view name);
    void reload_if_changed(f64 now);
    void clear() { entries_.clear(); }

    u32 count() const { return static_cast<u32>(entries_.size()); }
    u32 reload_count() const { return reload_count_; }

private:
    struct Entry {
        std::string name;
        std::string vertex;
        std::unique_ptr<ghost::engine::Shader> shader;
        std::filesystem::file_time_type failed_vert{};
        std::filesystem::file_time_type failed_frag{};
    };

    bool load(Entry& entry);

    std::vector<Entry> entries_;
    f64 last_poll_ = 0.0;
    u32 reload_count_ = 0;
};

} // namespace anom
