#pragma once

#include "core/types.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace anom {

struct MeshGlowTable {
    std::vector<std::pair<std::string, f32>> entries;
};

MeshGlowTable parse_mesh_glow(std::string_view json);
f32 mesh_glow(const MeshGlowTable& table, std::string_view mesh);

} // namespace anom
