#include "player/interact.h"
#include "world/pickup_body.h"
#include "audio/tapes.h"
#include "carsys/carsys.h"
#include "core/arena.h"
#include "core/config.h"
#include "core/log.h"
#include "physics/heightfield.h"
#include "physics/world.h"
#include "platform/filesystem.h"
#include "player/player.h"
#include "terminal/disks.h"
#include "vehicle/vehicle.h"
#include "world/terrain.h"
#include "world/zone.h"

#include <cstdio>

namespace anom {
namespace {
constexpr f32 kHoodOpenForBay = 0.8f;
constexpr f32 kDoorOpenForUse = 0.6f;
constexpr f32 kExitLookYaw = 1.15f;
constexpr const char* kDriverOnly = "driver's controls";

const InteractBox kDefaultBoxes[IBOX_COUNT] = {
    {"door", {0.78f, -0.02f, 0.10f}, {0.10f, 0.26f, 0.62f}},
    {"door_panel", {1.45f, -0.02f, -0.18f}, {0.48f, 0.26f, 0.42f}},
    {"handbrake", {-0.13f, -0.13f, 0.33f}, {0.09f, 0.10f, 0.16f}},
    {"wiper", {-0.48f, 0.08f, -0.28f}, {0.05f, 0.04f, 0.06f}},
    {"ignition", {-0.22f, 0.05f, -0.28f}, {0.07f, 0.06f, 0.08f}},
    {"deck", {0.12f, -0.045f, -0.295f}, {0.09f, 0.05f, 0.055f}},
    {"disk_slot", {0.0f, -0.119f, 0.265f}, {0.10f, 0.035f, 0.045f}},
    {"hood_latch", {0.0f, 0.04f, -1.50f}, {0.50f, 0.10f, 0.42f}},
    {"hood_raised", {0.0f, 0.72f, -0.92f}, {0.50f, 0.62f, 0.30f}},
    {"trunk_lid", {0.0f, 0.22f, 1.73f}, {0.50f, 0.09f, 0.31f}},
    {"trunk_edge", {0.0f, 0.55f, 1.52f}, {0.50f, 0.38f, 0.16f}},
    {"fuel", {0.80f, 0.10f, 1.30f}, {0.10f, 0.10f, 0.14f}},
    {"antenna_jack", {0.35f, 0.54f, 0.36f}, {0.055f, 0.055f, 0.055f}},
    {"bay_jack", {0.32f, 0.02f, -0.75f}, {0.055f, 0.055f, 0.055f}},
};

bool ray_vs_local_box(Ray local, Vec3 center, Vec3 half, f32* out_t)
{
    const Aabb box{center - half, center + half};
    return ray_vs_aabb(local, box, kInteractRange, out_t);
}

Ray chassis_local_ray(const RigidBody& body, Ray view_ray)
{
    const Quat inv = conjugate(body.rot);
    Ray local;
    local.origin = rotate(inv, view_ray.origin - body.pos);
    local.dir = rotate(inv, view_ray.dir);
    return local;
}

}

struct Interact::Candidate {
    f32 t = 1e30f;
    InteractAction action = InteractAction::None;
    PartKind part = PART_COUNT;
    EntityHandle entity;
    i32 side = 0;
    u32 cargo = 0;
    i32 cable = -1;
    bool is_hold = false;
    Vec3 center;
    Vec3 half;
    Vec3 place_pos;
    FixedString<96> prompt;

