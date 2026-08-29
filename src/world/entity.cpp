#include "world/entity.h"
#include "core/arena.h"

namespace anom {

void World::init(Arena& arena)
{
    entities_.init(arena, kMaxEntities, "entities");
}

EntityHandle World::spawn(EntityKind kind, Vec3 pos, Quat rot, f32 scale,
                          std::string_view mesh_name, u32 flags)
{
    const EntityHandle handle = entities_.alloc();
    Entity* entity = entities_.get(handle);
    if (!entity) {
        return handle;
    }
    entity->kind = kind;
    entity->flags = flags;
    entity->pos = pos;
    entity->rot = rot;
    entity->scale = scale;
    entity->half = Vec3{0.0f, 0.0f, 0.0f};
    entity->mesh = nullptr;
    entity->body = BodyHandle{};
    if (!mesh_name.empty()) {
        entity->mesh_name.assign(mesh_name);
    }
    return handle;
}

} // namespace anom
