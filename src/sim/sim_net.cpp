#include "sim/sim.h"
#include "world/scenes.h"
#include "world/pickup_body.h"
#include "core/log.h"
#include "math/glm_bridge.h"
#include "world/destination.h"

#include <cstring>
#include <variant>

namespace anom {
void Sim::client_reset(PlayerId local, Vec3 feet, f32 yaw, std::string_view name)
{
    for (PlayerSlot& s : slots_) {
        if (s.active) {
            jolt_.characterDestroy(static_cast<int>(slot_index(s.id)));
        }
        const PlayerId id = s.id;
        s = PlayerSlot{};
        s.id = id;
    }
    role_ = SimRole::Client;
    local_ = local;
    host_zone_serial_ = 0;
    seat_owners_[0] = kNoPlayer;
    seat_owners_[1] = kNoPlayer;
    mirror_driver_ = kNoPlayer;
    terminal_user_ = kNoPlayer;
    snapshots_.clear();
    snapshot_tick_.reset();
    host_events_.clear();
    term_mirrors_.clear();
    roster_.replace({});

    PlayerSlot& s = slots_[slot_index(local)];
    s.active = true;
    s.name.assign(name);
    s.color = kSlotColors[local];
    s.interact.init();
    kit_gun(s.gun);
    place_player(s, feet, yaw);
    commands_[slot_index(local)] = PlayerCommand{};
    roster_.add(local);
}

PlayerId Sim::become_solo(PlayerId local)
{
    PlayerSlot kept = slots_[slot_index(local)];
    for (PlayerSlot& s : slots_) {
        if (s.active) {
            jolt_.characterDestroy(static_cast<int>(slot_index(s.id)));
        }
        const PlayerId id = s.id;
        s = PlayerSlot{};
        s.id = id;
    }
    role_ = SimRole::Solo;
    local_ = 0;
    seat_owners_[0] = kNoPlayer;
    seat_owners_[1] = kNoPlayer;
    mirror_driver_ = kNoPlayer;
    terminal_user_ = kNoPlayer;
    snapshots_.clear();
    snapshot_tick_.reset();
    host_events_.clear();
    term_mirrors_.clear();
    mirrored_.clear();
    terminal_.power(false);
    roster_.replace({});

    PlayerSlot& s = slots_[0];
    s.active = true;
    s.name = kept.name;
    s.color = kSlotColors[0];
    s.interact = kept.interact;
    s.view_origin = kept.view_origin;
    s.view_dir = kept.view_dir;
    const Player& body = kept.player;
    place_player(s, body.state() == PlayerState::OnFoot ? body.pos() : spawn_point(0), body.yaw());
    commands_[0] = PlayerCommand{};
    roster_.add(0);
    return 0;
}

bool Sim::queue_snapshot(WorldSnapshot&& snapshot)
{
    const u32 tick = snapshot.header.tick;
    if (snapshot_tick_ && snapshot.header.zone_serial == snapshot_zone_ && tick <= *snapshot_tick_
        && *snapshot_tick_ - tick < kStaleSnapshotTicks) {
        return false;
    }
    snapshot_tick_ = tick;
    snapshot_zone_ = snapshot.header.zone_serial;
    snapshots_.clear();
    snapshots_.push_back(std::move(snapshot));
    return true;
}

void Sim::queue_term_mirror(const TermMirror& mirror)
{
    term_mirrors_.clear();
    term_mirrors_.push_back(mirror);
}

void Sim::make_snapshot(WorldSnapshot& out) const
{
    WorldHeader& h = out.header;
    h.tick = static_cast<u32>(tick_count_);
    h.zone_serial = zone_serial_;
    h.time_of_day = time_of_day_;
    h.weather = weather_;
    h.carsys = carsys_;
    h.car = vehicle_.wire(phys_);
    h.terminal_user = terminal_user_;
    h.driver = driver();
    h.travel_charge = travel_charge_;
    h.travel_primed = travel_primed_;
    h.travel_jump = travel_jump_;
    h.travel_target = travel_target_;
    h.tower_breached = tower_breached_;
    h.tower_pos = tower_pos_;
    h.tower_rot = tower_rot_;

    out.players.clear();
    for (const PlayerSlot& s : slots_) {
        if (!s.active) {
            continue;
        }
        SlotWire wire;
        wire.id = s.id;
        wire.dummy = s.dummy;
        wire.use_down = s.use_down;
        std::snprintf(wire.name, sizeof(wire.name), "%s", s.name.c_str());
        wire.color = s.color;
        wire.cowboy = s.cowboy;
        wire.player = s.player;
        wire.interact = s.interact;
        wire.gun = gun_view(s.id, 1.0f);
        out.players.push_back(wire);
    }
    out.roster = roster_.entries();
    gameplay_.snapshot(out.gameplay);

    out.pickups.clear();
    const Pool<Entity>& pool = world_.entities();
    for (u32 idx : pool.live_indices()) {
        const Entity* e = pool.at(idx);
        if (!e || e->kind != EntityKind::PartPickup) {
            continue;
        }
        PickupWire wire;
        const EntityHandle handle = pool.handle_at(idx);
        wire.idx = handle.idx;
        wire.gen = handle.gen;
        wire.item.kind = static_cast<ItemKind>(e->aux_kind);
        wire.item.condition = e->aux_value;
        wire.item.aux = static_cast<i32>(e->aux_data);
        PickupState body;
        if (pickup_body_state(phys_, *e, body)) {
            wire.has_body = true;
            wire.pos = body.pos;
            wire.rot = body.rot;
            wire.vel = body.vel;
            wire.angular_vel = body.angular_vel;
        } else {
            wire.pos = e->pos;
            wire.rot = e->rot;
        }
        out.pickups.push_back(wire);
    }
}

void Sim::sync_remote_character(PlayerSlot& s)
{
    const Player& p = s.player;
    const bool on_foot = p.state() == PlayerState::OnFoot;
    const MoveState& m = p.movement().state();
    const Vec3 up = p.up();
    if (!jolt_.characterValid(static_cast<int>(slot_index(s.id)))) {
        jolt_.characterCreate(static_cast<int>(slot_index(s.id)), to_glm(m.pos), to_glm(up), p.movement().tuning().radius, m.height);
    } else {
        jolt_.characterSetHeight(static_cast<int>(slot_index(s.id)), m.height, to_glm(up));
        jolt_.characterTeleport(static_cast<int>(slot_index(s.id)), to_glm(m.pos), to_glm(up));
    }
    jolt_.characterSetSolid(static_cast<int>(slot_index(s.id)), on_foot);
}

void Sim::adopt_remote_bodies(std::span<const SlotCommand> commands)
{
    for (const SlotCommand& in : commands) {
        PlayerSlot* s = slot(in.id);
        if (!s || !s->remote) {
            continue;
        }
        if (in.has_body) {
            s->player.adopt(in.body);
            sync_remote_character(*s);
        }
        if (in.has_gun) {
            s->gun.shown = in.gun;
        }
        if (in.has_car && driver() == in.id) {
            vehicle_.adopt(phys_, in.car);
        }
    }
}

void Sim::arbitrate_seat()
{
    for (u32 k = 0; k < kMaxSeats; k++) {
        const PlayerSlot* owner = slot(seat_owners_[k]);
        if (!owner || owner->player.state() == PlayerState::OnFoot || owner->player.seat() != k) {
            seat_owners_[k] = kNoPlayer;
        }
    }
    for (PlayerSlot& s : slots_) {
        if (!s.active || s.player.state() == PlayerState::OnFoot) {
            continue;
        }
        const u32 k = s.player.seat();
        const bool refused = k >= kMaxSeats || seat_blocked(k) || (seat_owners_[k] != kNoPlayer && seat_owners_[k] != s.id);
        if (!refused) {
            seat_owners_[k] = s.id;
            continue;
        }
        if (s.remote) {
            emit(ghost::game::SeatRefused{s.id});
        } else {
            s.player.eject(phys_, vehicle_);
        }
    }
}

void Sim::adopt_pickups(const std::vector<PickupWire>& pickups)
{
    Pool<Entity>& pool = world_.entities();
    const auto find_wire = [&](u32 idx, u32 gen) -> const PickupWire* {
        for (const PickupWire& w : pickups) {
            if (w.idx == idx && w.gen == gen) {
                return &w;
            }
        }
        return nullptr;
    };
    const auto despawn = [&](EntityHandle handle) {
        if (Entity* e = world_.entity(handle)) {
            pickup_body_destroy(phys_, *e);
            world_.despawn(handle);
        }
    };

    for (size_t i = 0; i < mirrored_.size();) {
        if (!find_wire(mirrored_[i].idx, mirrored_[i].gen) || !world_.entity(mirrored_[i].local)) {
            despawn(mirrored_[i].local);
            mirrored_[i] = mirrored_.back();
            mirrored_.pop_back();
        } else {
            i++;
        }
    }
    for (u32 idx : pool.live_indices()) {
        const Entity* e = pool.at(idx);
        if (!e || e->kind != EntityKind::PartPickup) {
            continue;
        }
        const EntityHandle handle = pool.handle_at(idx);
        bool known = false;
        for (const Mirrored& m : mirrored_) {
            known = known || m.local == handle;
        }
        if (!known) {
            despawn(handle);
        }
    }

    for (const PickupWire& w : pickups) {
        EntityHandle local;
        for (const Mirrored& m : mirrored_) {
            if (m.idx == w.idx && m.gen == w.gen) {
                local = m.local;
            }
        }
        if (!local.valid()) {
            local = interact_spawn_pickup(world_, phys_, w.item, w.pos, 0.0f, w.vel);
            mirrored_.push_back(Mirrored{w.idx, w.gen, local});
        }
        Entity* e = world_.entity(local);
        if (!e) {
            continue;
        }
        e->aux_value = w.item.condition;
        e->aux_data = static_cast<u32>(w.item.aux);
        if (pickup_has_body(phys_, *e)) {
            PickupState body;
            body.pos = w.pos;
            body.rot = w.rot;
            body.vel = w.vel;
            body.angular_vel = w.angular_vel;
            body.active = true;
            pickup_set_state(phys_, *e, body);
        } else {
            e->pos = w.pos;
            e->rot = w.rot;
        }
    }
}

void Sim::apply_snapshot(const WorldSnapshot& snap)
{
    const WorldHeader& h = snap.header;
    time_of_day_ = h.time_of_day;
    weather_ = h.weather;
    carsys_ = h.carsys;
    mirror_driver_ = h.driver;
    const bool new_zone = h.zone_serial != host_zone_serial_;
    host_zone_serial_ = h.zone_serial;
    if (h.driver != local_ || new_zone) {
        vehicle_.adopt(phys_, h.car);
    }
    terminal_user_ = h.terminal_user;
    travel_charge_ = h.travel_charge;
    travel_primed_ = h.travel_primed;
    travel_jump_ = h.travel_jump;
    travel_target_ = h.travel_target;
    tower_breached_ = h.tower_breached;
    tower_pos_ = h.tower_pos;
    tower_rot_ = h.tower_rot;

    for (PlayerSlot& s : slots_) {
        if (!s.active || s.id == local_) {
            continue;
        }
        bool present = false;
        for (const SlotWire& w : snap.players) {
            present = present || w.id == s.id;
        }
        if (!present) {
            jolt_.characterDestroy(static_cast<int>(slot_index(s.id)));
            const PlayerId id = s.id;
            s = PlayerSlot{};
            s.id = id;
        }
    }
    for (const SlotWire& w : snap.players) {
        if (slot_index(w.id) >= kMaxSlots) {
            continue;
        }
        PlayerSlot& s = slots_[slot_index(w.id)];
        s.name.assign(std::string_view(w.name, strnlen(w.name, sizeof(w.name))));
        s.color = w.color;
        s.dummy = w.dummy;
        s.cowboy = w.cowboy;
        if (w.id == local_) {
            s.interact.adopt_holdings(w.interact);
            continue;
        }
        if (!s.active) {
            const PlayerId id = s.id;
            s = PlayerSlot{};
            s.id = id;
            s.active = true;
            s.name.assign(std::string_view(w.name, strnlen(w.name, sizeof(w.name))));
            s.color = w.color;
            s.dummy = w.dummy;
            s.cowboy = w.cowboy;
            s.player.set_character(slot_index(id));
        }
        s.remote = true;
        s.use_down = w.use_down;
        s.interact = w.interact;
        s.gun.shown = w.gun;
        s.player.adopt(w.player);
        s.view_origin = s.player.pos() + s.player.up() * s.player.movement().state().eye_height;
        sync_remote_character(s);
    }

    roster_.replace(snap.roster);
    gameplay_.applySnapshot(snap.gameplay);
    if (const ghost::game::RosterEntry* mine = roster_.find(local_)) {
        slots_[slot_index(local_)].player.movement().set_vitals(mine->health, mine->sinceHurt, mine->downed);
    }
    adopt_pickups(snap.pickups);
}

void Sim::apply_host_events()
{
    for (const ghost::game::GameEvent& event : host_events_) {
        if (const auto* zone = std::get_if<ghost::game::ZoneLoaded>(&event)) {
            if (zone->scene >= 0 && static_cast<u32>(zone->scene) < scenes().size()) {
                load_zone(scenes()[static_cast<u32>(zone->scene)].zone_dir);
            }
        } else if (const auto* back = std::get_if<ghost::game::PlayerRespawned>(&event)) {
            respawn_owned(back->player);
        } else if (const auto* placed = std::get_if<ghost::game::PlayerPlaced>(&event)) {
            if (placed->player == local_) {
                place_player(slots_[slot_index(local_)], from_glm(placed->position), placed->yaw);
            }
        } else if (const auto* refused = std::get_if<ghost::game::SeatRefused>(&event)) {
            if (refused->player == local_) {
                slots_[slot_index(local_)].player.eject(phys_, vehicle_);
            }
        }
    }
}

void Sim::advance_client(f32 dt)
{
    if (travel_jump_ >= 0.0f) {
        travel_jump_ += dt;
    }
}

}
