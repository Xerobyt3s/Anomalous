#include "player/interact.h"
#include "player/player.h"
#include "vehicle/vehicle.h"
#include "carsys/carsys.h"
#include "world/world.h"
#include "physics/physics.h"
#include "physics/heightfield.h"

#include <stdio.h>
#include <string.h>

#define HOOD_OPEN_FOR_BAY 0.8f
#define DOOR_OPEN_FOR_USE 0.6f
#define EXIT_LOOK_YAW 1.15f
#define DOOR_HINGE_X 0.80f

void interact_init(Interact* it)
{
    Interact zero = {0};
    *it = zero;
}

typedef struct Candidate {
    f32 t;
    InteractAction action;
    PartKind part;
    Handle entity;
    i32 side;
    u32 cargo;
    b32 is_hold;
    Vec3 center;
    Vec3 half;
    Vec3 place_pos;
    char prompt[96];
} Candidate;

static b32 candidate_consider(Candidate* best, f32 t, InteractAction action, Vec3 center,
                              Vec3 half, b32 is_hold, const char* prompt)
{
    if (action == ACTION_NONE || t >= best->t) {
        return 0;
    }
    best->t = t;
    best->action = action;
    best->is_hold = is_hold;
    best->center = center;
    best->half = half;
    snprintf(best->prompt, sizeof(best->prompt), "%s", prompt);
    return 1;
}

static b32 ray_vs_local_box(Ray local, Vec3 center, Vec3 half, f32* out_t)
{
    Aabb box;
    box.min = vec3_sub(center, half);
    box.max = vec3_add(center, half);
    return ray_vs_aabb(local, box, INTERACT_RANGE, out_t);
}

static Ray chassis_local_ray(const RigidBody* body, Ray view_ray)
{
    Quat inv_rot = quat_conjugate(body->rot);
    Ray local;
    local.origin = quat_rotate_vec3(inv_rot, vec3_sub(view_ray.origin, body->pos));
    local.dir = quat_rotate_vec3(inv_rot, view_ray.dir);
    return local;
}

static void resolve_doors(Candidate* best, const struct Player* player, Vehicle* veh, CarSys* sys,
                          PhysWorld* phys, Ray local, b32 on_foot)
{
    Vec3 com = veh->cfg.com_offset;
    Vec3 door_half = v3(0.10f, 0.26f, 0.62f);
    f32 t;
    for (i32 side = 0; side < 2; side++) {
        f32 sign = side == 0 ? -1.0f : 1.0f;
        b32 open = sys->door_open[side] > DOOR_OPEN_FOR_USE;
        Vec3 center = vec3_sub(v3(sign * 0.78f, -0.02f, 0.10f), com);
        if (ray_vs_local_box(local, center, door_half, &t)) {
            b32 taken = 0;
            if (on_foot) {
                if (!open) {
                    taken = candidate_consider(best, t + 0.05f, ACTION_OPEN_DOOR, center,
                                               door_half, 0, "[E] open door");
                } else if (player_can_enter(player, phys, veh)) {
                    taken = candidate_consider(best, t + 0.05f, ACTION_ENTER_CAR, center,
                                               door_half, 0, "[E] enter car");
                }
            } else {
                taken = candidate_consider(best, t + 0.05f,
                                           open ? ACTION_CLOSE_DOOR : ACTION_OPEN_DOOR,
                                           center, door_half, 0,
                                           open ? "[E] close door" : "[E] open door");
            }
            if (taken) {
                best->side = side;
            }
        }
        if (on_foot && open) {
            Vec3 panel_center = vec3_sub(v3(sign * 1.45f, -0.02f, -0.18f), com);
            Vec3 panel_half = v3(0.48f, 0.26f, 0.42f);
            if (ray_vs_local_box(local, panel_center, panel_half, &t)
                && candidate_consider(best, t + 0.10f, ACTION_CLOSE_DOOR, panel_center,
                                      panel_half, 0, "[E] close door")) {
                best->side = side;
            }
        }
    }
}

