#include "world/world.h"
#include "assets/assets.h"
#include "render/render.h"

#include <stdio.h>

void world_init(World* world, struct Arena* arena)
{
    pool_init(&world->entities, arena, sizeof(Entity), WORLD_MAX_ENTITIES);
}

EntityHandle world_spawn(World* world, EntityKind kind, Vec3 pos, Quat rot, f32 scale,
                         const char* mesh_name, u32 flags)
{
    EntityHandle handle = pool_alloc(&world->entities);
    Entity* entity = pool_get(&world->entities, handle);
    if (!entity) {
        return handle;
    }
    entity->kind = kind;
    entity->flags = flags;
    entity->pos = pos;
    entity->rot = rot;
    entity->scale = scale;
    entity->mesh = 0;
    entity->mesh_name[0] = 0;
    entity->aux_kind = 0;
    entity->aux_value = 0.0f;
    entity->body = HANDLE_INVALID;
    if (mesh_name) {
        snprintf(entity->mesh_name, sizeof(entity->mesh_name), "%s", mesh_name);
        entity->mesh = asset_mesh(mesh_name);
    }
    return handle;
}

Entity* world_entity(World* world, EntityHandle handle)
{
    return pool_get(&world->entities, handle);
}

void world_despawn(World* world, EntityHandle handle)
{
    pool_free(&world->entities, handle);
}

void world_render(World* world)
{
    for (u32 idx = 0; idx < world->entities.capacity; idx++) {
        Entity* entity = pool_at(&world->entities, idx);
        if (!entity || !entity->mesh) {
            continue;
        }
        Mat4 model = mat4_trs(entity->pos, entity->rot, v3(entity->scale, entity->scale, entity->scale));
        r_draw_mesh(entity->mesh, model);
    }
}