    bool consider(f32 hit_t, InteractAction act, Vec3 c, Vec3 h, bool hold,
                  std::string_view text)
    {
        if (act == InteractAction::None || hit_t >= t) {
            return false;
        }
        t = hit_t;
        action = act;
        is_hold = hold;
        center = c;
        half = h;
        prompt.assign(text);
        return true;
    }
};

void InteractBoxes::load(Arena& scratch)
{
    ArenaScope scope(scratch);
    const fs::FileData file = fs::read_entire_file(scratch, path_.view());
    if (!file.valid()) {
        return;
    }
    Config cfg;
    if (!cfg.parse(scratch, file.text())) {
        return;
    }
    for (u32 i = 0; i < IBOX_COUNT; i++) {
        FixedString<64> key;
        key.format("boxes.%s", boxes_[i].name.c_str());
        f32 vals[6];
        if (cfg.get_f32_list(key.view(), vals) == 6) {
            boxes_[i].center = Vec3{vals[0], vals[1], vals[2]};
            boxes_[i].half = Vec3{vals[3], vals[4], vals[5]};
        }
    }
    log_info("interact: loaded boxes from %s", path_.c_str());
}

void InteractBoxes::init(Arena& scratch, std::string_view path)
{
    for (u32 i = 0; i < IBOX_COUNT; i++) {
        boxes_[i] = kDefaultBoxes[i];
    }
    path_.assign(path);
    mtime_ = fs::file_mtime(path_.view());
    if (mtime_ != 0) {
        load(scratch);
    }
}

void InteractBoxes::poll(Arena& scratch, f64 now)
{
    if (now < next_poll_ || path_.empty()) {
        return;
    }
    next_poll_ = now + 1.0;
    const i64 mtime = fs::file_mtime(path_.view());
    if (mtime != 0 && mtime != mtime_) {
        mtime_ = mtime;
        load(scratch);
    }
}

bool InteractBoxes::save() const
{
    if (path_.empty()) {
        return false;
    }
    std::FILE* out = fs::open(path_.view(), "wb");
    if (!out) {
        log_warn("interact: could not write %s", path_.c_str());
        return false;
    }
    std::fprintf(out, "[boxes]\n");
    for (u32 i = 0; i < IBOX_COUNT; i++) {
        const InteractBox& b = boxes_[i];
        std::fprintf(out, "%s = %.3f %.3f %.3f %.3f %.3f %.3f\n", b.name.c_str(),
                     static_cast<f64>(b.center.x), static_cast<f64>(b.center.y),
                     static_cast<f64>(b.center.z), static_cast<f64>(b.half.x),
                     static_cast<f64>(b.half.y), static_cast<f64>(b.half.z));
    }
    std::fclose(out);
    log_info("interact: saved boxes to %s", path_.c_str());
    return true;
}

void Interact::init()
{
    *this = Interact{};
    cable_drag_ = -1;
    target_part_ = PART_COUNT;
}

void Interact::set_tower(bool present, Vec3 port)
{
    tower_present_ = present;
    tower_port_ = port;
}

bool Interact::take_rounds_request()
{
    const bool value = rounds_request_;
    rounds_request_ = false;
    return value;
}
bool Interact::pour_request()
{
    const bool request = pour_request_;
    pour_request_ = false;
    return request;
}

bool Interact::take_holster_request()
{
    const bool value = holster_request_;
    holster_request_ = false;
    return value;
}

bool Interact::take_terminal_request()
{
    const bool value = use_terminal_request_;
    use_terminal_request_ = false;
    return value;
}

void Interact::adopt_holdings(const Interact& other)
{
    hands_ = other.hands_;
    has_key_ = other.has_key_;
    cable_drag_ = other.cable_drag_;
}

void Interact::perform(const InteractContext& ctx)
{
    CarSys& sys = *ctx.sys;

    switch (action_) {
    case InteractAction::OpenDoor:
        sys.door_target[target_side_] = true;
        break;
    case InteractAction::CloseDoor:
        sys.door_target[target_side_] = false;
        break;
    case InteractAction::Handbrake:
        sys.handbrake_latched = !sys.handbrake_latched;
        break;
    case InteractAction::InsertKey:
        sys.key_inserted = true;
        has_key_ = false;
        break;
    case InteractAction::EngineOff:
        sys.stop_engine();
        break;
    case InteractAction::PourMaterials:
        pour_target_ = 0;
        pour_request_ = true;
        break;
    case InteractAction::PlaceTankOnPrinter: {
        Entity* entity = ctx.world->entity(target_entity_);
        if (!entity || hands_.kind != ITEM_TANK) {
            break;
        }
        i32 id = static_cast<i32>(entity->aux_data);
        if (!sys.loose_printer(id)) {
            id = sys.claim_printer();
            entity->aux_data = static_cast<u32>(id);
        }
        LoosePrinter* printer = sys.loose_printer(id);
        if (printer && !printer->has_tank) {
            printer->has_tank = true;
            printer->tank_condition = hands_.condition;
            sys.fill_tank_from(hands_.aux, printer->bay.tank);
            hands_ = Item{};
        }
        break;
    }
    case InteractAction::TakeTankOffPrinter: {
        const Entity* entity = ctx.world->entity(target_entity_);
        LoosePrinter* printer = entity ? sys.loose_printer(static_cast<i32>(entity->aux_data)) : nullptr;
        if (printer && printer->has_tank && hands_.kind == ITEM_NONE) {
            hands_.kind = ITEM_TANK;
            hands_.condition = printer->tank_condition;
            hands_.aux = sys.stash_tank(printer->bay.tank);
            printer->has_tank = false;
        }
        break;
    }
    case InteractAction::CablePlugLoosePrinter: {
        Entity* entity = ctx.world->entity(target_entity_);
        if (!entity) {
            break;
        }
        i32 id = static_cast<i32>(entity->aux_data);
        if (!sys.loose_printer(id)) {
            id = sys.claim_printer();
            entity->aux_data = static_cast<u32>(id);
        }
        if (id != 0) {
            sys.cables[CABLE_BUS].state = CableState::Plugged;
            sys.bus_target = kBusTargetLoosePrinter;
            sys.bus_printer = id;
            cable_drag_ = -1;
        }
        break;
    }
    case InteractAction::FuelCap:
        sys.fuel_cap_open = !sys.fuel_cap_open;
        break;
    case InteractAction::Computer:
        sys.computer_on = !sys.computer_on;
        break;

    case InteractAction::RemovePart: {
        PartSlot& slot = sys.parts[target_part_];
        hands_.kind = target_part_ == PART_ANTENNA ? antenna_item_for_variant(slot.variant)
                                                   : item_for_part(target_part_);
        hands_.condition = slot.condition;
        hands_.aux = 0;
        if (target_part_ == PART_TANK) {
            hands_.aux = sys.stash_tank(sys.synth.tank);
        } else if (target_part_ == PART_PRINTER) {
            hands_.aux = sys.stash_installed_printer();
        }
        slot.installed = false;
        if (target_part_ == PART_ANTENNA) {
            sys.cables[CABLE_COAX].reset();
            if (cable_drag_ == static_cast<i32>(CABLE_COAX)) {
                cable_drag_ = -1;
            }
        }
        break;
    }
    case InteractAction::InstallPart: {
        PartSlot& slot = sys.parts[target_part_];
        slot.installed = true;
        slot.condition = hands_.condition;
        if (target_part_ == PART_ANTENNA) {
            slot.variant = antenna_variant_for_item(hands_.kind);
        } else if (target_part_ == PART_TANK) {
            sys.fill_tank_from(hands_.aux, sys.synth.tank);
        } else if (target_part_ == PART_PRINTER) {
            sys.install_printer_from(hands_.aux);
        }
        hands_.kind = ITEM_NONE;
        hands_.aux = 0;
        break;
    }

    case InteractAction::ToggleHood:
        sys.hood_target = !sys.hood_target;
        break;
    case InteractAction::ToggleTrunk:
        sys.trunk_target = !sys.trunk_target;
        break;

    case InteractAction::PlaceCargo:
        if (sys.cargo_add(hands_, place_pos_)) {
            hands_.kind = ITEM_NONE;
        }
        break;
    case InteractAction::TakeCargo:
        sys.cargo_take(target_cargo_, hands_);
        break;

    case InteractAction::TerminalUse:
    case InteractAction::Pickup: {
        if (hands_.kind != ITEM_NONE && action_ == InteractAction::TerminalUse) {
            break;
        }
        Entity* entity = ctx.world->entity(target_entity_);
        if (entity) {
            if (static_cast<ItemKind>(entity->aux_kind) == ITEM_KEY) {
                has_key_ = true;
            } else {
                hands_.kind = static_cast<ItemKind>(entity->aux_kind);
                hands_.condition = entity->aux_value;
                hands_.aux = static_cast<i32>(entity->aux_data);
            }
            if (entity->body != kNoEntityBody) {
                pickup_body_destroy(*ctx.phys, *entity);
            }
            ctx.world->despawn(target_entity_);
        } else if (action_ == InteractAction::TerminalUse
                   && sys.parts[PART_COMPUTER].installed) {
            PartSlot& slot = sys.parts[PART_COMPUTER];
            hands_.kind = ITEM_COMPUTER;
            hands_.condition = slot.condition;
            hands_.aux = 0;
            slot.installed = false;
        }
        break;
    }

    case InteractAction::CableGrab:
        sys.cables[target_cable_].state = CableState::Dragged;
        cable_drag_ = target_cable_;
        break;
    case InteractAction::CablePlug:
        sys.cables[target_cable_].state = CableState::Plugged;
        if (target_cable_ == static_cast<i32>(CABLE_COAX)) {
            sys.coax_target = kCoaxTargetAntenna;
        }
        if (target_cable_ == static_cast<i32>(CABLE_BUS)) {
            sys.bus_target = kBusTargetCar;
        }
        cable_drag_ = -1;
        break;
    case InteractAction::CablePlugCamera:
        sys.cables[CABLE_COAX].state = CableState::Plugged;
        sys.coax_target = kCoaxTargetCamera;
        cable_drag_ = -1;
        break;
    case InteractAction::CablePlugPrinter:
        sys.cables[CABLE_BUS].state = CableState::Plugged;
        sys.bus_target = kBusTargetPrinter;
        cable_drag_ = -1;
        break;
    case InteractAction::CablePlugTower:
        sys.cables[CABLE_BUS].state = CableState::Plugged;
        sys.bus_target = kBusTargetTower;
        cable_drag_ = -1;
        break;
    case InteractAction::CableRouteReel:
        sys.cables[target_cable_].via_reel = true;
        break;
    case InteractAction::CableUnplug:
        sys.cables[target_cable_].reset();
        if (target_cable_ == static_cast<i32>(CABLE_COAX)) {
            sys.coax_target = kCoaxTargetAntenna;
        }
        if (target_cable_ == static_cast<i32>(CABLE_BUS)) {
            sys.bus_target = kBusTargetCar;
            sys.bus_printer = 0;
        }
        if (cable_drag_ == target_cable_) {
            cable_drag_ = -1;
        }
        break;

    case InteractAction::DiskInsert:
        sys.floppy_disk = hands_.aux;
        sys.floppy_cond = hands_.condition;
        hands_.kind = ITEM_NONE;
        break;
    case InteractAction::DiskEject:
        hands_.kind = ITEM_FLOPPY;
        hands_.aux = sys.floppy_disk;
        hands_.condition = sys.floppy_cond;
        sys.floppy_disk = -1;
        break;

    case InteractAction::Wipers:
        sys.wiper_mode = (sys.wiper_mode + 1) % 3;
        break;

    case InteractAction::TakeRounds: {
        const Entity* entity = ctx.world->entity(target_entity_);
        rounds_target_ = entity && static_cast<ItemKind>(entity->aux_kind) == ITEM_PRINTER
                           ? -static_cast<i32>(entity->aux_data)
                           : 0;
        rounds_request_ = true;
        break;
    }

    case InteractAction::TapeInsert:
        sys.tape_inserted = hands_.aux;
        sys.tape_cond = hands_.condition;
        hands_.kind = ITEM_NONE;
        break;
    case InteractAction::TapeEject:
        hands_.kind = ITEM_CASSETTE;
        hands_.aux = sys.tape_inserted;
        hands_.condition = sys.tape_cond;
        sys.tape_inserted = -1;
        sys.deck_play = false;
        break;

    case InteractAction::Refuel:
        sys.fluids.fuel = f_min(sys.fluids.fuel + 0.45f, 1.0f);
        hands_.kind = ITEM_NONE;
        break;
    case InteractAction::OilFill:
        sys.fluids.oil = 1.0f;
        hands_.kind = ITEM_NONE;
        break;

    default:
        break;
    }
}

EntityHandle interact_spawn_pickup(World& world, PhysWorld& phys, Item item, Vec3 pos, f32 yaw,
                                   Vec3 vel)
{
    const Quat rot = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, yaw);
    const Quat mesh_rot = rot * item_cargo_rot(item.kind);
    const Vec3 mesh_pos = pos - rotate(mesh_rot, item_mesh_center(item.kind));

