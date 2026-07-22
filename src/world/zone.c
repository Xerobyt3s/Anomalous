#include "world/zone.h"
#include "world/world.h"
#include "world/terrain.h"
#include "physics/physics.h"
#include "physics/heightfield.h"
#include "assets/assets.h"
#include "carsys/items.h"
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

static const char* zone_kind_to_str(EntityKind kind)
{
    if (kind == ENTITY_TREE) {
        return "tree";
    }
    if (kind == ENTITY_BUILDING) {
        return "building";
    }
    return "static";
}

static f32 zone_entity_yaw(Quat rot)
{
    return quat_yaw(rot) * RAD_TO_DEG;
}

static void zone_add_mesh_collision(struct PhysWorld* phys, const char* mesh_name,
                                    EntityKind kind, Vec3 pos, Quat rot, f32 scale)
{
    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    char path[256];
    MeshData data = {0};
    b32 loaded = 0;
    snprintf(path, sizeof(path), "assets/meshes/%s_col.amsh", mesh_name);
    if (platform_file_mtime(path) != 0) {
        loaded = assets_load_mesh_data(&g_frame_arena, path, &data);
        if (loaded) {
            kind = ENTITY_BUILDING;
        }
    }
    if (!loaded) {
        snprintf(path, sizeof(path), "assets/meshes/%s.amsh", mesh_name);
        loaded = assets_load_mesh_data(&g_frame_arena, path, &data);
    }
    if (loaded) {
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
    f32 yoff = 0.0f, pitch_deg = 0.0f, roll_deg = 0.0f;
    i32 fields = sscanf(line, "%31s %31s %f %f %f %f %f %f %f", kind_str, mesh_name,
                        &x, &z, &yaw_deg, &scale, &yoff, &pitch_deg, &roll_deg);
    if (fields < 6) {
        log_warn("zone: malformed spawn line: %s", line);
        return;
    }
    if (fields < 7) {
        yoff = 0.0f;
    }
    if (fields < 9) {
        pitch_deg = 0.0f;
        roll_deg = 0.0f;
    }
    EntityKind kind = zone_kind_from_str(kind_str);
    Vec3 pos = v3(x, heightfield_sample(&terrain->hf, x, z) + yoff, z);
    Quat rot = quat_from_euler(yaw_deg * DEG_TO_RAD, pitch_deg * DEG_TO_RAD, roll_deg * DEG_TO_RAD);
    world_spawn(world, kind, pos, rot, scale, mesh_name, ENTITY_FLAG_COLLIDES);
    zone_add_mesh_collision(phys, mesh_name, kind, pos, rot, scale);
}

static void zone_spawn_trigger(struct World* world, const struct Terrain* terrain,
                               const char* line)
{
    char name[32];
    f32 x, yoff, z, hx, hy, hz, yaw_deg, param;
    i32 action;
    i32 fields = sscanf(line, "%31s %f %f %f %f %f %f %f %d %f", name, &x, &yoff, &z,
                        &hx, &hy, &hz, &yaw_deg, &action, &param);
    if (fields < 9) {
        log_warn("zone: malformed trigger line: %s", line);
        return;
    }
    if (fields < 10) {
        param = 0.0f;
    }
    Vec3 pos = v3(x, heightfield_sample(&terrain->hf, x, z) + yoff, z);
    Quat rot = quat_from_axis_angle(v3(0.0f, 1.0f, 0.0f), yaw_deg * DEG_TO_RAD);
    Handle handle = world_spawn(world, ENTITY_TRIGGER, pos, rot, 1.0f, 0,
                                ENTITY_FLAG_INTERACTABLE);
    Entity* e = world_entity(world, handle);
    if (e) {
        snprintf(e->mesh_name, sizeof(e->mesh_name), "%s", name);
        e->half = v3(hx, hy, hz);
        e->aux_kind = (u32)action;
        e->aux_value = param;
    }
}

static void zone_spawn_tower(struct World* world, struct PhysWorld* phys,
                             const struct Terrain* terrain, const char* line,
                             ZoneSpawn* out_spawn)
{
    f32 x, z, yaw_deg;
    if (sscanf(line, "%f %f %f", &x, &z, &yaw_deg) != 3) {
        log_warn("zone: malformed tower line: %s", line);
        return;
    }
    Vec3 pos = v3(x, heightfield_sample(&terrain->hf, x, z), z);
    Quat rot = quat_from_axis_angle(v3(0.0f, 1.0f, 0.0f), yaw_deg * DEG_TO_RAD);
    world_spawn(world, ENTITY_BUILDING, pos, rot, 1.0f, "relay_tower",
                ENTITY_FLAG_COLLIDES | ENTITY_FLAG_TOWER);
    zone_add_mesh_collision(phys, "relay_tower", ENTITY_BUILDING, pos, rot, 1.0f);
    if (out_spawn) {
        out_spawn->tower_present = 1;
        out_spawn->tower_x = x;
        out_spawn->tower_z = z;
        out_spawn->tower_yaw_deg = yaw_deg;
    }
}

static void zone_collect_pickup(const char* line, ZonePickups* out_pickups)
{
    if (!out_pickups || out_pickups->count >= ZONE_MAX_PICKUPS) {
        return;
    }
    ZonePickup* p = &out_pickups->items[out_pickups->count];
    i32 fields = sscanf(line, "%31s %f %f %f %f %d", p->item, &p->x, &p->z, &p->yaw_deg,
                        &p->condition, &p->aux);
    if (fields < 4) {
        log_warn("zone: malformed pickup line: %s", line);
        return;
    }
    if (fields < 5) {
        p->condition = 1.0f;
    }
    if (fields < 6) {
        p->aux = 0;
    }
    out_pickups->count++;
}

static u32 zone_spawn_from_config(struct World* world, struct PhysWorld* phys,
                                  const struct Terrain* terrain, const Config* cfg,
                                  ZoneSpawn* out_spawn, ZonePickups* out_pickups)
{
    u32 n = 0;
    for (u32 i = 0; i < cfg->count; i++) {
        const char* key = cfg->entries[i].key;
        const char* value = cfg->entries[i].value;
        if (strcmp(key, "entities.spawn") == 0) {
            zone_spawn_entity(world, phys, terrain, value);
            n++;
        } else if (strcmp(key, "entities.trigger") == 0) {
            zone_spawn_trigger(world, terrain, value);
            n++;
        } else if (strcmp(key, "entities.tower") == 0) {
            zone_spawn_tower(world, phys, terrain, value, out_spawn);
            n++;
        } else if (strcmp(key, "entities.pickup") == 0) {
            zone_collect_pickup(value, out_pickups);
        }
    }
    return n;
}

b32 zone_load(const char* zone_dir, struct Arena* arena, struct World* world,
              struct PhysWorld* phys, struct Terrain* terrain, ZoneSpawn* out_spawn,
              ZonePickups* out_pickups)
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

    out_spawn->tower_present = 0;
    if (out_pickups) {
        out_pickups->count = 0;
    }
    u32 entity_count = zone_spawn_from_config(world, phys, terrain, &cfg, out_spawn, out_pickups);
    phys_statics_build(phys, arena);

    Vec3 car_xz = config_get_vec3(&cfg, "spawn.car_pos", vec3_zero());
    out_spawn->car_pos = v3(car_xz.x, heightfield_sample(&terrain->hf, car_xz.x, car_xz.y) + 1.0f, car_xz.y);
    out_spawn->car_yaw = config_get_f32(&cfg, "spawn.car_yaw_deg", 0.0f) * DEG_TO_RAD;

    Quat car_rot = quat_from_axis_angle(v3(0.0f, 1.0f, 0.0f), out_spawn->car_yaw);
    Vec3 beside_door = vec3_add(out_spawn->car_pos, quat_rotate_vec3(car_rot, v3(-2.6f, 0.0f, -0.3f)));
    Vec3 player_xz = config_get_vec3(&cfg, "spawn.player_pos", v3(beside_door.x, beside_door.z, 0.0f));
    out_spawn->player_pos = v3(player_xz.x, heightfield_sample(&terrain->hf, player_xz.x, player_xz.y), player_xz.y);
    Vec3 to_car = vec3_sub(out_spawn->car_pos, out_spawn->player_pos);
    out_spawn->player_yaw = config_get_f32(&cfg, "spawn.player_yaw_deg", atan2f(to_car.x, -to_car.z) * RAD_TO_DEG) * DEG_TO_RAD;

    log_info("zone: loaded %s | %u entities | %u static tris", zone_dir, entity_count,
             phys->statics.tri_count);
    arena_temp_end(temp);
    return 1;
}

