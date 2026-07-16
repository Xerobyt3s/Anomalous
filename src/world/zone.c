#include "world/zone.h"
#include "world/world.h"
#include "world/terrain.h"
#include "physics/physics.h"
#include "physics/heightfield.h"
#include "assets/assets.h"
#include "core/arena.h"
#include "core/config.h"
#include "core/log.h"
#include "platform/platform.h"

#include <stdio.h>
#include <string.h>

#define ZONE_MAX_STATIC_TRIS 16384

static EntityKind zone_kind_from_str(const char* kind_str)
{
    if (strcmp(kind_str, "tree") == 0) {
        return ENTITY_TREE;
    }
    if (strcmp(kind_str, "building") == 0) {
        return ENTITY_BUILDING;
    }
    return ENTITY_STATIC_MESH;
}

static void zone_add_mesh_collision(struct PhysWorld* phys, const char* mesh_name,
                                    EntityKind kind, Vec3 pos, Quat rot, f32 scale)
{
    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    char path[256];
    snprintf(path, sizeof(path), "assets/meshes/%s.amsh", mesh_name);
    MeshData data;
    if (assets_load_mesh_data(&g_frame_arena, path, &data)) {
        Mat3 rotation = quat_to_mat3(rot);
        for (u32 s = 0; s < data.submesh_count; s++) {
            const AmshSubmesh* sub = &data.submeshes[s];
            if (kind == ENTITY_TREE && strcmp(sub->material, "bark") != 0) {
                continue;
            }
            for (u32 i = 0; i + 2 < sub->index_count; i += 3) {
                Vec3 tri[3];
                for (u32 k = 0; k < 3; k++) {
                    const AmshVertex* v = &data.vertices[data.indices[sub->first_index + i + k]];
                    Vec3 local = vec3_scale(v3(v->pos[0], v->pos[1], v->pos[2]), scale);
                    tri[k] = vec3_add(pos, mat3_mul_vec3(rotation, local));
                }
                phys_add_static_tri(phys, tri[0], tri[1], tri[2]);
            }
        }
    }
    arena_temp_end(temp);
}

static void zone_spawn_entity(struct World* world, struct PhysWorld* phys,
                              const struct Terrain* terrain, const char* line)
{
    char kind_str[32];
    char mesh_name[32];
    f32 x, z, yaw_deg, scale;
    if (sscanf(line, "%31s %31s %f %f %f %f", kind_str, mesh_name, &x, &z, &yaw_deg, &scale) != 6) {
        log_warn("zone: malformed spawn line: %s", line);
        return;
    }
    EntityKind kind = zone_kind_from_str(kind_str);
    Vec3 pos = v3(x, heightfield_sample(&terrain->hf, x, z), z);
    Quat rot = quat_from_axis_angle(v3(0.0f, 1.0f, 0.0f), yaw_deg * DEG_TO_RAD);
    world_spawn(world, kind, pos, rot, scale, mesh_name, ENTITY_FLAG_COLLIDES);
    zone_add_mesh_collision(phys, mesh_name, kind, pos, rot, scale);
}

b32 zone_load(const char* zone_dir, struct Arena* arena, struct World* world,
              struct PhysWorld* phys, struct Terrain* terrain, ZoneSpawn* out_spawn)
{
    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    char path[256];
    snprintf(path, sizeof(path), "%s/zone.cfg", zone_dir);
    FileData file = platform_read_entire_file(&g_frame_arena, path);
    if (!file.data) {
        arena_temp_end(temp);
        return 0;
    }
    Config cfg;
    if (!config_parse(&cfg, &g_frame_arena, (const char*)file.data)) {
        arena_temp_end(temp);
        return 0;
    }

    if (!terrain_load(terrain, arena, zone_dir, &cfg)) {
        arena_temp_end(temp);
        return 0;
    }

    phys_statics_reserve(phys, arena, ZONE_MAX_STATIC_TRIS);

    u32 entity_count = 0;
    for (u32 i = 0; i < cfg.count; i++) {
        if (strcmp(cfg.entries[i].key, "entities.spawn") != 0) {
            continue;
        }
        zone_spawn_entity(world, phys, terrain, cfg.entries[i].value);
        entity_count++;
    }
    phys_statics_build(phys, arena);

    Vec3 car_xz = config_get_vec3(&cfg, "spawn.car_pos", vec3_zero());
    out_spawn->car_pos = v3(car_xz.x, heightfield_sample(&terrain->hf, car_xz.x, car_xz.y) + 1.0f, car_xz.y);
    out_spawn->car_yaw = config_get_f32(&cfg, "spawn.car_yaw_deg", 0.0f) * DEG_TO_RAD;

    log_info("zone: loaded %s | %u entities | %u static tris", zone_dir, entity_count,
             phys->statics.tri_count);
    arena_temp_end(temp);
    return 1;
}