    const EntityHandle handle = world.spawn(EntityKind::PartPickup, mesh_pos, mesh_rot, 1.0f,
                                            item_mesh(item.kind), 0);
    Entity* entity = world.entity(handle);
    if (!entity) {
        return handle;
    }
    entity->aux_kind = static_cast<u32>(item.kind);
    entity->aux_value = item.condition;
    entity->aux_data = static_cast<u32>(item.aux);

    if (item.kind != ITEM_KEY) {
        entity->body = pickup_body_create(phys, pos, rot, item_cargo_half(item.kind), f_max(item_mass(item.kind), 1.0f), vel);
    }
    return handle;
}

void interact_spawn_zone_pickups(World& world, PhysWorld& phys, const Terrain& terrain,
                                 const ZonePickups& pickups)
{
    for (u32 i = 0; i < pickups.count; i++) {
        const ZonePickup& p = pickups.items[i];
        const ItemKind kind = item_from_id(p.item.view());
        if (kind == ITEM_NONE) {
            log_warn("zone: unknown pickup item: %s", p.item.c_str());
            continue;
        }

        f32 ground = terrain.heightfield().sample(p.x, p.z);
        Ray down;
        down.origin = Vec3{p.x, ground + 1.6f, p.z};
        down.dir = Vec3{0.0f, -1.0f, 0.0f};
        PhysRayHit hit{};
        if (phys.raycast(down, 8.0f, &hit)) {
            ground = hit.point.y;
        }

        const Vec3 pos{p.x, ground + item_cargo_half(kind).y + 0.10f, p.z};
        Item item;
        item.kind = kind;
        item.condition = p.condition;
        item.aux = p.aux;
        interact_spawn_pickup(world, phys, item, pos, p.yaw_deg * kDegToRad,
                              Vec3{0.0f, 0.0f, 0.0f});
    }
    log_info("zone: spawned %u pickups", pickups.count);
}

void Interact::consider_cable_root(Candidate& best, CarSys& sys, CableKind kind, f32 t,
                                   Vec3 center, Vec3 half) const
{
    const Cable& cable = sys.cables[kind];
    const char* name = kind == CABLE_COAX ? "coax" : "bus";
    FixedString<96> prompt;
    bool taken = false;

    if (cable.state == CableState::Stowed) {
        if (cable_drag_ < 0 && hands_.kind == ITEM_NONE) {
            prompt.format("[E] grab %s cable", name);
            taken = best.consider(t, InteractAction::CableGrab, center, half, false,
                                  prompt.view());
        } else {
            prompt.format("%s port", name);
            taken = best.consider(t, InteractAction::Info, center, half, false, prompt.view());
        }
    } else if (cable.state == CableState::Plugged) {
        prompt.format("[E] unplug %s cable", name);
        taken = best.consider(t, InteractAction::CableUnplug, center, half, false,
                              prompt.view());
    } else {
        prompt.format("%s cable is out", name);
        taken = best.consider(t, InteractAction::Info, center, half, false, prompt.view());
    }
    if (taken) {
        best.cable = static_cast<i32>(kind);
    }
}

void Interact::consider_cable_jack(Candidate& best, CarSys& sys, CableKind kind, f32 t,
                                   Vec3 center, Vec3 half, std::string_view target_name) const
{
    const Cable& cable = sys.cables[kind];
    const char* name = kind == CABLE_COAX ? "coax" : "bus";
    FixedString<96> prompt;
    bool taken = false;

    if (cable.state == CableState::Plugged) {
        prompt.format("[E] unplug %s cable", name);
        taken = best.consider(t, InteractAction::CableUnplug, center, half, false,
                              prompt.view());
    } else if (cable_drag_ == static_cast<i32>(kind)) {
        prompt.format("[E] connect %s to %.*s", name, static_cast<int>(target_name.size()),
                      target_name.data());
        taken = best.consider(t, InteractAction::CablePlug, center, half, false, prompt.view());
    } else {
        prompt.format("%s jack", name);
        taken = best.consider(t, InteractAction::Info, center, half, false, prompt.view());
    }
    if (taken) {
        best.cable = static_cast<i32>(kind);
    }
}

void Interact::consider_disk_slot(Candidate& best, CarSys& sys, f32 t, Vec3 center,
                                  Vec3 half) const
{
    FixedString<96> prompt;
    if (hands_.kind == ITEM_FLOPPY && sys.floppy_disk < 0) {
        const std::string_view label = disk_label(hands_.aux);
        prompt.format("[E] insert %.*s", static_cast<int>(label.size()), label.data());
        best.consider(t, InteractAction::DiskInsert, center, half, false, prompt.view());
    } else if (sys.floppy_disk >= 0 && hands_.kind == ITEM_NONE && cable_drag_ < 0) {
        const std::string_view label = disk_label(sys.floppy_disk);
        prompt.format("[E] eject %.*s", static_cast<int>(label.size()), label.data());
        best.consider(t, InteractAction::DiskEject, center, half, false, prompt.view());
    } else if (sys.floppy_disk < 0) {
        best.consider(t, InteractAction::Info, center, half, false, "drive b: slot empty");
    } else {
        best.consider(t, InteractAction::Info, center, half, false, "drive b: disk loaded");
    }
}

void Interact::consider_deck_slot(Candidate& best, CarSys& sys, const TapeLibrary* tapes, f32 t,
                                  Vec3 center, Vec3 half) const
{
    const auto label = [tapes](i32 aux) -> std::string_view {
        return tapes ? tapes->label(aux) : std::string_view("TAPE");
    };

    FixedString<96> prompt;
    if (hands_.kind == ITEM_CASSETTE && sys.tape_inserted < 0) {
        const std::string_view name = label(hands_.aux);
        prompt.format("[E] insert tape \"%.*s\"", static_cast<int>(name.size()), name.data());
        best.consider(t, InteractAction::TapeInsert, center, half, false, prompt.view());
    } else if (sys.tape_inserted >= 0 && hands_.kind == ITEM_NONE && cable_drag_ < 0) {
        prompt.format("[E] %s | hold [E] eject tape", sys.deck_play ? "stop tape" : "play tape");
        best.consider(t, InteractAction::TapeEject, center, half, true, prompt.view());
    } else if (sys.tape_inserted < 0) {
        best.consider(t, InteractAction::Info, center, half, false, "tape deck: empty");
    } else {
        const std::string_view name = label(sys.tape_inserted);
        prompt.format("tape deck: \"%.*s\"", static_cast<int>(name.size()), name.data());
        best.consider(t, InteractAction::Info, center, half, false, prompt.view());
    }
}