static void resolve_in_car(Candidate* best, Interact* it, struct Player* player, Vehicle* veh,
                           CarSys* sys, PhysWorld* phys, Ray view_ray)
{
    RigidBody* body = phys_body(phys, veh->body);
    if (!body) {
        return;
    }
    Ray local = chassis_local_ray(body, view_ray);
    Vec3 com = veh->cfg.com_offset;
    f32 t;

    resolve_doors(best, player, veh, sys, phys, local, 0);

    Vec3 lever_center = vec3_sub(v3(-0.13f, -0.13f, 0.33f), com);
    Vec3 lever_half = v3(0.09f, 0.10f, 0.16f);
    if (ray_vs_local_box(local, lever_center, lever_half, &t)) {
        candidate_consider(best, t, ACTION_HANDBRAKE, lever_center, lever_half, 0,
                           sys->handbrake_latched ? "[E] release handbrake" : "[E] set handbrake");
    }

    for (i32 side = 0; side < 2; side++) {
        f32 sign = side == 0 ? -1.0f : 1.0f;
        if (sys->door_open[side] < DOOR_OPEN_FOR_USE) {
            continue;
        }
        if (player->look_yaw * sign > EXIT_LOOK_YAW && player_can_exit(player, phys, veh)) {
            Vec3 center = vec3_sub(v3(sign * 0.95f, 0.0f, 0.10f), com);
            if (candidate_consider(best, 0.05f, ACTION_EXIT_CAR, center,
                                   v3(0.05f, 0.25f, 0.55f), 0, "[E] get out")) {
                best->side = side;
            }
        }
    }
    (void)it;
}

