#include "render/entity_meshes.h"
#include "render/asset_cache.h"
#include "world/entity.h"

namespace anom {
void EntityMeshes::ensure(u32 idx)
{
    if (idx >= meshes_.size()) {
        meshes_.resize(idx + 1, nullptr);
        names_.resize(idx + 1);
    }
}

void EntityMeshes::bind(AssetCache& assets, const World& world)
{
    const Pool<Entity>& pool = world.entities();
    for (u32 idx = 0; idx < meshes_.size(); idx++) {
        const Entity* e = pool.at(idx);
        if (!e || e->kind == EntityKind::Island) {
            continue;
        }
        if (e->mesh_name != names_[idx].view()) {
            meshes_[idx] = nullptr;
            names_[idx].assign("");
        }
    }
    for (u32 idx : pool.live_indices()) {
        const Entity* e = pool.at(idx);
        if (!e || e->kind == EntityKind::Island) {
            continue;
        }
        ensure(idx);
        const bool drawable = !e->mesh_name.empty() && e->kind != EntityKind::Trigger && e->kind != EntityKind::Gravity
                           && e->kind != EntityKind::IslandLink && e->kind != EntityKind::Tree;
        if (!drawable) {
            meshes_[idx] = nullptr;
            names_[idx].assign("");
            continue;
        }
        if (meshes_[idx] && names_[idx] == e->mesh_name.view()) {
            continue;
        }
        meshes_[idx] = assets.mesh(e->mesh_name.view());
        names_[idx].assign(e->mesh_name.view());
    }
}

void EntityMeshes::set(u32 idx, const GpuMesh* mesh)
{
    ensure(idx);
    meshes_[idx] = mesh;
}

const GpuMesh* EntityMeshes::get(u32 idx) const
{
    return idx < meshes_.size() ? meshes_[idx] : nullptr;
}

}