void Interact::resolve_doors(Candidate& best, const InteractBoxes& boxes,
                             const InteractContext& ctx, Ray local, bool on_foot) const
{
    CarSys& sys = *ctx.sys;
    const Vec3 com = ctx.veh->config().com_offset;
    const InteractBox& door_box = boxes.box(IBOX_DOOR);
    const Vec3 door_half = door_box.half;

    const VehicleConfig& cfg = ctx.veh->config();
    f32 t = 0.0f;

    for (i32 side = 0; side < 2; side++) {
        const f32 sign = side == 0 ? -1.0f : 1.0f;
        const bool open = sys.door_open[side] > kDoorOpenForUse;
        const Vec3 center = Vec3{sign * door_box.center.x, door_box.center.y, door_box.center.z}
                          - com;

        if (ray_vs_local_box(local, center, door_half, &t)) {
            bool taken = false;
            if (on_foot) {
                i32 seat = -1;
                for (u32 k = 0; k < cfg.seat_count; k++) {
                    if (cfg.seats[k].door_side == side) {
                        seat = static_cast<i32>(k);
                    }
                }
                const SeatConfig* sc = seat >= 0 ? &cfg.seats[seat] : nullptr;
                if (!open) {
                    taken = best.consider(t + 0.05f, InteractAction::OpenDoor, center, door_half,
                                          false, "[E] open door");
                } else if (!sc) {
                    taken = false;
                } else if (sc->blocked_by != PART_COUNT && sys.parts[sc->blocked_by].installed) {
                    const std::string_view name = part_def(sc->blocked_by).name;
                    FixedString<96> blocked;
                    blocked.format("the %.*s is on that seat", static_cast<int>(name.size()), name.data());
                    taken = best.consider(t + 0.05f, InteractAction::Info, center, door_half, false, blocked.view());
                } else if (ctx.seats_taken & (1u << static_cast<u32>(seat))) {
                    taken = best.consider(t + 0.05f, InteractAction::Info, center, door_half, false, "seat taken");
                } else if (ctx.player->can_enter(*ctx.phys, ctx.veh, static_cast<u32>(seat))) {
                    taken = best.consider(t + 0.05f, InteractAction::EnterCar, center, door_half, false,
                                          sc->drives ? "[E] drive" : "[E] ride along");
                }
            } else {
                taken = best.consider(t + 0.05f,
                                      open ? InteractAction::CloseDoor : InteractAction::OpenDoor,
                                      center, door_half, false,
                                      open ? "[E] close door" : "[E] open door");
            }
            if (taken) {
                best.side = side;
            }
        }

        if (on_foot && open) {
            const InteractBox& panel = boxes.box(IBOX_DOOR_PANEL);
            const Vec3 panel_center =
                Vec3{sign * panel.center.x, panel.center.y, panel.center.z} - com;
            if (ray_vs_local_box(local, panel_center, panel.half, &t)
                && best.consider(t + 0.10f, InteractAction::CloseDoor, panel_center, panel.half,
                                 false, "[E] close door")) {
                best.side = side;
            }
        }
    }
}

void Interact::resolve_in_car(Candidate& best, const InteractBoxes& boxes,
                              const InteractContext& ctx) const
{
    if (!ctx.veh) {
        return;
    }
    RigidBody* body = ctx.phys->body(ctx.veh->body());
    if (!body) {
        return;
    }
    CarSys& sys = *ctx.sys;
    const Ray local = chassis_local_ray(*body, ctx.view_ray);
    const Vec3 com = ctx.veh->config().com_offset;
    f32 t = 0.0f;

    resolve_doors(best, boxes, ctx, local, false);

    const Vec3 lever_center = boxes.box(IBOX_HANDBRAKE).center - com;
    const Vec3 lever_half = boxes.box(IBOX_HANDBRAKE).half;
    if (ray_vs_local_box(local, lever_center, lever_half, &t)) {
        if (ctx.is_driver) {
            best.consider(t, InteractAction::Handbrake, lever_center, lever_half, false,
                          sys.handbrake_latched ? "[E] release handbrake" : "[E] set handbrake");
        } else {
            best.consider(t, InteractAction::Info, lever_center, lever_half, false, kDriverOnly);
        }
    }

    static constexpr const char* kWiperModes[3] = {"off", "interval", "full"};
    const Vec3 stalk_center = boxes.box(IBOX_WIPER).center - com;
    const Vec3 stalk_half = boxes.box(IBOX_WIPER).half;
    if (ray_vs_local_box(local, stalk_center, stalk_half, &t)) {
        if (ctx.is_driver) {
            FixedString<96> prompt;
            prompt.format("[E] wipers: %s", kWiperModes[sys.wiper_mode % 3]);
            best.consider(t, InteractAction::Wipers, stalk_center, stalk_half, false, prompt.view());
        } else {
            best.consider(t, InteractAction::Info, stalk_center, stalk_half, false, kDriverOnly);
        }
    }

    const Vec3 ign_center = boxes.box(IBOX_IGNITION).center - com;
    const Vec3 ign_half = boxes.box(IBOX_IGNITION).half;
    if (ray_vs_local_box(local, ign_center, ign_half, &t)) {
        if (!ctx.is_driver) {
            best.consider(t, InteractAction::Info, ign_center, ign_half, false, kDriverOnly);
        } else if (!sys.key_inserted) {
            if (has_key_) {
                best.consider(t, InteractAction::InsertKey, ign_center, ign_half, false,
                              "[E] insert key  (or [I])");
            } else {
                best.consider(t, InteractAction::Info, ign_center, ign_half, false,
                              "ignition - no key");
            }
        } else if (sys.engine_on) {
            best.consider(t, InteractAction::EngineOff, ign_center, ign_half, false,
                          "[E] switch off  (or tap [I])");
        } else {
            const StartBlocker blocker = sys.start_blocker();
            if (blocker == StartBlocker::None) {
                best.consider(t, InteractAction::Crank, ign_center, ign_half, false,
                              "hold [E] turn key | [G] take key");
            } else {
                FixedString<96> prompt;
                const std::string_view why = start_blocker_text(blocker);
                prompt.format("hold [E] turn key - %.*s | [G] take key", static_cast<int>(why.size()), why.data());
                best.consider(t, InteractAction::Crank, ign_center, ign_half, false, prompt.view());
            }
        }
    }

    const PartDef& term_def = part_def(PART_COMPUTER);
    const Vec3 term_center = term_def.socket_pos - com;
    if (ray_vs_local_box(local, term_center, term_def.socket_half, &t)) {
        if (sys.parts[PART_COMPUTER].installed) {
            if (sys.computer_on) {
                best.consider(t, InteractAction::TerminalUse, term_center, term_def.socket_half,
                              false, "[E] use terminal");
            } else {
                best.consider(t, InteractAction::Computer, term_center, term_def.socket_half,
                              false, "[E] power on terminal");
            }
        } else if (hands_.kind == ITEM_COMPUTER) {
            if (best.consider(t, InteractAction::InstallPart, term_center, term_def.socket_half,
                              true, "hold [E] install terminal")) {
                best.part = PART_COMPUTER;
            }
        }
    }

    if (sys.parts[PART_COMPUTER].installed) {
        const Quat rest = part_computer_rest_rot();
        const Vec3 slot_center = term_center + rotate(rest, boxes.box(IBOX_DISK_SLOT).center);
        if (ray_vs_local_box(local, slot_center, boxes.box(IBOX_DISK_SLOT).half, &t)) {
            consider_disk_slot(best, sys, t - 0.30f, slot_center, boxes.box(IBOX_DISK_SLOT).half);
        }
    }

    const Vec3 deck_center = boxes.box(IBOX_DECK).center - com;
    if (ray_vs_local_box(local, deck_center, boxes.box(IBOX_DECK).half, &t)) {
        consider_deck_slot(best, sys, ctx.tapes, t - 0.15f, deck_center,
                           boxes.box(IBOX_DECK).half);
    }

    for (i32 side = 0; side < 2; side++) {
        const f32 sign = side == 0 ? -1.0f : 1.0f;
        if (sys.door_open[side] < kDoorOpenForUse) {
            continue;
        }
        if (ctx.player->look_yaw() * sign > kExitLookYaw) {
            if (side == 1 && sys.parts[PART_COMPUTER].installed) {
                continue;
            }
            if (ctx.player->can_exit(*ctx.phys, ctx.veh)) {
                const Vec3 center = Vec3{sign * 0.95f, 0.0f, 0.10f} - com;
                if (best.consider(0.05f, InteractAction::ExitCar, center,
                                  Vec3{0.05f, 0.25f, 0.55f}, false, "[E] get out")) {
                    best.side = side;
                }
            }
        }
    }
}

