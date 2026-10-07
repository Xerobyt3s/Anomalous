#include "render/mesh_glow.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace anom {

MeshGlowTable parse_mesh_glow(std::string_view json)
{
    MeshGlowTable table;
    const nlohmann::json doc = nlohmann::json::parse(json, nullptr, false);
    if (!doc.is_object() || !doc.contains("meshes") || !doc["meshes"].is_object()) {
        return table;
    }
    for (const auto& [name, value] : doc["meshes"].items()) {
        if (value.is_number()) {
            table.entries.emplace_back(name, std::max(value.get<f32>(), 0.0f));
        }
    }
    return table;
}

f32 mesh_glow(const MeshGlowTable& table, std::string_view mesh)
{
    for (const auto& [name, glow] : table.entries) {
        if (name == mesh) {
            return glow;
        }
    }
    return 0.0f;
}

} // namespace anom