static b32 zone_line_is_entity(const char* p)
{
    return strncmp(p, "spawn", 5) == 0 || strncmp(p, "pickup", 6) == 0
        || strncmp(p, "trigger", 7) == 0 || strncmp(p, "tower", 5) == 0;
}

b32 zone_save(const char* zone_dir, struct World* world, struct PhysWorld* phys,
              const struct Terrain* terrain)
{
    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    char path[256];
    snprintf(path, sizeof(path), "%s/zone.cfg", zone_dir);
    FileData file = platform_read_entire_file(&g_frame_arena, path);
    FILE* out = (FILE*)platform_fopen(path, "wb");
    if (!out) {
        log_warn("zone: could not open %s for writing", path);
        arena_temp_end(temp);
        return 0;
    }

    b32 have_entities_header = 0;
    b32 in_spawn_section = 0;
    if (file.data) {
        const char* s = (const char*)file.data;
        while (*s) {
            char buf[512];
            u32 n = 0;
            while (*s && *s != '\n' && n < sizeof(buf) - 1) {
                if (*s != '\r') {
                    buf[n++] = *s;
                }
                s++;
            }
            while (*s && *s != '\n') {
                s++;
            }
            if (*s == '\n') {
                s++;
            }
            buf[n] = 0;
            const char* p = buf;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p == '[') {
                in_spawn_section = strncmp(p, "[spawn]", 7) == 0;
                if (strncmp(p, "[entities]", 10) == 0) {
                    have_entities_header = 1;
                }
            }
            if (in_spawn_section || *p == '[' || *p == '#' || *p == 0
                || !zone_line_is_entity(p)) {
                fprintf(out, "%s\n", buf);
            }
        }
    }
    if (!have_entities_header) {
        fprintf(out, "\n[entities]\n");
    }

    u32 written = 0;
    for (u32 idx = 0; idx < world->entities.capacity; idx++) {
        Entity* e = pool_at(&world->entities, idx);
        if (!e) {
            continue;
        }
        if (e->flags & ENTITY_FLAG_TOWER) {
            fprintf(out, "tower = %.3f %.3f %.2f\n", (f64)e->pos.x, (f64)e->pos.z,
                    (f64)zone_entity_yaw(e->rot));
            written++;
            continue;
        }
        if (e->kind == ENTITY_TRIGGER) {
            f32 ground = heightfield_sample(&terrain->hf, e->pos.x, e->pos.z);
            fprintf(out, "trigger = %s %.3f %.3f %.3f %.3f %.3f %.3f %.2f %d %.3f\n",
                    e->mesh_name[0] ? e->mesh_name : "unnamed", (f64)e->pos.x,
                    (f64)(e->pos.y - ground), (f64)e->pos.z, (f64)e->half.x, (f64)e->half.y,
                    (f64)e->half.z, (f64)zone_entity_yaw(e->rot), (int)e->aux_kind,
                    (f64)e->aux_value);
            written++;
            continue;
        }
        if (e->kind == ENTITY_PART_PICKUP) {
            Vec3 pos = e->pos;
            f32 yaw = zone_entity_yaw(e->rot);
            RigidBody* body = phys_body(phys, e->body);
            if (body) {
                pos = body->pos;
                yaw = zone_entity_yaw(body->rot);
            }
            fprintf(out, "pickup = %s %.3f %.3f %.2f %.3f %d\n", item_id((ItemKind)e->aux_kind),
                    (f64)pos.x, (f64)pos.z, (f64)yaw, (f64)e->aux_value, (int)e->aux_data);
            written++;
            continue;
        }
        if (e->kind == ENTITY_VEHICLE || e->mesh_name[0] == 0) {
            continue;
        }
        f32 ground = heightfield_sample(&terrain->hf, e->pos.x, e->pos.z);
        f32 yoff = e->pos.y - ground;
        f32 yaw = zone_entity_yaw(e->rot);
        if (f_abs(yoff) > 0.01f) {
            fprintf(out, "spawn = %s %s %.3f %.3f %.2f %.3f %.3f\n",
                    zone_kind_to_str(e->kind), e->mesh_name, (f64)e->pos.x, (f64)e->pos.z,
                    (f64)yaw, (f64)e->scale, (f64)yoff);
        } else {
            fprintf(out, "spawn = %s %s %.3f %.3f %.2f %.3f\n",
                    zone_kind_to_str(e->kind), e->mesh_name, (f64)e->pos.x, (f64)e->pos.z,
                    (f64)yaw, (f64)e->scale);
        }
        written++;
    }
    fclose(out);
    log_info("zone: saved %s | %u entities", zone_dir, written);
    arena_temp_end(temp);
    return 1;
}

b32 zone_reload(const char* zone_dir, struct Arena* arena, struct World* world,
                struct PhysWorld* phys, const struct Terrain* terrain,
                ZonePickups* out_pickups)
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
    for (u32 idx = 0; idx < world->entities.capacity; idx++) {
        Entity* e = pool_at(&world->entities, idx);
        if (e && handle_valid(e->body)) {
            phys_body_destroy(phys, e->body);
        }
    }
    world_clear(world);
    phys_statics_reserve(phys, arena, ZONE_MAX_STATIC_TRIS);
    if (out_pickups) {
        out_pickups->count = 0;
    }
    u32 count = zone_spawn_from_config(world, phys, terrain, &cfg, 0, out_pickups);
    phys_statics_build(phys, arena);
    log_info("zone: reloaded %s | %u entities", zone_dir, count);
    arena_temp_end(temp);
    return 1;
}