void Interact::resolve_car_targets(Candidate& best, const InteractBoxes& boxes,
                                   const InteractContext& ctx) const
{
    if (!ctx.veh) {
        return;
    }
    RigidBody* body = ctx.phys->body(ctx.veh->body());
    if (!body) {
        return;
    }
    CarSys& sys = *ctx.sys;
    const Ray local = chassis_local_ray(*body, ctx.view_ray);
    const Vec3 com = ctx.veh->config().com_offset;
    FixedString<96> prompt;
    f32 t = 0.0f;

    for (u32 k = 0; k < PART_COUNT; k++) {
        const PartKind kind = static_cast<PartKind>(k);
        const PartDef& def = part_def(kind);
        PartSlot& slot = sys.parts[k];
        if (def.engine_bay && sys.hood_open < kHoodOpenForBay) {
            continue;
        }
        const Vec3 center = def.socket_pos - com;
        if (!ray_vs_local_box(local, center, def.socket_half, &t)) {
            continue;
        }

        bool taken = false;
        if (slot.installed) {
            if (kind == PART_ENGINE && hands_.kind == ITEM_OILCAN) {
                taken = best.consider(t, InteractAction::OilFill, center, def.socket_half, true,
                                      "hold [E] top up oil");
            } else if (kind == PART_PRINTER && hands_.kind == ITEM_NONE && sys.synth.tray_total() > 0) {
                prompt.format("[E] take %u rounds from the printer", sys.synth.tray_total());
                taken = best.consider(t, InteractAction::TakeRounds, center, def.socket_half, false, prompt.view());
            } else if (kind == PART_PRINTER && hands_.kind == ITEM_NONE && sys.parts[PART_TANK].installed) {
                taken = best.consider(t, InteractAction::Info, center, def.socket_half, false,
                                      "printer | take the tank off it first");
            } else if (def.removable && hands_.kind == ITEM_NONE) {
                if (kind == PART_COMPUTER && sys.computer_on) {
                    taken = best.consider(t, InteractAction::TerminalUse, center,
                                          def.socket_half, true,
                                          "[E] use terminal | hold [E] take");
                } else if (kind == PART_COMPUTER) {
                    prompt.format("tap [E] power | hold [E] take terminal (%.0f%%)",
                                  static_cast<f64>(slot.condition * 100.0f));
                    taken = best.consider(t, InteractAction::RemovePart, center, def.socket_half,
                                          true, prompt.view());
                } else if (kind == PART_TANK && ctx.materials > 0) {
                    prompt.format("tap [E] pour %u materials | hold [E] take tank (%u doses)", ctx.materials,
                                  sys.synth.tank_total());
                    taken = best.consider(t, InteractAction::RemovePart, center, def.socket_half,
                                          true, prompt.view());
                } else if (kind == PART_TANK) {
                    prompt.format("material tank: %u doses | hold [E] take", sys.synth.tank_total());
                    taken = best.consider(t, InteractAction::RemovePart, center, def.socket_half,
                                          true, prompt.view());
                } else if (kind == PART_PRINTER && sys.synth.job_left > 0) {
                    prompt.format("printer: %u left%s | hold [E] take", static_cast<u32>(sys.synth.job_left),
                                  sys.synth.stalled ? " (stalled)" : "");
                    taken = best.consider(t, InteractAction::RemovePart, center, def.socket_half,
                                          true, prompt.view());
                } else {
                    prompt.format("hold [E] take %.*s (%.0f%%)",
                                  static_cast<int>(def.name.size()), def.name.data(),
                                  static_cast<f64>(slot.condition * 100.0f));
                    taken = best.consider(t, InteractAction::RemovePart, center, def.socket_half,
                                          true, prompt.view());
                }
            } else {
                prompt.format("%.*s %.0f%%", static_cast<int>(def.name.size()), def.name.data(),
                              static_cast<f64>(slot.condition * 100.0f));
                taken = best.consider(t, InteractAction::Info, center, def.socket_half, false,
                                      prompt.view());
            }
        } else if (kind == PART_TANK && !sys.parts[PART_PRINTER].installed) {
            continue;
        } else if (hands_.kind != ITEM_NONE
                   && (kind == PART_ANTENNA ? antenna_variant_for_item(hands_.kind) >= 0
                                            : item_for_part(kind) == hands_.kind)) {
            const std::string_view iname = item_name(hands_.kind);
            prompt.format("hold [E] install %.*s (%.0f%%)", static_cast<int>(iname.size()),
                          iname.data(), static_cast<f64>(hands_.condition * 100.0f));
            taken = best.consider(t, InteractAction::InstallPart, center, def.socket_half, true,
                                  prompt.view());
        } else {
            prompt.format("%.*s missing", static_cast<int>(def.name.size()), def.name.data());
            taken = best.consider(t, InteractAction::Info, center, def.socket_half, false,
                                  prompt.view());
        }
        if (taken) {
            best.part = kind;
        }
    }

    if (sys.parts[PART_COMPUTER].installed) {
        const Quat rest = part_computer_rest_rot();
        const Vec3 term_base = part_def(PART_COMPUTER).socket_pos - com;
        const Vec3 ports[2] = {kConnectorCoaxLocal, kConnectorBusLocal};
        const Vec3 port_half{0.05f, 0.05f, 0.06f};
        for (u32 pk = 0; pk < 2; pk++) {
            const Vec3 center = term_base + rotate(rest, ports[pk]);
            if (ray_vs_local_box(local, center, port_half, &t)) {
                consider_cable_root(best, sys, static_cast<CableKind>(pk), t - 0.45f, center,
                                    port_half);
            }
        }
        const Vec3 slot_center = term_base + rotate(rest, boxes.box(IBOX_DISK_SLOT).center);
        if (ray_vs_local_box(local, slot_center, boxes.box(IBOX_DISK_SLOT).half, &t)) {
            consider_disk_slot(best, sys, t - 0.30f, slot_center, boxes.box(IBOX_DISK_SLOT).half);
        }
    }

    {
        const Vec3 jack_half = boxes.box(IBOX_ANTENNA_JACK).half;
        Vec3 center = boxes.box(IBOX_ANTENNA_JACK).center - com;
        if (ray_vs_local_box(local, center, jack_half, &t)) {
            if (!sys.parts[PART_ANTENNA].installed
                && cable_drag_ == static_cast<i32>(CABLE_COAX)) {
                best.consider(t - 0.20f, InteractAction::Info, center, jack_half, false,
                              "no antenna mounted");
            } else if (sys.parts[PART_ANTENNA].installed
                       || sys.cables[CABLE_COAX].state == CableState::Plugged) {
                consider_cable_jack(best, sys, CABLE_COAX, t - 0.20f, center, jack_half,
                                    "antenna");
            }
        }
        if (sys.parts[PART_PRINTER].installed && sys.bus_target != kBusTargetTower) {
            const Vec3 jack = kPrinterJackLocal - com;
            if (ray_vs_local_box(local, jack, kPrinterJackHalf, &t)) {
                const Cable& bus = sys.cables[CABLE_BUS];
                if (bus.state == CableState::Plugged && sys.bus_target == kBusTargetPrinter) {
                    if (best.consider(t - 0.20f, InteractAction::CableUnplug, jack, kPrinterJackHalf, false,
                                      "[E] unplug bus cable")) {
                        best.cable = static_cast<i32>(CABLE_BUS);
                    }
                } else if (cable_drag_ == static_cast<i32>(CABLE_BUS)) {
                    if (best.consider(t - 0.20f, InteractAction::CablePlugPrinter, jack, kPrinterJackHalf, false,
                                      "[E] connect bus to printer")) {
                        best.cable = static_cast<i32>(CABLE_BUS);
                    }
                } else {
                    best.consider(t - 0.20f, InteractAction::Info, jack, kPrinterJackHalf, false, "printer bus jack");
                }
            }
        }
        if (sys.hood_open >= kHoodOpenForBay && sys.bus_target == kBusTargetCar) {
            center = boxes.box(IBOX_BAY_JACK).center - com;
            if (ray_vs_local_box(local, center, boxes.box(IBOX_BAY_JACK).half, &t)) {
                consider_cable_jack(best, sys, CABLE_BUS, t - 0.20f, center,
                                    boxes.box(IBOX_BAY_JACK).half, "vehicle bus");
            }
        }
    }

    resolve_doors(best, boxes, ctx, local, true);

    if (sys.hood_open < 0.5f) {
        const Vec3 latch_center = boxes.box(IBOX_HOOD_LATCH).center - com;
        if (ray_vs_local_box(local, latch_center, boxes.box(IBOX_HOOD_LATCH).half, &t)) {
            best.consider(t + 0.15f, InteractAction::ToggleHood, latch_center,
                          boxes.box(IBOX_HOOD_LATCH).half, false, "[E] open hood");
        }
    } else {
        const Vec3 raised_center = boxes.box(IBOX_HOOD_RAISED).center - com;
        if (ray_vs_local_box(local, raised_center, boxes.box(IBOX_HOOD_RAISED).half, &t)) {
            best.consider(t + 2.0f, InteractAction::ToggleHood, raised_center,
                          boxes.box(IBOX_HOOD_RAISED).half, false, "[E] close hood");
        }
    }

    if (sys.trunk_open < 0.5f) {
        const Vec3 lid_center = boxes.box(IBOX_TRUNK_LID).center - com;
        if (ray_vs_local_box(local, lid_center, boxes.box(IBOX_TRUNK_LID).half, &t)) {
            best.consider(t, InteractAction::ToggleTrunk, lid_center,
                          boxes.box(IBOX_TRUNK_LID).half, false, "[E] open trunk");
        }
    } else {
        const Vec3 edge_center = boxes.box(IBOX_TRUNK_EDGE).center - com;
        if (ray_vs_local_box(local, edge_center, boxes.box(IBOX_TRUNK_EDGE).half, &t)) {
            if (ctx.materials > 0 && sys.parts[PART_TANK].installed && hands_.kind == ITEM_NONE) {
                FixedString<96> pour;
                pour.format("[E] pour %u materials into tank", ctx.materials);
                best.consider(t, InteractAction::PourMaterials, edge_center, boxes.box(IBOX_TRUNK_EDGE).half,
                              false, pour.view());
            }
            best.consider(t + 0.1f, InteractAction::ToggleTrunk, edge_center,
                          boxes.box(IBOX_TRUNK_EDGE).half, false, "[E] close trunk");
        }

        if (hands_.kind != ITEM_NONE) {
            const Vec3 half = item_cargo_half(hands_.kind);
            const f32 plane_y = kTrunkFloorY + half.y - com.y;
            if (local.dir.y < -0.05f) {
                const f32 hit_t = (plane_y - local.origin.y) / local.dir.y;
                if (hit_t > 0.0f && hit_t < kInteractRange) {
                    const Vec3 hit_cfg = local.origin + local.dir * hit_t + com;
                    if (hit_cfg.z > kTrunkMinZ - 0.25f && hit_cfg.z < kTrunkMaxZ + 0.25f
                        && f_abs(hit_cfg.x) < kTrunkMaxX + 0.25f) {
                        Vec3 place;
                        place.x = f_clamp(hit_cfg.x, kTrunkMinX + half.x,
                                          f_max(kTrunkMaxX - half.x, kTrunkMinX + half.x));
                        place.z = f_clamp(hit_cfg.z, kTrunkMinZ + half.z,
                                          f_max(kTrunkMaxZ - half.z, kTrunkMinZ + half.z));
                        bool fits = false;
                        place.y = sys.cargo_place_y(hands_.kind, place.x, place.z, &fits);
                        if (fits) {
                            const std::string_view iname = item_name(hands_.kind);
                            prompt.format("[E] place %.*s", static_cast<int>(iname.size()),
                                          iname.data());
                            best.consider(hit_t, InteractAction::PlaceCargo, place - com, half,
                                          false, prompt.view());
                            best.place_pos = place;
                        } else {
                            best.consider(hit_t, InteractAction::Info, place - com, half, false,
                                          "no room there");
                        }
                    }
                }
            }
        } else {
            for (u32 i = 0; i < kCargoMax; i++) {
                const CargoItem& c = sys.cargo[i];
                if (!c.used) {
                    continue;
                }
                const Vec3 half = item_cargo_half(c.item.kind);
                const Vec3 center = c.pos - com;
                if (!ray_vs_local_box(local, center, half, &t)) {
                    continue;
                }
                if (c.item.kind == ITEM_FLOPPY) {
                    const std::string_view label = disk_label(c.item.aux);
                    prompt.format("[E] take floppy (%.*s)", static_cast<int>(label.size()),
                                  label.data());
                } else {
                    const std::string_view iname = item_name(c.item.kind);
                    prompt.format("[E] take %.*s (%.0f%%)", static_cast<int>(iname.size()),
                                  iname.data(), static_cast<f64>(c.item.condition * 100.0f));
                }
                if (best.consider(t, InteractAction::TakeCargo, center, half, false,
                                  prompt.view())) {
                    best.cargo = i;
                }
            }
        }
    }

    const Vec3 filler_center = boxes.box(IBOX_FUEL).center - com;
    const Vec3 filler_half = boxes.box(IBOX_FUEL).half;
    if (ray_vs_local_box(local, filler_center, filler_half, &t)) {
        if (hands_.kind == ITEM_JERRYCAN) {
            if (sys.fuel_cap_open) {
                best.consider(t, InteractAction::Refuel, filler_center, filler_half, true,
                              "hold [E] refuel");
            } else {
                best.consider(t, InteractAction::Info, filler_center, filler_half, false,
                              "fuel cap is closed");
            }
        } else {
            best.consider(t, InteractAction::FuelCap, filler_center, filler_half, false,
                          sys.fuel_cap_open ? "[E] close fuel cap" : "[E] open fuel cap");
        }
    }
}

