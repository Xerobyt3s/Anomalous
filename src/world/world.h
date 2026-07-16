#pragma once

#include "core/types.h"
#include "core/pool.h"
#include "math/vmath.h"

struct Arena;
struct GpuMesh;

#define WORLD_MAX_ENTITIES 4096

#define ENTITY_FLAG_COLLIDES (1u << 0)
#define ENTITY_FLAG_INTERACTABLE (1u << 1)

typedef enum EntityKind {
    ENTITY_STATIC_MESH,
    ENTITY_TREE,
    ENTITY_BUILDING,
    ENTITY_VEHICLE,
    ENTITY_PART_PICKUP,
    ENTITY_TRIGGER,
} EntityKind;

typedef Handle EntityHandle;

typedef struct Entity {
    EntityKind kind;
    u32 flags;
    Vec3 pos;
    Quat rot;
    f32 scale;
    const struct GpuMesh* mesh;
    char mesh_name[32];
    u32 aux_kind;
    f32 aux_value;
    Handle body;
} Entity;

typedef struct World {
    Pool entities;
} World;

void         world_init(World* world, struct Arena* arena);
EntityHandle world_spawn(World* world, EntityKind kind, Vec3 pos, Quat rot, f32 scale,
                         const char* mesh_name, u32 flags);
Entity*      world_entity(World* world, EntityHandle handle);
void         world_despawn(World* world, EntityHandle handle);
void         world_render(World* world);
