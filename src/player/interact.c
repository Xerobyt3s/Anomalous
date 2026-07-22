#include "player/interact.h"
#include "player/player.h"
#include "vehicle/vehicle.h"
#include "carsys/carsys.h"
#include "world/world.h"
#include "world/zone.h"
#include "world/terrain.h"
#include "physics/physics.h"
#include "physics/heightfield.h"
#include "terminal/disks.h"
#include "audio/tapes.h"
#include "core/arena.h"
#include "core/config.h"
#include "core/log.h"
#include "platform/platform.h"

#include <stdio.h>
#include <string.h>

#define HOOD_OPEN_FOR_BAY 0.8f
#define DOOR_OPEN_FOR_USE 0.6f
#define EXIT_LOOK_YAW 1.15f
#define DOOR_HINGE_X 0.80f

static InteractBox s_boxes[IBOX_COUNT] = {
    { "door",         { 0.78f, -0.02f, 0.10f },  { 0.10f, 0.26f, 0.62f } },
    { "door_panel",   { 1.45f, -0.02f, -0.18f }, { 0.48f, 0.26f, 0.42f } },
    { "handbrake",    { -0.13f, -0.13f, 0.33f }, { 0.09f, 0.10f, 0.16f } },
    { "wiper",        { -0.48f, 0.08f, -0.28f }, { 0.05f, 0.04f, 0.06f } },
    { "ignition",     { -0.22f, 0.05f, -0.28f }, { 0.07f, 0.06f, 0.08f } },
    { "deck",         { 0.12f, -0.045f, -0.295f }, { 0.09f, 0.05f, 0.055f } },
    { "disk_slot",    { 0.0f, -0.119f, 0.265f }, { 0.10f, 0.035f, 0.045f } },
    { "hood_latch",   { 0.0f, 0.04f, -1.50f },   { 0.50f, 0.10f, 0.42f } },
    { "hood_raised",  { 0.0f, 0.72f, -0.92f },   { 0.50f, 0.62f, 0.30f } },
    { "trunk_lid",    { 0.0f, 0.22f, 1.73f },    { 0.50f, 0.09f, 0.31f } },
    { "trunk_edge",   { 0.0f, 0.55f, 1.52f },    { 0.50f, 0.38f, 0.16f } },
    { "fuel",         { 0.80f, 0.10f, 1.30f },   { 0.10f, 0.10f, 0.14f } },
    { "antenna_jack", { 0.35f, 0.54f, 0.36f },   { 0.055f, 0.055f, 0.055f } },
    { "bay_jack",     { 0.32f, 0.02f, -0.75f },  { 0.055f, 0.055f, 0.055f } },
};

static char s_boxes_path[256];
static i64 s_boxes_mtime;
static f64 s_boxes_next_poll;

InteractBox* interact_box(u32 id)
{
    return &s_boxes[id];
}

static void interact_boxes_load(void)
{
    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    FileData file = platform_read_entire_file(&g_frame_arena, s_boxes_path);
    if (file.data) {
        Config cfg;
        if (config_parse(&cfg, &g_frame_arena, (const char*)file.data)) {
            for (u32 i = 0; i < IBOX_COUNT; i++) {
                char key[64];
                snprintf(key, sizeof(key), "boxes.%s", s_boxes[i].name);
                f32 vals[6];
                if (config_get_f32_list(&cfg, key, vals, 6) == 6) {
                    s_boxes[i].center = v3(vals[0], vals[1], vals[2]);
                    s_boxes[i].half = v3(vals[3], vals[4], vals[5]);
                }
            }
            log_info("interact: loaded boxes from %s", s_boxes_path);
        }
    }
    arena_temp_end(temp);
}

void interact_boxes_init(const char* path)
{
    snprintf(s_boxes_path, sizeof(s_boxes_path), "%s", path);
    s_boxes_mtime = platform_file_mtime(path);
    if (s_boxes_mtime != 0) {
        interact_boxes_load();
    }
}

void interact_boxes_poll(f64 now)
{
    if (now < s_boxes_next_poll || !s_boxes_path[0]) {
        return;
    }
    s_boxes_next_poll = now + 1.0;
    i64 mtime = platform_file_mtime(s_boxes_path);
    if (mtime != 0 && mtime != s_boxes_mtime) {
        s_boxes_mtime = mtime;
        interact_boxes_load();
    }
}

b32 interact_boxes_save(void)
{
    if (!s_boxes_path[0]) {
        return 0;
    }
    FILE* out = (FILE*)platform_fopen(s_boxes_path, "wb");
    if (!out) {
        log_warn("interact: could not write %s", s_boxes_path);
        return 0;
    }
    fprintf(out, "[boxes]\n");
    for (u32 i = 0; i < IBOX_COUNT; i++) {
        const InteractBox* b = &s_boxes[i];
        fprintf(out, "%s = %.3f %.3f %.3f %.3f %.3f %.3f\n", b->name,
                (f64)b->center.x, (f64)b->center.y, (f64)b->center.z,
                (f64)b->half.x, (f64)b->half.y, (f64)b->half.z);
    }
    fclose(out);
    s_boxes_mtime = platform_file_mtime(s_boxes_path);
    log_info("interact: saved boxes to %s", s_boxes_path);
    return 1;
}

void interact_init(Interact* it)
{
    Interact zero = {0};
    *it = zero;
    it->cable_drag = -1;
}