void Interact::resolve_pickups(Candidate& best, const InteractBoxes& boxes,
                               const InteractContext& ctx) const
{
    CarSys& sys = *ctx.sys;
    World& world = *ctx.world;
    FixedString<96> prompt;

    for (const u32 idx : world.entities().live_indices()) {
        Entity* entity = world.entities().at(idx);
        if (!entity || entity->kind != EntityKind::PartPickup) {
            continue;
        }
        const ItemKind pick_kind = static_cast<ItemKind>(entity->aux_kind);
        const bool is_computer = pick_kind == ITEM_COMPUTER;
        const bool is_camera = pick_kind == ITEM_CAMERA;
        const bool is_reel = pick_kind == ITEM_REEL;
        const bool coax_business =
            is_camera
            && (cable_drag_ == static_cast<i32>(CABLE_COAX)
                || (sys.coax_target == kCoaxTargetCamera
                    && sys.cables[CABLE_COAX].state == CableState::Plugged));

        const bool is_printer = pick_kind == ITEM_PRINTER;
        const bool printer_business =
            is_printer && (hands_.kind == ITEM_TANK || cable_drag_ == static_cast<i32>(CABLE_BUS));
        if ((hands_.kind != ITEM_NONE || cable_drag_ >= 0) && pick_kind != ITEM_KEY
            && !(is_computer && (sys.computer_on || hands_.kind == ITEM_FLOPPY))
            && !(is_reel && cable_drag_ >= 0) && !coax_business && !printer_business) {
            continue;
        }

        if (is_printer) {
            const i32 id = static_cast<i32>(entity->aux_data);
            Sphere jack;
            jack.center = entity->pos + rotate(entity->rot, kLoosePrinterJackLocal);
            jack.radius = 0.08f;
            f32 jt = 0.0f;
            if (ray_vs_sphere(ctx.view_ray, jack, kInteractRange, &jt)) {
                if (sys.bus_on_loose_printer(id)) {
                    if (best.consider(jt - 0.20f, InteractAction::CableUnplug, Vec3{}, Vec3{}, false,
                                      "[E] unplug bus cable")) {
                        best.cable = static_cast<i32>(CABLE_BUS);
                    }
                } else if (cable_drag_ == static_cast<i32>(CABLE_BUS)
                           && best.consider(jt - 0.20f, InteractAction::CablePlugLoosePrinter, Vec3{}, Vec3{}, false,
                                            "[E] connect bus to printer")) {
                    best.entity = world.entities().handle_at(idx);
                    best.cable = static_cast<i32>(CABLE_BUS);
                }
            }
        }

        if (is_computer) {
            const Vec3 ports[2] = {kConnectorCoaxLocal, kConnectorBusLocal};
            for (u32 pk = 0; pk < 2; pk++) {
                Sphere port;
                port.center = entity->pos + rotate(entity->rot, ports[pk]);
                port.radius = 0.07f;
                f32 pt = 0.0f;
                if (ray_vs_sphere(ctx.view_ray, port, kInteractRange, &pt)) {
                    consider_cable_root(best, sys, static_cast<CableKind>(pk), pt - 0.50f,
                                        Vec3{}, Vec3{});
                }
            }
            Sphere slot;
            slot.center = entity->pos + rotate(entity->rot, boxes.box(IBOX_DISK_SLOT).center);
            slot.radius = 0.09f;
            f32 st = 0.0f;
            if (ray_vs_sphere(ctx.view_ray, slot, kInteractRange, &st)) {
                consider_disk_slot(best, sys, st - 0.50f, Vec3{}, Vec3{});
            }
        }

        if (is_camera) {
            if (sys.coax_target == kCoaxTargetCamera
                && sys.cables[CABLE_COAX].state == CableState::Plugged) {
                Sphere plug;
                plug.center = entity->pos + rotate(entity->rot, Vec3{-0.13f, 0.01f, 0.016f});
                plug.radius = 0.13f;
                f32 pt = 0.0f;
                if (ray_vs_sphere(ctx.view_ray, plug, kInteractRange, &pt)
                    && best.consider(pt - 0.60f, InteractAction::CableUnplug, Vec3{}, Vec3{},
                                     false, "[E] unplug coax from camera")) {
                    best.cable = static_cast<i32>(CABLE_COAX);
                }
            } else if (cable_drag_ == static_cast<i32>(CABLE_COAX)) {
                Sphere cam;
                cam.center = entity->pos + Vec3{0.0f, 0.06f, 0.0f};
                cam.radius = 0.25f;
                f32 ct = 0.0f;
                if (ray_vs_sphere(ctx.view_ray, cam, kInteractRange, &ct)
                    && best.consider(ct - 0.30f, InteractAction::CablePlugCamera, Vec3{}, Vec3{},
                                     false, "[E] connect coax to camera")) {
                    best.cable = static_cast<i32>(CABLE_COAX);
                }
                continue;
            }
        }

        if (is_reel && cable_drag_ >= 0) {
            const Cable& dragged = sys.cables[cable_drag_];
            Sphere reel;
            reel.center = entity->pos + Vec3{0.0f, 0.10f, 0.0f};
            reel.radius = 0.30f;
            f32 rt = 0.0f;
            if (ray_vs_sphere(ctx.view_ray, reel, kInteractRange, &rt)) {
                const bool other_routed = sys.cables[1 - cable_drag_].via_reel;
                if (dragged.via_reel || other_routed) {
                    best.consider(rt - 0.30f, InteractAction::Info, Vec3{}, Vec3{}, false,
                                  "reel already in use");
                } else {
                    prompt.format("[E] connect %s to reel",
                                  cable_drag_ == static_cast<i32>(CABLE_COAX) ? "coax" : "bus");
                    if (best.consider(rt - 0.30f, InteractAction::CableRouteReel, Vec3{}, Vec3{},
                                      false, prompt.view())) {
                        best.cable = cable_drag_;
                    }
                }
            }
            continue;
        }

        Sphere sphere;
        sphere.center = entity->pos + rotate(entity->rot, item_mesh_center(pick_kind))
                      + Vec3{0.0f, 0.22f, 0.0f};
        sphere.radius = antenna_variant_for_item(pick_kind) >= 0 ? 0.60f : 0.45f * entity->scale;
        f32 t = 0.0f;
        if (!ray_vs_sphere(ctx.view_ray, sphere, kInteractRange, &t)) {
            continue;
        }

        if (is_computer) {
            if (sys.computer_on) {
                const bool can_take = hands_.kind == ITEM_NONE && cable_drag_ < 0;
                if (best.consider(t, InteractAction::TerminalUse, Vec3{}, Vec3{}, can_take,
                                  can_take ? "[E] use terminal | hold [E] take"
                                           : "[E] use terminal")) {
                    best.entity = world.entities().handle_at(idx);
                }
            } else {
                prompt.format("[E] power on | hold [E] take terminal (%.0f%%)",
                              static_cast<f64>(entity->aux_value * 100.0f));
                if (best.consider(t, InteractAction::Pickup, Vec3{}, Vec3{}, true,
                                  prompt.view())) {
                    best.entity = world.entities().handle_at(idx);
                }
            }
            continue;
        }

        if (pick_kind == ITEM_TANK && hands_.kind == ITEM_NONE) {
            const LooseTank* tank = sys.loose_tank(static_cast<i32>(entity->aux_data));
            const u32 doses = tank ? tank->total() : 0;
            if (ctx.materials > 0) {
                prompt.format("tap [E] pour %u materials | hold [E] take tank (%u doses)", ctx.materials, doses);
            } else {
                prompt.format("[E] take tank (%u doses)", doses);
            }
            if (best.consider(t, InteractAction::Pickup, Vec3{}, Vec3{}, ctx.materials > 0, prompt.view())) {
                best.entity = world.entities().handle_at(idx);
            }
            continue;
        }

        if (pick_kind == ITEM_PRINTER) {
            const LoosePrinter* printer = sys.loose_printer(static_cast<i32>(entity->aux_data));
            bool considered = false;
            if (cable_drag_ == static_cast<i32>(CABLE_BUS)) {
                considered = best.consider(t, InteractAction::CablePlugLoosePrinter, Vec3{}, Vec3{}, false,
                                           "[E] connect bus to printer");
                if (considered) {
                    best.cable = static_cast<i32>(CABLE_BUS);
                }
            } else if (hands_.kind == ITEM_TANK) {
                if (printer && printer->has_tank) {
                    best.consider(t, InteractAction::Info, Vec3{}, Vec3{}, false, "printer already has a tank");
                } else {
                    considered = best.consider(t, InteractAction::PlaceTankOnPrinter, Vec3{}, Vec3{}, false,
                                               "[E] put tank on printer");
                }
            } else if (hands_.kind != ITEM_NONE) {
                continue;
            } else if (printer && printer->bay.tray_total() > 0) {
                prompt.format("[E] take %u rounds from the printer", printer->bay.tray_total());
                considered = best.consider(t, InteractAction::TakeRounds, Vec3{}, Vec3{}, false, prompt.view());
            } else if (printer && printer->has_tank) {
                if (ctx.materials > 0) {
                    prompt.format("tap [E] pour %u materials | hold [E] take tank (%u doses)", ctx.materials,
                                  printer->bay.tank_total());
                } else {
                    prompt.format("hold [E] take tank off the printer (%u doses)", printer->bay.tank_total());
                }
                considered = best.consider(t, InteractAction::TakeTankOffPrinter, Vec3{}, Vec3{}, true, prompt.view());
            } else {
                const std::string_view iname = item_name(pick_kind);
                prompt.format("[E] take %.*s (%.0f%%)", static_cast<int>(iname.size()), iname.data(),
                              static_cast<f64>(entity->aux_value * 100.0f));
                considered = best.consider(t, InteractAction::Pickup, Vec3{}, Vec3{}, false, prompt.view());
            }
            if (considered) {
                best.entity = world.entities().handle_at(idx);
            }
            continue;
        }

        if (pick_kind == ITEM_FLOPPY) {
            const std::string_view label = disk_label(static_cast<i32>(entity->aux_data));
            prompt.format("[E] take floppy (%.*s)", static_cast<int>(label.size()),
                          label.data());
        } else if (pick_kind == ITEM_CASSETTE) {
            const std::string_view label =
                ctx.tapes ? ctx.tapes->label(static_cast<i32>(entity->aux_data))
                          : std::string_view("TAPE");
            prompt.format("[E] take cassette (%.*s)", static_cast<int>(label.size()),
                          label.data());
        } else {
            const std::string_view iname = item_name(pick_kind);
            prompt.format("[E] take %.*s (%.0f%%)", static_cast<int>(iname.size()), iname.data(),
                          static_cast<f64>(entity->aux_value * 100.0f));
        }
        if (best.consider(t, InteractAction::Pickup, Vec3{}, Vec3{}, false, prompt.view())) {
            best.entity = world.entities().handle_at(idx);
        }
    }
}