static void resolve_car_targets(Candidate* best, const Interact* it, struct Player* player,
                                Vehicle* veh, CarSys* sys, PhysWorld* phys, Ray view_ray)
{
    RigidBody* body = phys_body(phys, veh->body);
    if (!body) {
        return;
    }
    Ray local = chassis_local_ray(body, view_ray);
    Vec3 com = veh->cfg.com_offset;
    char prompt[96];
    f32 t;

    for (u32 k = 0; k < PART_COUNT; k++) {
        const PartDef* def = part_def((PartKind)k);
        PartSlot* slot = &sys->parts[k];
        if (def->engine_bay && sys->hood_open < HOOD_OPEN_FOR_BAY) {
            continue;
        }
        Vec3 center = vec3_sub(def->socket_pos, com);
        if (!ray_vs_local_box(local, center, def->socket_half, &t)) {
            continue;
        }
        b32 taken = 0;
        if (slot->installed) {
            if ((PartKind)k == PART_ENGINE && it->hands.kind == ITEM_OILCAN) {
                taken = candidate_consider(best, t, ACTION_OIL_FILL, center, def->socket_half, 1,
                                           "hold [E] top up oil");
            } else if (def->removable && it->hands.kind == ITEM_NONE) {
                snprintf(prompt, sizeof(prompt), "hold [E] take %s (%.0f%%)",
                         def->name, (f64)(slot->condition * 100.0f));
                taken = candidate_consider(best, t, ACTION_REMOVE_PART, center, def->socket_half,
                                           1, prompt);
            } else {
                snprintf(prompt, sizeof(prompt), "%s %.0f%%",
                         def->name, (f64)(slot->condition * 100.0f));
                taken = candidate_consider(best, t, ACTION_INFO, center, def->socket_half, 0,
                                           prompt);
            }
        } else if (item_for_part((PartKind)k) == it->hands.kind && it->hands.kind != ITEM_NONE) {
            snprintf(prompt, sizeof(prompt), "hold [E] install %s (%.0f%%)",
                     def->name, (f64)(it->hands.condition * 100.0f));
            taken = candidate_consider(best, t, ACTION_INSTALL_PART, center, def->socket_half,
                                       1, prompt);
        } else {
            snprintf(prompt, sizeof(prompt), "%s missing", def->name);
            taken = candidate_consider(best, t, ACTION_INFO, center, def->socket_half, 0, prompt);
        }
        if (taken) {
            best->part = (PartKind)k;
        }
    }

    resolve_doors(best, player, veh, sys, phys, local, 1);

    if (sys->hood_open < 0.5f) {
        Vec3 latch_half = v3(0.50f, 0.10f, 0.42f);
        Vec3 latch_center = vec3_sub(v3(0.0f, 0.04f, -1.50f), com);
        if (ray_vs_local_box(local, latch_center, latch_half, &t)) {
            candidate_consider(best, t + 0.15f, ACTION_TOGGLE_HOOD, latch_center, latch_half, 0,
                               "[E] open hood");
        }
    } else {
        Vec3 raised_half = v3(0.50f, 0.62f, 0.30f);
        Vec3 raised_center = vec3_sub(v3(0.0f, 0.72f, -0.92f), com);
        if (ray_vs_local_box(local, raised_center, raised_half, &t)) {
            candidate_consider(best, t + 2.0f, ACTION_TOGGLE_HOOD, raised_center, raised_half, 0,
                               "[E] close hood");
        }
    }

    if (sys->trunk_open < 0.5f) {
        Vec3 lid_half = v3(0.50f, 0.09f, 0.31f);
        Vec3 lid_center = vec3_sub(v3(0.0f, 0.22f, 1.73f), com);
        if (ray_vs_local_box(local, lid_center, lid_half, &t)) {
            candidate_consider(best, t, ACTION_TOGGLE_TRUNK, lid_center, lid_half, 0,
                               "[E] open trunk");
        }
    } else {
        Vec3 edge_half = v3(0.50f, 0.38f, 0.16f);
        Vec3 edge_center = vec3_sub(v3(0.0f, 0.55f, 1.52f), com);
        if (ray_vs_local_box(local, edge_center, edge_half, &t)) {
            candidate_consider(best, t + 0.1f, ACTION_TOGGLE_TRUNK, edge_center, edge_half, 0,
                               "[E] close trunk");
        }

        if (it->hands.kind != ITEM_NONE) {
            Vec3 half = item_cargo_half(it->hands.kind);
            f32 plane_y = TRUNK_FLOOR_Y + half.y - com.y;
            if (local.dir.y < -0.05f) {
                f32 hit_t = (plane_y - local.origin.y) / local.dir.y;
                if (hit_t > 0.0f && hit_t < INTERACT_RANGE) {
                    Vec3 hit = vec3_add(local.origin, vec3_scale(local.dir, hit_t));
                    Vec3 hit_cfg = vec3_add(hit, com);
                    if (hit_cfg.z > TRUNK_MIN_Z - 0.25f && hit_cfg.z < TRUNK_MAX_Z + 0.25f
                        && f_abs(hit_cfg.x) < TRUNK_MAX_X + 0.25f) {
                        Vec3 place;
                        place.x = f_clamp(hit_cfg.x, TRUNK_MIN_X + half.x,
                                          f_max(TRUNK_MAX_X - half.x, TRUNK_MIN_X + half.x));
                        place.z = f_clamp(hit_cfg.z, TRUNK_MIN_Z + half.z,
                                          f_max(TRUNK_MAX_Z - half.z, TRUNK_MIN_Z + half.z));
                        b32 fits = 0;
                        place.y = carsys_cargo_place_y(sys, it->hands.kind, place.x, place.z, &fits);
                        if (fits) {
                            snprintf(prompt, sizeof(prompt), "[E] place %s",
                                     item_name(it->hands.kind));
                            candidate_consider(best, hit_t, ACTION_PLACE_CARGO,
                                               vec3_sub(place, com), half, 0, prompt);
                            best->place_pos = place;
                        } else {
                            candidate_consider(best, hit_t, ACTION_INFO,
                                               vec3_sub(place, com), half, 0, "no room there");
                        }
                    }
                }
            }
        } else {
            for (u32 i = 0; i < CARGO_MAX; i++) {
                const CargoItem* c = &sys->cargo[i];
                if (!c->used) {
                    continue;
                }
                Vec3 half = item_cargo_half(c->item.kind);
                Vec3 center = vec3_sub(c->pos, com);
                if (!ray_vs_local_box(local, center, half, &t)) {
                    continue;
                }
                snprintf(prompt, sizeof(prompt), "[E] take %s (%.0f%%)",
                         item_name(c->item.kind), (f64)(c->item.condition * 100.0f));
                if (candidate_consider(best, t, ACTION_TAKE_CARGO, center, half, 0, prompt)) {
                    best->cargo = i;
                }
            }
        }
    }

    Vec3 filler_half = v3(0.10f, 0.10f, 0.14f);
    Vec3 filler_center = vec3_sub(v3(0.80f, 0.10f, 1.30f), com);
    if (it->hands.kind == ITEM_JERRYCAN && ray_vs_local_box(local, filler_center, filler_half, &t)) {
        candidate_consider(best, t, ACTION_REFUEL, filler_center, filler_half, 1, "hold [E] refuel");
    }
}

