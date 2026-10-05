#pragma once

#include "core/fixed_string.h"
#include "core/types.h"

#include <vector>

namespace anom {
class AssetCache;
class World;
struct Entity;
struct GpuMesh;

class EntityMeshes {
public:
    void bind(AssetCache& assets, const World& world);
    void set(u32 idx, const GpuMesh* mesh);
    const GpuMesh* get(u32 idx) const;

private:
    void ensure(u32 idx);

    std::vector<const GpuMesh*> meshes_;
    std::vector<FixedString<32>> names_;
};

}