void Interact::resolve_tower_port(Candidate& best, const InteractContext& ctx) const
{
    if (!tower_present_) {
        return;
    }
    Sphere port;
    port.center = tower_port_;
    port.radius = 0.55f;
    f32 t = 0.0f;
    if (!ray_vs_sphere(ctx.view_ray, port, kInteractRange, &t)) {
        return;
    }

    CarSys& sys = *ctx.sys;
    const Cable& bus = sys.cables[CABLE_BUS];
    if (sys.bus_target == kBusTargetTower && bus.state == CableState::Plugged) {
        if (best.consider(t - 0.30f, InteractAction::CableUnplug, Vec3{}, Vec3{}, false,
                          "[E] unplug bus from relay port")) {
            best.cable = static_cast<i32>(CABLE_BUS);
        }
    } else if (cable_drag_ == static_cast<i32>(CABLE_BUS)) {
        if (best.consider(t - 0.30f, InteractAction::CablePlugTower, Vec3{}, Vec3{}, false,
                          "[E] connect bus to relay port")) {
            best.cable = static_cast<i32>(CABLE_BUS);
        }
    } else if (cable_drag_ < 0 && hands_.kind == ITEM_NONE) {
        best.consider(t - 0.30f, InteractAction::Info, Vec3{}, Vec3{}, false,
                      "relay service port");
    }
}