static void resolve_pickups(Candidate* best, const Interact* it, World* world, Ray view_ray)
{
    if (it->hands.kind != ITEM_NONE) {
        return;
    }
    char prompt[96];
    for (u32 idx = 0; idx < world->entities.capacity; idx++) {
        Entity* entity = pool_at(&world->entities, idx);
        if (!entity || entity->kind != ENTITY_PART_PICKUP) {
            continue;
        }
        Sphere sphere;
        sphere.center = vec3_add(entity->pos, v3(0.0f, 0.22f, 0.0f));
        sphere.radius = 0.45f * entity->scale;
        f32 t;
        if (!ray_vs_sphere(view_ray, sphere, INTERACT_RANGE, &t)) {
            continue;
        }
        snprintf(prompt, sizeof(prompt), "[E] take %s (%.0f%%)",
                 item_name((ItemKind)entity->aux_kind), (f64)(entity->aux_value * 100.0f));
        if (candidate_consider(best, t, ACTION_PICKUP, vec3_zero(), vec3_zero(), 0, prompt)) {
            best->entity.idx = idx;
            best->entity.gen = world->entities.gens[idx];
        }
    }
}

static void interact_perform(Interact* it, CarSys* sys, World* world)
{
    switch (it->action) {
    case ACTION_OPEN_DOOR:
        sys->door_target[it->target_side] = 1;
        break;
    case ACTION_CLOSE_DOOR:
        sys->door_target[it->target_side] = 0;
        break;
    case ACTION_HANDBRAKE:
        sys->handbrake_latched = !sys->handbrake_latched;
        break;
    case ACTION_REMOVE_PART: {
        PartSlot* slot = &sys->parts[it->target_part];
        it->hands.kind = item_for_part(it->target_part);
        it->hands.condition = slot->condition;
        slot->installed = 0;
        break;
    }
    case ACTION_INSTALL_PART: {
        PartSlot* slot = &sys->parts[it->target_part];
        slot->installed = 1;
        slot->condition = it->hands.condition;
        it->hands.kind = ITEM_NONE;
        break;
    }
    case ACTION_TOGGLE_HOOD:
        sys->hood_target = !sys->hood_target;
        break;
    case ACTION_TOGGLE_TRUNK:
        sys->trunk_target = !sys->trunk_target;
        break;
    case ACTION_PLACE_CARGO:
        if (carsys_cargo_add(sys, it->hands, it->place_pos)) {
            it->hands.kind = ITEM_NONE;
        }
        break;
    case ACTION_TAKE_CARGO:
        carsys_cargo_take(sys, it->target_cargo, &it->hands);
        break;
    case ACTION_PICKUP: {
        Entity* entity = world_entity(world, it->target_entity);
        if (entity) {
            it->hands.kind = (ItemKind)entity->aux_kind;
            it->hands.condition = entity->aux_value;
            world_despawn(world, it->target_entity);
        }
        break;
    }
    case ACTION_REFUEL:
        sys->fluids.fuel = f_min(sys->fluids.fuel + 0.45f, 1.0f);
        it->hands.kind = ITEM_NONE;
        break;
    case ACTION_OIL_FILL:
        sys->fluids.oil = 1.0f;
        it->hands.kind = ITEM_NONE;
        break;
    default:
        break;
    }
}