typedef struct Candidate {
    f32 t;
    InteractAction action;
    PartKind part;
    Handle entity;
    i32 side;
    u32 cargo;
    i32 cable;
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

static b32 consider_cable_root(Candidate* best, const Interact* it, CarSys* sys, CableKind kind,
                               f32 t, Vec3 center, Vec3 half)
{
    Cable* cable = &sys->cables[kind];
    const char* name = kind == CABLE_COAX ? "coax" : "bus";
    char prompt[96];
    b32 taken = 0;
    if (cable->state == CABLE_STOWED) {
        if (it->cable_drag < 0 && it->hands.kind == ITEM_NONE) {
            snprintf(prompt, sizeof(prompt), "[E] grab %s cable", name);
            taken = candidate_consider(best, t, ACTION_CABLE_GRAB, center, half, 0, prompt);
        } else {
            snprintf(prompt, sizeof(prompt), "%s port", name);
            taken = candidate_consider(best, t, ACTION_INFO, center, half, 0, prompt);
        }
    } else if (cable->state == CABLE_PLUGGED) {
        snprintf(prompt, sizeof(prompt), "[E] unplug %s cable", name);
        taken = candidate_consider(best, t, ACTION_CABLE_UNPLUG, center, half, 0, prompt);
    } else {
        snprintf(prompt, sizeof(prompt), "%s cable is out", name);
        taken = candidate_consider(best, t, ACTION_INFO, center, half, 0, prompt);
    }
    if (taken) {
        best->cable = (i32)kind;
    }
    return taken;
}

static b32 consider_cable_jack(Candidate* best, const Interact* it, CarSys* sys, CableKind kind,
                               f32 t, Vec3 center, Vec3 half, const char* target_name)
{
    Cable* cable = &sys->cables[kind];
    const char* name = kind == CABLE_COAX ? "coax" : "bus";
    char prompt[96];
    b32 taken = 0;
    if (cable->state == CABLE_PLUGGED) {
        snprintf(prompt, sizeof(prompt), "[E] unplug %s cable", name);
        taken = candidate_consider(best, t, ACTION_CABLE_UNPLUG, center, half, 0, prompt);
    } else if (it->cable_drag == (i32)kind) {
        snprintf(prompt, sizeof(prompt), "[E] connect %s to %s", name, target_name);
        taken = candidate_consider(best, t, ACTION_CABLE_PLUG, center, half, 0, prompt);
    } else {
        snprintf(prompt, sizeof(prompt), "%s jack", name);
        taken = candidate_consider(best, t, ACTION_INFO, center, half, 0, prompt);
    }
    if (taken) {
        best->cable = (i32)kind;
    }
    return taken;
}

static void consider_disk_slot(Candidate* best, const Interact* it, CarSys* sys, f32 t,
                               Vec3 center, Vec3 half)
{
    char prompt[96];
    if (it->hands.kind == ITEM_FLOPPY && sys->floppy_disk < 0) {
        snprintf(prompt, sizeof(prompt), "[E] insert %s", disk_label(it->hands.aux));
        candidate_consider(best, t, ACTION_DISK_INSERT, center, half, 0, prompt);
    } else if (sys->floppy_disk >= 0 && it->hands.kind == ITEM_NONE && it->cable_drag < 0) {
        snprintf(prompt, sizeof(prompt), "[E] eject %s", disk_label(sys->floppy_disk));
        candidate_consider(best, t, ACTION_DISK_EJECT, center, half, 0, prompt);
    } else if (sys->floppy_disk < 0) {
        candidate_consider(best, t, ACTION_INFO, center, half, 0, "drive b: slot empty");
    } else {
        candidate_consider(best, t, ACTION_INFO, center, half, 0, "drive b: disk loaded");
    }
}

static void consider_deck_slot(Candidate* best, const Interact* it, CarSys* sys, f32 t,
                               Vec3 center, Vec3 half)
{
    char prompt[96];
    if (it->hands.kind == ITEM_CASSETTE && sys->tape_inserted < 0) {
        snprintf(prompt, sizeof(prompt), "[E] insert tape \"%s\"", tape_label(it->hands.aux));
        candidate_consider(best, t, ACTION_TAPE_INSERT, center, half, 0, prompt);
    } else if (sys->tape_inserted >= 0 && it->hands.kind == ITEM_NONE && it->cable_drag < 0) {
        snprintf(prompt, sizeof(prompt), "[E] %s | hold [E] eject tape",
                 sys->deck_play ? "stop tape" : "play tape");
        candidate_consider(best, t, ACTION_TAPE_EJECT, center, half, 1, prompt);
    } else if (sys->tape_inserted < 0) {
        candidate_consider(best, t, ACTION_INFO, center, half, 0, "tape deck: empty");
    } else {
        snprintf(prompt, sizeof(prompt), "tape deck: \"%s\"", tape_label(sys->tape_inserted));
        candidate_consider(best, t, ACTION_INFO, center, half, 0, prompt);
    }
}

static void resolve_doors(Candidate* best, const struct Player* player, Vehicle* veh, CarSys* sys,
                          PhysWorld* phys, Ray local, b32 on_foot)
{
    Vec3 com = veh->cfg.com_offset;
    const InteractBox* door_box = &s_boxes[IBOX_DOOR];
    Vec3 door_half = door_box->half;
    f32 t;
    for (i32 side = 0; side < 2; side++) {
        f32 sign = side == 0 ? -1.0f : 1.0f;
        b32 open = sys->door_open[side] > DOOR_OPEN_FOR_USE;
        Vec3 center = vec3_sub(v3(sign * door_box->center.x, door_box->center.y,
                                  door_box->center.z), com);
        if (ray_vs_local_box(local, center, door_half, &t)) {
            b32 taken = 0;
            if (on_foot) {
                if (!open) {
                    taken = candidate_consider(best, t + 0.05f, ACTION_OPEN_DOOR, center,
                                               door_half, 0, "[E] open door");
                } else if (side == 1 && sys->parts[PART_COMPUTER].installed) {
                    taken = 0;
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
            const InteractBox* panel_box = &s_boxes[IBOX_DOOR_PANEL];
            Vec3 panel_center = vec3_sub(v3(sign * panel_box->center.x, panel_box->center.y,
                                            panel_box->center.z), com);
            Vec3 panel_half = panel_box->half;
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

    Vec3 lever_center = vec3_sub(s_boxes[IBOX_HANDBRAKE].center, com);
    Vec3 lever_half = s_boxes[IBOX_HANDBRAKE].half;
    if (ray_vs_local_box(local, lever_center, lever_half, &t)) {
        candidate_consider(best, t, ACTION_HANDBRAKE, lever_center, lever_half, 0,
                           sys->handbrake_latched ? "[E] release handbrake" : "[E] set handbrake");
    }

    static const char* wiper_modes[3] = { "off", "interval", "full" };
    Vec3 stalk_center = vec3_sub(s_boxes[IBOX_WIPER].center, com);
    Vec3 stalk_half = s_boxes[IBOX_WIPER].half;
    if (ray_vs_local_box(local, stalk_center, stalk_half, &t)) {
        char wprompt[96];
        snprintf(wprompt, sizeof(wprompt), "[E] wipers: %s", wiper_modes[sys->wiper_mode % 3]);
        candidate_consider(best, t, ACTION_WIPERS, stalk_center, stalk_half, 0, wprompt);
    }

    Vec3 ignition_center = vec3_sub(s_boxes[IBOX_IGNITION].center, com);
    Vec3 ignition_half = s_boxes[IBOX_IGNITION].half;
    if (ray_vs_local_box(local, ignition_center, ignition_half, &t)) {
        if (!sys->key_inserted) {
            if (it->has_key) {
                candidate_consider(best, t, ACTION_INSERT_KEY, ignition_center, ignition_half, 0,
                                   "[E] insert key");
            } else {
                candidate_consider(best, t, ACTION_INFO, ignition_center, ignition_half, 0,
                                   "ignition — no key");
            }
        } else if (sys->engine_on) {
            candidate_consider(best, t, ACTION_ENGINE_OFF, ignition_center, ignition_half, 0,
                               "[E] switch off");
        } else {
            candidate_consider(best, t, ACTION_CRANK, ignition_center, ignition_half, 0,
                               "hold [E] turn key | [G] take key");
        }
    }

    const PartDef* term_def = part_def(PART_COMPUTER);
    Vec3 term_center = vec3_sub(term_def->socket_pos, com);
    if (ray_vs_local_box(local, term_center, term_def->socket_half, &t)) {
        if (sys->parts[PART_COMPUTER].installed) {
            if (sys->computer_on) {
                candidate_consider(best, t, ACTION_TERMINAL_USE, term_center,
                                   term_def->socket_half, 0, "[E] use terminal");
            } else {
                candidate_consider(best, t, ACTION_COMPUTER, term_center, term_def->socket_half, 0,
                                   "[E] power on terminal");
            }
        } else if (it->hands.kind == ITEM_COMPUTER) {
            if (candidate_consider(best, t, ACTION_INSTALL_PART, term_center,
                                   term_def->socket_half, 1, "hold [E] install terminal")) {
                best->part = PART_COMPUTER;
            }
        }
    }
    if (sys->parts[PART_COMPUTER].installed) {
        Quat rest = part_computer_rest_rot();
        Vec3 slot_center = vec3_add(term_center,
                                    quat_rotate_vec3(rest, s_boxes[IBOX_DISK_SLOT].center));
        if (ray_vs_local_box(local, slot_center, s_boxes[IBOX_DISK_SLOT].half, &t)) {
            consider_disk_slot(best, it, sys, t - 0.30f, slot_center,
                               s_boxes[IBOX_DISK_SLOT].half);
        }
    }

    Vec3 deck_center = vec3_sub(s_boxes[IBOX_DECK].center, com);
    if (ray_vs_local_box(local, deck_center, s_boxes[IBOX_DECK].half, &t)) {
        consider_deck_slot(best, it, sys, t - 0.15f, deck_center, s_boxes[IBOX_DECK].half);
    }

    for (i32 side = 0; side < 2; side++) {
        f32 sign = side == 0 ? -1.0f : 1.0f;
        if (sys->door_open[side] < DOOR_OPEN_FOR_USE) {
            continue;
        }
        if (player->look_yaw * sign > EXIT_LOOK_YAW) {
            if (side == 1 && sys->parts[PART_COMPUTER].installed) {
                continue;
            }
            if (player_can_exit(player, phys, veh)) {
                Vec3 center = vec3_sub(v3(sign * 0.95f, 0.0f, 0.10f), com);
                if (candidate_consider(best, 0.05f, ACTION_EXIT_CAR, center,
                                       v3(0.05f, 0.25f, 0.55f), 0, "[E] get out")) {
                    best->side = side;
                }
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
                if ((PartKind)k == PART_COMPUTER && sys->computer_on) {
                    taken = candidate_consider(best, t, ACTION_TERMINAL_USE, center,
                                               def->socket_half, 1,
                                               "[E] use terminal | hold [E] take");
                } else if ((PartKind)k == PART_COMPUTER) {
                    snprintf(prompt, sizeof(prompt), "tap [E] power | hold [E] take terminal (%.0f%%)",
                             (f64)(slot->condition * 100.0f));
                    taken = candidate_consider(best, t, ACTION_REMOVE_PART, center,
                                               def->socket_half, 1, prompt);
                } else {
                    snprintf(prompt, sizeof(prompt), "hold [E] take %s (%.0f%%)",
                             def->name, (f64)(slot->condition * 100.0f));
                    taken = candidate_consider(best, t, ACTION_REMOVE_PART, center,
                                               def->socket_half, 1, prompt);
                }
            } else {
                snprintf(prompt, sizeof(prompt), "%s %.0f%%",
                         def->name, (f64)(slot->condition * 100.0f));
                taken = candidate_consider(best, t, ACTION_INFO, center, def->socket_half, 0,
                                           prompt);
            }
        } else if (it->hands.kind != ITEM_NONE
                   && ((PartKind)k == PART_ANTENNA
                       ? antenna_variant_for_item(it->hands.kind) >= 0
                       : item_for_part((PartKind)k) == it->hands.kind)) {
            snprintf(prompt, sizeof(prompt), "hold [E] install %s (%.0f%%)",
                     item_name(it->hands.kind), (f64)(it->hands.condition * 100.0f));
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

    if (sys->parts[PART_COMPUTER].installed) {
        Quat rest = part_computer_rest_rot();
        Vec3 term_base = vec3_sub(part_def(PART_COMPUTER)->socket_pos, com);
        Vec3 ports[2] = { CONNECTOR_COAX_LOCAL, CONNECTOR_BUS_LOCAL };
        Vec3 port_half = v3(0.05f, 0.05f, 0.06f);
        for (u32 pk = 0; pk < 2; pk++) {
            Vec3 center = vec3_add(term_base, quat_rotate_vec3(rest, ports[pk]));
            if (ray_vs_local_box(local, center, port_half, &t)) {
                consider_cable_root(best, it, sys, (CableKind)pk, t - 0.45f, center, port_half);
            }
        }
        Vec3 slot_center = vec3_add(term_base,
                                    quat_rotate_vec3(rest, s_boxes[IBOX_DISK_SLOT].center));
        if (ray_vs_local_box(local, slot_center, s_boxes[IBOX_DISK_SLOT].half, &t)) {
            consider_disk_slot(best, it, sys, t - 0.30f, slot_center,
                               s_boxes[IBOX_DISK_SLOT].half);
        }
    }

    {
        Vec3 jack_half = s_boxes[IBOX_ANTENNA_JACK].half;
        Vec3 center = vec3_sub(s_boxes[IBOX_ANTENNA_JACK].center, com);
        if (ray_vs_local_box(local, center, jack_half, &t)) {
            if (!sys->parts[PART_ANTENNA].installed && it->cable_drag == (i32)CABLE_COAX) {
                candidate_consider(best, t - 0.20f, ACTION_INFO, center, jack_half, 0,
                                   "no antenna mounted");
            } else if (sys->parts[PART_ANTENNA].installed
                       || sys->cables[CABLE_COAX].state == CABLE_PLUGGED) {
                consider_cable_jack(best, it, sys, CABLE_COAX, t - 0.20f, center, jack_half,
                                    "antenna");
            }
        }
        if (sys->hood_open >= HOOD_OPEN_FOR_BAY && sys->bus_target == BUS_TARGET_CAR) {
            center = vec3_sub(s_boxes[IBOX_BAY_JACK].center, com);
            if (ray_vs_local_box(local, center, s_boxes[IBOX_BAY_JACK].half, &t)) {
                consider_cable_jack(best, it, sys, CABLE_BUS, t - 0.20f, center,
                                    s_boxes[IBOX_BAY_JACK].half, "vehicle bus");
            }
        }
    }

    resolve_doors(best, player, veh, sys, phys, local, 1);

    if (sys->hood_open < 0.5f) {
        Vec3 latch_half = s_boxes[IBOX_HOOD_LATCH].half;
        Vec3 latch_center = vec3_sub(s_boxes[IBOX_HOOD_LATCH].center, com);
        if (ray_vs_local_box(local, latch_center, latch_half, &t)) {
            candidate_consider(best, t + 0.15f, ACTION_TOGGLE_HOOD, latch_center, latch_half, 0,
                               "[E] open hood");
        }
    } else {
        Vec3 raised_half = s_boxes[IBOX_HOOD_RAISED].half;
        Vec3 raised_center = vec3_sub(s_boxes[IBOX_HOOD_RAISED].center, com);
        if (ray_vs_local_box(local, raised_center, raised_half, &t)) {
            candidate_consider(best, t + 2.0f, ACTION_TOGGLE_HOOD, raised_center, raised_half, 0,
                               "[E] close hood");
        }
    }

    if (sys->trunk_open < 0.5f) {
        Vec3 lid_half = s_boxes[IBOX_TRUNK_LID].half;
        Vec3 lid_center = vec3_sub(s_boxes[IBOX_TRUNK_LID].center, com);
        if (ray_vs_local_box(local, lid_center, lid_half, &t)) {
            candidate_consider(best, t, ACTION_TOGGLE_TRUNK, lid_center, lid_half, 0,
                               "[E] open trunk");
        }
    } else {
        Vec3 edge_half = s_boxes[IBOX_TRUNK_EDGE].half;
        Vec3 edge_center = vec3_sub(s_boxes[IBOX_TRUNK_EDGE].center, com);
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
                if (c->item.kind == ITEM_FLOPPY) {
                    snprintf(prompt, sizeof(prompt), "[E] take floppy (%s)",
                             disk_label(c->item.aux));
                } else {
                    snprintf(prompt, sizeof(prompt), "[E] take %s (%.0f%%)",
                             item_name(c->item.kind), (f64)(c->item.condition * 100.0f));
                }
                if (candidate_consider(best, t, ACTION_TAKE_CARGO, center, half, 0, prompt)) {
                    best->cargo = i;
                }
            }
        }
    }

    Vec3 filler_half = s_boxes[IBOX_FUEL].half;
    Vec3 filler_center = vec3_sub(s_boxes[IBOX_FUEL].center, com);
    if (ray_vs_local_box(local, filler_center, filler_half, &t)) {
        if (it->hands.kind == ITEM_JERRYCAN) {
            if (sys->fuel_cap_open) {
                candidate_consider(best, t, ACTION_REFUEL, filler_center, filler_half, 1,
                                   "hold [E] refuel");
            } else {
                candidate_consider(best, t, ACTION_INFO, filler_center, filler_half, 0,
                                   "fuel cap is closed");
            }
        } else {
            candidate_consider(best, t, ACTION_FUEL_CAP, filler_center, filler_half, 0,
                               sys->fuel_cap_open ? "[E] close fuel cap" : "[E] open fuel cap");
        }
    }
}

static void resolve_pickups(Candidate* best, const Interact* it, CarSys* sys, World* world,
                            Ray view_ray)
{
    char prompt[96];
    for (u32 idx = 0; idx < world->entities.capacity; idx++) {
        Entity* entity = pool_at(&world->entities, idx);
        if (!entity || entity->kind != ENTITY_PART_PICKUP) {
            continue;
        }
        b32 is_computer = (ItemKind)entity->aux_kind == ITEM_COMPUTER;
        b32 is_camera = (ItemKind)entity->aux_kind == ITEM_CAMERA;
        b32 is_reel = (ItemKind)entity->aux_kind == ITEM_REEL;
        b32 coax_business = is_camera
                            && (it->cable_drag == (i32)CABLE_COAX
                                || (sys->coax_target == COAX_TARGET_CAMERA
                                    && sys->cables[CABLE_COAX].state == CABLE_PLUGGED));
        if ((it->hands.kind != ITEM_NONE || it->cable_drag >= 0)
            && (ItemKind)entity->aux_kind != ITEM_KEY
            && !(is_computer && (sys->computer_on || it->hands.kind == ITEM_FLOPPY))
            && !(is_reel && it->cable_drag >= 0)
            && !coax_business) {
            continue;
        }
        if (is_computer) {
            Vec3 ports[2] = { CONNECTOR_COAX_LOCAL, CONNECTOR_BUS_LOCAL };
            for (u32 pk = 0; pk < 2; pk++) {
                Sphere port;
                port.center = vec3_add(entity->pos, quat_rotate_vec3(entity->rot, ports[pk]));
                port.radius = 0.07f;
                f32 pt;
                if (ray_vs_sphere(view_ray, port, INTERACT_RANGE, &pt)) {
                    consider_cable_root(best, it, sys, (CableKind)pk, pt - 0.50f,
                                        vec3_zero(), vec3_zero());
                }
            }
            Sphere slot;
            slot.center = vec3_add(entity->pos,
                                   quat_rotate_vec3(entity->rot, s_boxes[IBOX_DISK_SLOT].center));
            slot.radius = 0.09f;
            f32 st;
            if (ray_vs_sphere(view_ray, slot, INTERACT_RANGE, &st)) {
                consider_disk_slot(best, it, sys, st - 0.50f, vec3_zero(), vec3_zero());
            }
        }
        if (is_camera) {
            if (sys->coax_target == COAX_TARGET_CAMERA
                && sys->cables[CABLE_COAX].state == CABLE_PLUGGED) {
                Sphere plug_s;
                plug_s.center = vec3_add(entity->pos,
                                         quat_rotate_vec3(entity->rot,
                                                          v3(-0.13f, 0.01f, 0.016f)));
                plug_s.radius = 0.13f;
                f32 pt;
                if (ray_vs_sphere(view_ray, plug_s, INTERACT_RANGE, &pt)) {
                    if (candidate_consider(best, pt - 0.60f, ACTION_CABLE_UNPLUG, vec3_zero(),
                                           vec3_zero(), 0, "[E] unplug coax from camera")) {
                        best->cable = (i32)CABLE_COAX;
                    }
                }
            } else if (it->cable_drag == (i32)CABLE_COAX) {
                Sphere cam_s;
                cam_s.center = vec3_add(entity->pos, v3(0.0f, 0.06f, 0.0f));
                cam_s.radius = 0.25f;
                f32 ct;
                if (ray_vs_sphere(view_ray, cam_s, INTERACT_RANGE, &ct)) {
                    if (candidate_consider(best, ct - 0.30f, ACTION_CABLE_PLUG_CAMERA,
                                           vec3_zero(), vec3_zero(), 0,
                                           "[E] connect coax to camera")) {
                        best->cable = (i32)CABLE_COAX;
                    }
                }
                continue;
            }
        }
        if (is_reel && it->cable_drag >= 0) {
            Cable* dragged = &sys->cables[it->cable_drag];
            Sphere reel_s;
            reel_s.center = vec3_add(entity->pos, v3(0.0f, 0.10f, 0.0f));
            reel_s.radius = 0.30f;
            f32 rt;
            if (ray_vs_sphere(view_ray, reel_s, INTERACT_RANGE, &rt)) {
                b32 other_routed = sys->cables[1 - it->cable_drag].via_reel;
                if (dragged->via_reel || other_routed) {
                    candidate_consider(best, rt - 0.30f, ACTION_INFO, vec3_zero(), vec3_zero(),
                                       0, "reel already in use");
                } else {
                    snprintf(prompt, sizeof(prompt), "[E] connect %s to reel",
                             it->cable_drag == (i32)CABLE_COAX ? "coax" : "bus");
                    if (candidate_consider(best, rt - 0.30f, ACTION_CABLE_ROUTE_REEL,
                                           vec3_zero(), vec3_zero(), 0, prompt)) {
                        best->cable = it->cable_drag;
                    }
                }
            }
            continue;
        }
        ItemKind pick_kind = (ItemKind)entity->aux_kind;
        Sphere sphere;
        sphere.center = vec3_add(vec3_add(entity->pos,
                                          quat_rotate_vec3(entity->rot,
                                                           item_mesh_center(pick_kind))),
                                 v3(0.0f, 0.22f, 0.0f));
        sphere.radius = antenna_variant_for_item(pick_kind) >= 0 ? 0.60f
                        : 0.45f * entity->scale;
        f32 t;
        if (!ray_vs_sphere(view_ray, sphere, INTERACT_RANGE, &t)) {
            continue;
        }
        if (is_computer) {
            if (sys->computer_on) {
                b32 can_take = it->hands.kind == ITEM_NONE && it->cable_drag < 0;
                if (candidate_consider(best, t, ACTION_TERMINAL_USE, vec3_zero(), vec3_zero(),
                                       can_take,
                                       can_take ? "[E] use terminal | hold [E] take"
                                                : "[E] use terminal")) {
                    best->entity.idx = idx;
                    best->entity.gen = world->entities.gens[idx];
                }
            } else {
                snprintf(prompt, sizeof(prompt), "[E] power on | hold [E] take terminal (%.0f%%)",
                         (f64)(entity->aux_value * 100.0f));
                if (candidate_consider(best, t, ACTION_PICKUP, vec3_zero(), vec3_zero(), 1,
                                       prompt)) {
                    best->entity.idx = idx;
                    best->entity.gen = world->entities.gens[idx];
                }
            }
            continue;
        }
        if (pick_kind == ITEM_FLOPPY) {
            snprintf(prompt, sizeof(prompt), "[E] take floppy (%s)",
                     disk_label((i32)entity->aux_data));
        } else if (pick_kind == ITEM_CASSETTE) {
            snprintf(prompt, sizeof(prompt), "[E] take cassette (%s)",
                     tape_label((i32)entity->aux_data));
        } else {
            snprintf(prompt, sizeof(prompt), "[E] take %s (%.0f%%)",
                     item_name(pick_kind), (f64)(entity->aux_value * 100.0f));
        }
        if (candidate_consider(best, t, ACTION_PICKUP, vec3_zero(), vec3_zero(), 0, prompt)) {
            best->entity.idx = idx;
            best->entity.gen = world->entities.gens[idx];
        }
    }
}

static void resolve_tower_port(Candidate* best, const Interact* it, CarSys* sys, Ray view_ray)
{
    if (!it->tower_present) {
        return;
    }
    Sphere port;
    port.center = it->tower_port;
    port.radius = 0.55f;
    f32 t;
    if (!ray_vs_sphere(view_ray, port, INTERACT_RANGE, &t)) {
        return;
    }
    Cable* bus = &sys->cables[CABLE_BUS];
    if (sys->bus_target == BUS_TARGET_TOWER && bus->state == CABLE_PLUGGED) {
        if (candidate_consider(best, t - 0.30f, ACTION_CABLE_UNPLUG, vec3_zero(), vec3_zero(),
                               0, "[E] unplug bus from relay port")) {
            best->cable = (i32)CABLE_BUS;
        }
    } else if (it->cable_drag == (i32)CABLE_BUS) {
        if (candidate_consider(best, t - 0.30f, ACTION_CABLE_PLUG_TOWER, vec3_zero(), vec3_zero(),
                               0, "[E] connect bus to relay port")) {
            best->cable = (i32)CABLE_BUS;
        }
    } else if (it->cable_drag < 0 && it->hands.kind == ITEM_NONE) {
        candidate_consider(best, t - 0.30f, ACTION_INFO, vec3_zero(), vec3_zero(), 0,
                           "relay service port");
    }
}

static void interact_perform(Interact* it, CarSys* sys, World* world, PhysWorld* phys)
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
    case ACTION_INSERT_KEY:
        sys->key_inserted = 1;
        it->has_key = 0;
        break;
    case ACTION_ENGINE_OFF:
        carsys_stop_engine(sys);
        break;
    case ACTION_FUEL_CAP:
        sys->fuel_cap_open = !sys->fuel_cap_open;
        break;
    case ACTION_COMPUTER:
        sys->computer_on = !sys->computer_on;
        break;
    case ACTION_REMOVE_PART: {
        PartSlot* slot = &sys->parts[it->target_part];
        it->hands.kind = it->target_part == PART_ANTENNA
                         ? antenna_item_for_variant(slot->variant)
                         : item_for_part(it->target_part);
        it->hands.condition = slot->condition;
        it->hands.aux = 0;
        slot->installed = 0;
        if (it->target_part == PART_ANTENNA) {
            cable_reset(&sys->cables[CABLE_COAX]);
            if (it->cable_drag == (i32)CABLE_COAX) {
                it->cable_drag = -1;
            }
        }
        break;
    }
    case ACTION_INSTALL_PART: {
        PartSlot* slot = &sys->parts[it->target_part];
        slot->installed = 1;
        slot->condition = it->hands.condition;
        if (it->target_part == PART_ANTENNA) {
            slot->variant = antenna_variant_for_item(it->hands.kind);
        }
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
    case ACTION_TERMINAL_USE:
    case ACTION_PICKUP: {
        if (it->hands.kind != ITEM_NONE && it->action == ACTION_TERMINAL_USE) {
            break;
        }
        Entity* entity = world_entity(world, it->target_entity);
        if (entity) {
            if ((ItemKind)entity->aux_kind == ITEM_KEY) {
                it->has_key = 1;
            } else {
                it->hands.kind = (ItemKind)entity->aux_kind;
                it->hands.condition = entity->aux_value;
                it->hands.aux = (i32)entity->aux_data;
            }
            if (handle_valid(entity->body)) {
                phys_body_destroy(phys, entity->body);
            }
            world_despawn(world, it->target_entity);
        } else if (it->action == ACTION_TERMINAL_USE
                   && sys->parts[PART_COMPUTER].installed) {
            PartSlot* slot = &sys->parts[PART_COMPUTER];
            it->hands.kind = ITEM_COMPUTER;
            it->hands.condition = slot->condition;
            it->hands.aux = 0;
            slot->installed = 0;
        }
        break;
    }
    case ACTION_CABLE_GRAB:
        sys->cables[it->target_cable].state = CABLE_DRAGGED;
        it->cable_drag = it->target_cable;
        break;
    case ACTION_CABLE_PLUG:
        sys->cables[it->target_cable].state = CABLE_PLUGGED;
        if (it->target_cable == (i32)CABLE_COAX) {
            sys->coax_target = COAX_TARGET_ANTENNA;
        }
        if (it->target_cable == (i32)CABLE_BUS) {
            sys->bus_target = BUS_TARGET_CAR;
        }
        it->cable_drag = -1;
        break;
    case ACTION_CABLE_PLUG_CAMERA:
        sys->cables[CABLE_COAX].state = CABLE_PLUGGED;
        sys->coax_target = COAX_TARGET_CAMERA;
        it->cable_drag = -1;
        break;
    case ACTION_CABLE_PLUG_TOWER:
        sys->cables[CABLE_BUS].state = CABLE_PLUGGED;
        sys->bus_target = BUS_TARGET_TOWER;
        it->cable_drag = -1;
        break;
    case ACTION_CABLE_ROUTE_REEL:
        sys->cables[it->target_cable].via_reel = 1;
        break;
    case ACTION_CABLE_UNPLUG:
        cable_reset(&sys->cables[it->target_cable]);
        if (it->target_cable == (i32)CABLE_COAX) {
            sys->coax_target = COAX_TARGET_ANTENNA;
        }
        if (it->target_cable == (i32)CABLE_BUS) {
            sys->bus_target = BUS_TARGET_CAR;
        }
        if (it->cable_drag == it->target_cable) {
            it->cable_drag = -1;
        }
        break;
    case ACTION_DISK_INSERT:
        sys->floppy_disk = it->hands.aux;
        sys->floppy_cond = it->hands.condition;
        it->hands.kind = ITEM_NONE;
        break;
    case ACTION_DISK_EJECT:
        it->hands.kind = ITEM_FLOPPY;
        it->hands.aux = sys->floppy_disk;
        it->hands.condition = sys->floppy_cond;
        sys->floppy_disk = -1;
        break;
    case ACTION_WIPERS:
        sys->wiper_mode = (sys->wiper_mode + 1) % 3;
        break;
    case ACTION_TAPE_INSERT:
        sys->tape_inserted = it->hands.aux;
        sys->tape_cond = it->hands.condition;
        it->hands.kind = ITEM_NONE;
        break;
    case ACTION_TAPE_EJECT:
        it->hands.kind = ITEM_CASSETTE;
        it->hands.aux = sys->tape_inserted;
        it->hands.condition = sys->tape_cond;
        sys->tape_inserted = -1;
        sys->deck_play = 0;
        break;
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
    best.cable = -1;
    best.is_hold = 0;
    best.center = vec3_zero();
    best.half = vec3_zero();
    best.place_pos = vec3_zero();
    best.prompt[0] = 0;
    Handle none = {0};
    best.entity = none;

    if (player->state == PLAYER_ON_FOOT) {
        resolve_car_targets(&best, it, player, veh, sys, phys, view_ray);
        resolve_pickups(&best, it, sys, world, view_ray);
        resolve_tower_port(&best, it, sys, view_ray);
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
    it->target_cable = best.cable;
    it->target_center = best.center;
    it->target_half = best.half;
    it->place_pos = best.place_pos;
    it->action_is_hold = best.is_hold;
    snprintf(it->prompt, sizeof(it->prompt), "%s", best.prompt);
    if (!same_target) {
        it->hold_time = 0.0f;
        it->press_latch = 0;
    }

    if (best.action == ACTION_CRANK) {
        if (e_pressed) {
            it->crank_latch = 1;
        }
        if (!e_down) {
            it->crank_latch = 0;
        }
    } else {
        it->crank_latch = 0;
    }
    sys->crank_request = it->crank_latch;

    if (e_pressed) {
        it->press_latch = 1;
    }

    if (best.action == ACTION_TERMINAL_USE) {
        if (best.is_hold) {
            if (e_down) {
                if (it->press_latch) {
                    it->hold_time += dt;
                    if (it->hold_time >= INTERACT_HOLD_TIME) {
                        interact_perform(it, sys, world, phys);
                        it->hold_time = 0.0f;
                        it->press_latch = 0;
                    }
                }
            } else {
                if (it->press_latch && it->hold_time > 0.0f) {
                    it->use_terminal_request = 1;
                }
                it->hold_time = 0.0f;
                it->press_latch = 0;
            }
            it->hold_progress = it->hold_time / INTERACT_HOLD_TIME;
        } else {
            it->hold_time = 0.0f;
            it->hold_progress = 0.0f;
            if (e_pressed) {
                it->use_terminal_request = 1;
                it->press_latch = 0;
            }
        }
        return;
    }

    if (best.action == ACTION_TAPE_EJECT && best.is_hold) {
        if (!e_down) {
            if (it->press_latch && it->hold_time > 0.0f) {
                sys->deck_play = !sys->deck_play;
            }
            it->press_latch = 0;
            it->hold_time = 0.0f;
        } else if (it->press_latch) {
            it->hold_time += dt;
            if (it->hold_time >= INTERACT_HOLD_TIME) {
                interact_perform(it, sys, world, phys);
                it->hold_time = 0.0f;
                it->press_latch = 0;
            }
        }
        it->hold_progress = it->hold_time / INTERACT_HOLD_TIME;
        return;
    }

    if (best.action == ACTION_NONE || best.action == ACTION_INFO
        || best.action == ACTION_ENTER_CAR || best.action == ACTION_EXIT_CAR
        || best.action == ACTION_CRANK) {
        if (e_pressed && best.action == ACTION_NONE && it->hands.kind == ITEM_COMPUTER
            && sys->computer_on) {
            it->use_terminal_request = 1;
            it->press_latch = 0;
        }
        it->hold_time = 0.0f;
        it->hold_progress = 0.0f;
        return;
    }

    if (best.is_hold) {
        if (e_down) {
            if (it->press_latch) {
                it->hold_time += dt;
                if (it->hold_time >= INTERACT_HOLD_TIME) {
                    interact_perform(it, sys, world, phys);
                    it->hold_time = 0.0f;
                    it->press_latch = 0;
                }
            }
        } else {
            if (it->press_latch && it->hold_time > 0.0f && best.action == ACTION_PICKUP) {
                Entity* entity = world_entity(world, best.entity);
                if (entity && (ItemKind)entity->aux_kind == ITEM_COMPUTER) {
                    sys->computer_on = 1;
                }
            }
            if (it->press_latch && it->hold_time > 0.0f && best.action == ACTION_REMOVE_PART
                && best.part == PART_COMPUTER) {
                sys->computer_on = !sys->computer_on;
            }
            it->hold_time = 0.0f;
            it->press_latch = 0;
        }
        it->hold_progress = it->hold_time / INTERACT_HOLD_TIME;
    } else {
        it->hold_progress = 0.0f;
        if (e_pressed && it->press_latch) {
            interact_perform(it, sys, world, phys);
            it->press_latch = 0;
        }
    }
}

Handle interact_spawn_pickup(struct World* world, struct PhysWorld* phys, Item item,
                             Vec3 pos, f32 yaw, Vec3 vel)
{
    Quat rot = quat_from_axis_angle(v3(0.0f, 1.0f, 0.0f), yaw);
    Quat mesh_rot = quat_mul(rot, item_cargo_rot(item.kind));
    Vec3 mesh_pos = vec3_sub(pos, quat_rotate_vec3(mesh_rot, item_mesh_center(item.kind)));
    EntityHandle handle = world_spawn(world, ENTITY_PART_PICKUP, mesh_pos, mesh_rot, 1.0f,
                                      item_mesh(item.kind), 0);
    Entity* entity = world_entity(world, handle);
    if (!entity) {
        return handle;
    }
    entity->aux_kind = (u32)item.kind;
    entity->aux_value = item.condition;
    entity->aux_data = (u32)item.aux;
    if (item.kind != ITEM_KEY) {
        entity->body = phys_body_create_box(phys, pos, rot, item_cargo_half(item.kind),
                                            f_max(item_mass(item.kind), 1.0f));
        RigidBody* body = phys_body(phys, entity->body);
        if (body) {
            body->vel = vel;
        }
    }
    return handle;
}

void interact_spawn_zone_pickups(struct World* world, struct PhysWorld* phys,
                                 const struct Terrain* terrain,
                                 const struct ZonePickups* pickups)
{
    for (u32 i = 0; i < pickups->count; i++) {
        const ZonePickup* p = &pickups->items[i];
        ItemKind kind = item_from_id(p->item);
        if (kind == ITEM_NONE) {
            log_warn("zone: unknown pickup item: %s", p->item);
            continue;
        }
        f32 ground = heightfield_sample(&terrain->hf, p->x, p->z);
        Ray down;
        down.origin = v3(p->x, ground + 1.6f, p->z);
        down.dir = v3(0.0f, -1.0f, 0.0f);
        PhysRayHit hit;
        if (phys_raycast(phys, down, 8.0f, &hit)) {
            ground = hit.point.y;
        }
        Vec3 pos = v3(p->x, ground + item_cargo_half(kind).y + 0.10f, p->z);
        Item item;
        item.kind = kind;
        item.condition = p->condition;
        item.aux = p->aux;
        interact_spawn_pickup(world, phys, item, pos, p->yaw_deg * DEG_TO_RAD, vec3_zero());
    }
    log_info("zone: spawned %u pickups", pickups->count);
}

b32 interact_drop(Interact* it, struct World* world, struct PhysWorld* phys, Vec3 origin,
                  Vec3 dir, f32 power)
{
    if (it->hands.kind == ITEM_NONE) {
        return 0;
    }
    Vec3 spot = vec3_add(origin, vec3_scale(dir, 0.8f));
    Vec3 vel = vec3_add(vec3_scale(dir, 1.2f + 8.5f * power),
                        v3(0.0f, 0.5f + 1.8f * power, 0.0f));
    interact_spawn_pickup(world, phys, it->hands, spot, atan2f(dir.x, -dir.z), vel);
    it->hands.kind = ITEM_NONE;
    return 1;
}