void Interact::update(const InteractBoxes& boxes, const InteractContext& ctx, f32 dt)
{
    Candidate best;

    if (ctx.player->state() == PlayerState::OnFoot) {
        resolve_car_targets(best, boxes, ctx);
        resolve_pickups(best, boxes, ctx);
        resolve_tower_port(best, ctx);
    } else if (ctx.player->state() == PlayerState::Driving) {
        resolve_in_car(best, boxes, ctx);
    }
    if (ctx.gun_drawn && (best.action == InteractAction::Pickup || best.action == InteractAction::RemovePart
                          || best.action == InteractAction::TakeCargo)) {
        best.action = InteractAction::Info;
        best.is_hold = false;
        best.prompt.assign("[E] holster and take");
        holster_request_ = holster_request_ || ctx.e_pressed;
    }

    const bool same_target = best.action == action_ && best.part == target_part_
                          && best.entity.idx == target_entity_.idx && best.side == target_side_
                          && best.cargo == target_cargo_;

    action_ = best.action;
    target_part_ = best.part;
    target_entity_ = best.entity;
    target_side_ = best.side;
    target_cargo_ = best.cargo;
    target_cable_ = best.cable;
    target_center_ = best.center;
    target_half_ = best.half;
    place_pos_ = best.place_pos;
    action_is_hold_ = best.is_hold;
    prompt_ = best.prompt;

    if (!same_target) {
        hold_time_ = 0.0f;
        press_latch_ = false;
    }

    if (best.action == InteractAction::Crank) {
        if (ctx.e_pressed) {
            crank_latch_ = true;
        }
        if (!ctx.e_down) {
            crank_latch_ = false;
        }
    } else {
        crank_latch_ = false;
    }
    if (crank_latch_ && !ctx.preview) {
        ctx.sys->crank_request = true;
    }

    if (ctx.e_pressed) {
        press_latch_ = true;
    }

    if (best.action == InteractAction::TerminalUse) {
        if (best.is_hold) {
            if (ctx.e_down) {
                if (press_latch_) {
                    hold_time_ += dt;
                    if (hold_time_ >= kInteractHoldTime) {
                        if (!ctx.preview) {
                            perform(ctx);
                        }
                        hold_time_ = 0.0f;
                        press_latch_ = false;
                    }
                }
            } else {
                if (press_latch_ && hold_time_ > 0.0f) {
                    use_terminal_request_ = true;
                }
                hold_time_ = 0.0f;
                press_latch_ = false;
            }
            hold_progress_ = hold_time_ / kInteractHoldTime;
        } else {
            hold_time_ = 0.0f;
            hold_progress_ = 0.0f;
            if (ctx.e_pressed) {
                use_terminal_request_ = true;
                press_latch_ = false;
            }
        }
        return;
    }

    if (best.action == InteractAction::TapeEject && best.is_hold) {
        if (!ctx.e_down) {
            if (press_latch_ && hold_time_ > 0.0f && !ctx.preview) {
                ctx.sys->deck_play = !ctx.sys->deck_play;
            }
            press_latch_ = false;
            hold_time_ = 0.0f;
        } else if (press_latch_) {
            hold_time_ += dt;
            if (hold_time_ >= kInteractHoldTime) {
                if (!ctx.preview) {
                    perform(ctx);
                }
                hold_time_ = 0.0f;
                press_latch_ = false;
            }
        }
        hold_progress_ = hold_time_ / kInteractHoldTime;
        return;
    }

    if (best.action == InteractAction::None || best.action == InteractAction::Info
        || best.action == InteractAction::EnterCar || best.action == InteractAction::ExitCar
        || best.action == InteractAction::Crank) {
        if (ctx.e_pressed && best.action == InteractAction::None
            && hands_.kind == ITEM_COMPUTER && ctx.sys->computer_on) {
            use_terminal_request_ = true;
            press_latch_ = false;
        }
        hold_time_ = 0.0f;
        hold_progress_ = 0.0f;
        return;
    }

    if (best.is_hold) {
        if (ctx.e_down) {
            if (press_latch_) {
                hold_time_ += dt;
                if (hold_time_ >= kInteractHoldTime) {
                    if (!ctx.preview) {
                        perform(ctx);
                    }
                    hold_time_ = 0.0f;
                    press_latch_ = false;
                }
            }
        } else {
            if (press_latch_ && hold_time_ > 0.0f && best.action == InteractAction::Pickup && !ctx.preview) {
                const Entity* entity = ctx.world->entity(best.entity);
                if (entity && static_cast<ItemKind>(entity->aux_kind) == ITEM_COMPUTER) {
                    ctx.sys->computer_on = true;
                }
            }
            if (press_latch_ && hold_time_ > 0.0f && best.action == InteractAction::RemovePart
                && best.part == PART_COMPUTER && !ctx.preview) {
                ctx.sys->computer_on = !ctx.sys->computer_on;
            }
            if (press_latch_ && hold_time_ > 0.0f && ctx.materials > 0 && !ctx.preview) {
                Entity* entity = ctx.world->entity(best.entity);
                const ItemKind kind = entity ? static_cast<ItemKind>(entity->aux_kind) : ITEM_NONE;
                if (best.action == InteractAction::RemovePart && best.part == PART_TANK) {
                    pour_target_ = 0;
                    pour_request_ = true;
                } else if (best.action == InteractAction::Pickup && kind == ITEM_TANK) {
                    i32 id = static_cast<i32>(entity->aux_data);
                    if (!ctx.sys->loose_tank(id)) {
                        id = ctx.sys->claim_tank();
                        entity->aux_data = static_cast<u32>(id);
                    }
                    if (id != 0) {
                        pour_target_ = id;
                        pour_request_ = true;
                    }
                } else if (best.action == InteractAction::TakeTankOffPrinter && kind == ITEM_PRINTER) {
                    pour_target_ = -static_cast<i32>(entity->aux_data);
                    pour_request_ = true;
                }
            }
            hold_time_ = 0.0f;
            press_latch_ = false;
        }
        hold_progress_ = hold_time_ / kInteractHoldTime;
    } else {
        hold_progress_ = 0.0f;
        if (ctx.e_pressed && press_latch_) {
            if (!ctx.preview) {
                perform(ctx);
            }
            press_latch_ = false;
        }
    }
}

bool Interact::drop(World& world, PhysWorld& phys, Vec3 origin, Vec3 dir, f32 power)
{
    if (hands_.kind == ITEM_NONE) {
        return false;
    }
    const Vec3 spot = origin + dir * 0.8f;
    const Vec3 vel = dir * (1.2f + 8.5f * power) + Vec3{0.0f, 0.5f + 1.8f * power, 0.0f};
    interact_spawn_pickup(world, phys, hands_, spot, std::atan2(dir.x, -dir.z), vel);
    hands_.kind = ITEM_NONE;
    return true;
}

}
