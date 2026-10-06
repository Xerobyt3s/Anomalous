#pragma once

#include "core/types.h"

#include <span>
#include <string_view>

namespace anom {

struct SceneEntry {
    std::string_view id;
    std::string_view name;
    std::string_view zone_dir;
};

std::span<const SceneEntry> scenes();
i32 scene_index(std::string_view id);
i32 scene_index_of_dir(std::string_view zone_dir);

}