void interact_update(Interact* it, struct Player* player, struct Vehicle* veh,
                     struct CarSys* sys, struct World* world, struct PhysWorld* phys,
                     Ray view_ray, b32 e_down, b32 e_pressed, f32 dt)
{
    Candidate best;
    best.t = 1e30f;
    best.action = ACTION_NONE;
    best.part = PART_COUNT;
    best.side = 0;
    best.cargo = 0;
    best.is_hold = 0;
    best.center = vec3_zero();
    best.half = vec3_zero();
    best.place_pos = vec3_zero();
    best.prompt[0] = 0;
    Handle none = {0};
    best.entity = none;

    if (player->state == PLAYER_ON_FOOT) {
        resolve_car_targets(&best, it, player, veh, sys, phys, view_ray);
        resolve_pickups(&best, it, world, view_ray);
    } else if (player->state == PLAYER_DRIVING) {
        resolve_in_car(&best, it, player, veh, sys, phys, view_ray);
    }

    b32 same_target = best.action == it->action && best.part == it->target_part
                   && best.entity.idx == it->target_entity.idx && best.side == it->target_side
                   && best.cargo == it->target_cargo;
    it->action = best.action;
    it->target_part = best.part;
    it->target_entity = best.entity;
    it->target_side = best.side;
    it->target_cargo = best.cargo;
    it->target_center = best.center;
    it->target_half = best.half;
    it->place_pos = best.place_pos;
    it->action_is_hold = best.is_hold;
    snprintf(it->prompt, sizeof(it->prompt), "%s", best.prompt);
    if (!same_target) {
        it->hold_time = 0.0f;
    }

    if (best.action == ACTION_NONE || best.action == ACTION_INFO
        || best.action == ACTION_ENTER_CAR || best.action == ACTION_EXIT_CAR) {
        it->hold_time = 0.0f;
        it->hold_progress = 0.0f;
        return;
    }

    if (best.is_hold) {
        if (e_down) {
            it->hold_time += dt;
            if (it->hold_time >= INTERACT_HOLD_TIME) {
                interact_perform(it, sys, world);
                it->hold_time = 0.0f;
            }
        } else {
            it->hold_time = 0.0f;
        }
        it->hold_progress = it->hold_time / INTERACT_HOLD_TIME;
    } else {
        it->hold_progress = 0.0f;
        if (e_pressed) {
            interact_perform(it, sys, world);
        }
    }
}

b32 interact_drop(Interact* it, struct World* world, struct PhysWorld* phys, Vec3 pos, f32 yaw)
{
    if (it->hands.kind == ITEM_NONE) {
        return 0;
    }
    Vec3 fwd = v3(sinf(yaw), 0.0f, -cosf(yaw));
    Vec3 spot = vec3_add(pos, vec3_scale(fwd, 1.1f));
    spot.y = heightfield_sample(phys->hf, spot.x, spot.z) + 0.18f;
    EntityHandle handle = world_spawn(world, ENTITY_PART_PICKUP, spot,
                                      quat_from_axis_angle(v3(0.0f, 1.0f, 0.0f), yaw), 1.0f,
                                      item_mesh(it->hands.kind), 0);
    Entity* entity = world_entity(world, handle);
    if (!entity) {
        return 0;
    }
    entity->aux_kind = (u32)it->hands.kind;
    entity->aux_value = it->hands.condition;
    it->hands.kind = ITEM_NONE;
    return 1;
}
