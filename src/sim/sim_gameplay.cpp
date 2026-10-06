#include "sim/sim.h"
#include "core/log.h"
#include "math/glm_bridge.h"
#include "world/pickup_body.h"

#include <algorithm>
#include <cmath>
#include <variant>

namespace anom {
namespace {

constexpr f32 kHipSpreadDeg = 0.5f;
constexpr f32 kAimedSpreadDeg = 0.15f;
constexpr f32 kCrouchSpreadScale = 0.6f;
constexpr f32 kBlinkClearance = 0.4f;
constexpr f32 kMuzzleAhead = 0.6f;
constexpr f32 kZoneGhostSpread = 1.5f;
constexpr f32 kWindowTop = 0.6f;
constexpr f32 kWindowBottom = -0.55f;
constexpr f32 kWindowClearance = 0.12f;

ghost::game::PlayerState world_ghost_state(const Player& p)
{
    const MoveState& m = p.movement().state();
    ghost::game::PlayerState s = ghost_state(m);
    s.position = to_glm(p.pos());
    s.velocity = to_glm(p.vel());
    const Vec3 forward = frame_forward(m.frame, m.yaw);
    s.yaw = std::atan2(forward.x, -forward.z);
    return s;
}

}

Vec3 Sim::eye_of(const PlayerSlot& s) const
{
    const Player& p = s.player;
    return p.pos() + p.up() * p.movement().state().eye_height;
}

ghost::game::MechanismView Sim::gun_view(PlayerId id, f32 alpha) const
{
    const PlayerSlot* s = slot(id);
    if (!s) {
        return {};
    }
    if (s->remote) {
        return s->gun.shown;
    }
    return gameplay_.viewOf(s->gun, alpha, static_cast<f32>(gameplay_.simTime()));
}

void Sim::queue_remote_rounds(std::vector<ghost::game::net::RoundWire> rounds)
{
    remote_rounds_.insert(remote_rounds_.end(), rounds.begin(), rounds.end());
}

void Sim::gather_world_for_gameplay()
{
    std::vector<ghost::game::PlayerBody> bodies;
    std::vector<PlayerId> interacting;
    for (const PlayerSlot& s : slots_) {
        if (!s.active) {
            continue;
        }
        const Player& p = s.player;
        const MoveState& m = p.movement().state();
        ghost::game::PlayerBody b;
        b.id = s.id;
        b.feet = to_glm(p.pos());
        b.up = to_glm(p.up());
        b.radius = p.movement().tuning().radius;
        b.height = m.height;
        b.eye = to_glm(eye_of(s));
        b.viewDirection = to_glm(s.view_dir);
        b.hidden = m.shroud_time > 0.0f;
        b.visibility = rules_.visibility(m.stance);
        b.downed = roster_.downed(s.id);
        b.zombie = s.zombie_of != kNoPlayer;
        if (const ghost::game::RosterEntry* entry = roster_.find(s.id)) {
            b.zombie = b.zombie || entry->zombie;
            b.possessed = entry->possessedBy != kNoPlayer;
        }
        bodies.push_back(b);
        if (s.use_down && p.state() == PlayerState::OnFoot && s.interact.action() == InteractAction::None) {
            interacting.push_back(s.id);
        }
    }
    gameplay_.setBodies(std::move(bodies));
    gameplay_.setInteracting(std::move(interacting));

    std::vector<ghost::game::LooseBody> loose;
    for (u32 idx : world_.entities().live_indices()) {
        const Entity* e = world_.entities().at(idx);
        if (!e || e->kind != EntityKind::PartPickup || e->body == kNoEntityBody) {
            continue;
        }
        const ItemKind kind = static_cast<ItemKind>(e->aux_kind);
        loose.push_back({e->body, to_glm(item_cargo_half(kind)), f_max(item_mass(kind), 1.0f)});
    }
    gameplay_.setLoose(std::move(loose));
    gameplay_.setHost(role_ != SimRole::Client);
}

void Sim::apply_inbound()
{
    if (role_ == SimRole::Client) {
        PlayerSlot* mine = slot(local_);
        for (const glm::vec3& push : impulses_in_) {
            if (mine && mine->player.state() == PlayerState::OnFoot) {
                mine->player.movement().add_impulse(from_glm(push));
            }
        }
        for (const ghost::game::Round& round : gives_in_) {
            if (mine) {
                mine->gun.pouch.add(round.element);
                emit(ghost::game::RoundPickedUp{});
            }
        }
        for (const ghost::game::Projectile& projectile : projectiles_in_) {
            if (projectile.round.element < gameplay_.ammo().elements.size()) {
                gameplay_.ballistics().adopt(projectile);
            }
        }
    } else {
        for (const RemoteShot& in : remote_shots_) {
            const ghost::game::net::ShotMsg& shot = in.shot;
            if (!slot(in.id) || roster_.downed(in.id) || shot.round.element >= gameplay_.ammo().elements.size()) {
                continue;
            }
            emit(ghost::game::ShotFired{shot.round, 0, in.id});
            const PlayerSlot& shooter = *slot(in.id);
            const glm::vec3 inherit = seated_armed(shooter) ? to_glm(shooter_velocity(shooter)) : glm::vec3(0.0f);
            gameplay_.fireShot(in.id, shot.origin, shot.forward, shot.spreadDeg, shot.round, false, events_, inherit);
        }
        gameplay_.adoptRounds(remote_rounds_);
    }
    impulses_in_.clear();
    gives_in_.clear();
    projectiles_in_.clear();
    remote_shots_.clear();
    remote_rounds_.clear();
}

void Sim::tick_guns(f32 dt)
{
    if (spawn_.scene.kind != SceneKind::Zone) {
        for (PlayerSlot& s : slots_) {
            if (s.active && owns(s) && s.zombie_of == kNoPlayer) {
                tick_bench(s, dt);
            }
        }
    }
    for (PlayerSlot& s : slots_) {
        if (!s.active || s.remote || s.zombie_of != kNoPlayer) {
            continue;
        }
        const PlayerCommand& cmd = commands_[slot_index(s.id)];
        const MoveState& m = s.player.movement().state();
        ghost::game::GunCommand gc;
        gc.trigger = cmd.gameplay && cmd.trigger;
        gc.cock = cmd.cock;
        gc.toggleCylinder = cmd.cylinder;
        gc.closeCylinder = cmd.close_cylinder;
        gc.eject = cmd.eject;
        gc.speedload = cmd.speedload;
        gc.turn = cmd.turn;
        if (cmd.load_element >= 0) {
            gc.load = true;
            gc.loadElement = static_cast<ghost::game::ElementId>(cmd.load_element);
        }
        if (cmd.quick_fill_element >= 0) {
            gc.quickFill = true;
            gc.quickFillElement = static_cast<ghost::game::ElementId>(cmd.quick_fill_element);
        }
        gc.muzzle = to_glm(cmd.muzzle);
        gc.barrelDirection = to_glm(cmd.barrel_dir);
        const bool armed_here = s.player.state() == PlayerState::OnFoot || seated_armed(s);
        const bool holstered = !armed_here || m.holstered || m.holster > 0.0f;
        s.window_blocked = false;
        if (seated_armed(s) && !holstered) {
            const Vec3 aim = length_sq(cmd.barrel_dir) > 1e-6f ? normalize(cmd.barrel_dir) : s.view_dir;
            s.window_blocked = !aims_out_window(s, aim);
            if (s.window_blocked) {
                gc.trigger = false;
            }
        }
        std::vector<ghost::game::ShotFired> shots;
        std::vector<ghost::game::ChambersEjected> ejected;
        gameplay_.tickGun(s.id, s.gun, gc, holstered, roster_.downed(s.id), dt, events_, shots, ejected);
        for (const ghost::game::ShotFired& shot : shots) {
            spawn_shot(s, shot);
        }
        for (const ghost::game::ChambersEjected& out : ejected) {
            drop_ejected(s, out);
        }
    }
}

void Sim::spawn_shot(PlayerSlot& s, const ghost::game::ShotFired& shot)
{
    const PlayerCommand& cmd = commands_[slot_index(s.id)];
    const MoveState& m = s.player.movement().state();
    const Vec3 eye = eye_of(s);
    Vec3 forward = length_sq(cmd.barrel_dir) > 1e-6f ? normalize(cmd.barrel_dir) : s.view_dir;
    Vec3 origin = length_sq(cmd.muzzle) > 1e-6f ? cmd.muzzle : eye + forward * kMuzzleAhead;
    const bool seated = seated_armed(s);
    if (seated) {
        origin = window_origin(s, origin, forward);
    } else if (const auto blocked = jolt_.raycast(to_glm(eye), to_glm(origin))) {
        origin = lerp(eye, from_glm(blocked->point), 0.9f);
    }
    const glm::vec3 inherit = seated ? to_glm(shooter_velocity(s)) : glm::vec3(0.0f);
    f32 spread = m.aiming ? kAimedSpreadDeg : kHipSpreadDeg;
    if (m.stance == Stance::Crouch) {
        spread *= kCrouchSpreadScale;
    }
    const ghost::game::ElementDef& def = gameplay_.ammo().elements[shot.round.element];
    if (role_ == SimRole::Client) {
        ghost::game::net::ShotMsg out;
        out.origin = to_glm(origin);
        out.forward = to_glm(forward);
        out.spreadDeg = spread;
        out.round = shot.round;
        shots_out_.push_back(out);
        if (def.self != ghost::game::SelfEffect::None) {
            gameplay_.fireShot(s.id, to_glm(origin), to_glm(forward), spread, shot.round, true, events_, inherit);
        }
        return;
    }
    gameplay_.fireShot(s.id, to_glm(origin), to_glm(forward), spread, shot.round, true, events_, inherit);
}

void Sim::drop_ejected(PlayerSlot& s, const ghost::game::ChambersEjected& ejected)
{
    const Player& p = s.player;
    const MoveState& m = p.movement().state();
    const Vec3 forward = frame_forward(m.frame, m.yaw);
    const Vec3 right = frame_right(m.frame, m.yaw);
    const Vec3 origin = eye_of(s) - p.up() * 0.4f + forward * 0.35f - right * 0.08f;
    std::vector<ghost::game::net::RoundWire> made =
        gameplay_.dropRounds(ejected, to_glm(origin), to_glm(forward), to_glm(right));
    if (role_ == SimRole::Client) {
        rounds_out_.insert(rounds_out_.end(), made.begin(), made.end());
    }
}

void Sim::push_player(PlayerId id, const glm::vec3& delta_v)
{
    PlayerSlot* s = slot(id);
    if (!s) {
        return;
    }
    if (s->remote) {
        if (role_ != SimRole::Client) {
            impulses_out_.push_back({id, delta_v});
        }
        return;
    }
    if (s->player.state() == PlayerState::OnFoot) {
        s->player.movement().add_impulse(from_glm(delta_v));
    }
}

glm::vec3 Sim::self_cast(PlayerId id, const ghost::game::ElementDef& def, const glm::vec3& direction)
{
    PlayerSlot* s = slot(id);
    if (!s) {
        return glm::vec3(0.0f);
    }
    Movement& move = s->player.movement();
    const Vec3 feet = s->player.pos();
    if (def.self == ghost::game::SelfEffect::Haste) {
        move.apply_haste(def.selfDuration, def.selfSpeedScale);
    } else if (def.self == ghost::game::SelfEffect::Shroud) {
        move.apply_shroud(def.selfDuration);
    } else if (def.self == ghost::game::SelfEffect::Blink && s->player.state() == PlayerState::OnFoot) {
        const Vec3 up = s->player.up();
        const Vec3 eye_offset = up * move.state().eye_height;
        const Vec3 eye = feet + eye_offset;
        const Vec3 dir = from_glm(direction);
        f32 distance = def.selfDistance;
        if (const auto hit = jolt_.raycast(to_glm(eye), to_glm(eye + dir * (distance + kBlinkClearance)))) {
            distance = f_max(0.0f, length(from_glm(hit->point) - eye) - kBlinkClearance);
        }
        Vec3 arrive = eye + dir * distance - eye_offset;
        if (const auto floor = jolt_.raycast(to_glm(arrive + eye_offset), to_glm(arrive - up * 0.05f))) {
            arrive = from_glm(floor->point);
        }
        move.teleport(arrive);
        return to_glm(arrive);
    }
    return to_glm(feet);
}

PlayerId Sim::raise_zombie(PlayerId player)
{
    const ghost::game::NecromiteParams* rules = gameplay_.necromiteRules();
    const PlayerSlot* owner = slot(player);
    if (!rules || !owner || player >= kMaxPlayers) {
        return kNoPlayer;
    }
    const PlayerId zombie = static_cast<PlayerId>(kZombieIdBase + player);
    if (!roster_.possess(player, zombie, rules->zombie.health)) {
        return kNoPlayer;
    }
    PlayerSlot& z = slots_[slot_index(zombie)];
    const PlayerSlot copy = *owner;
    z = PlayerSlot{};
    z.id = zombie;
    z.active = true;
    z.name = copy.name;
    z.color = copy.color;
    z.zombie_of = player;
    z.gun = copy.gun;
    z.mind.seed = gameplay_.nextRandom() * 10.0f;
    z.interact.init();
    place_player(z, copy.player.pos(), copy.player.yaw());
    z.player.movement().set_vitals(rules->zombie.health, 1e3f, false);
    log_info("sim: %s rises", z.name.c_str());
    return zombie;
}

void Sim::fell_zombie(PlayerId zombie)
{
    PlayerSlot* z = slot(zombie);
    const PlayerId player = roster_.madeOf(zombie);
    const Vec3 at = z ? z->player.pos() : Vec3{};
    const f32 yaw = z ? z->player.yaw() : 0.0f;
    roster_.release(player);
    roster_.remove(zombie);
    if (z && player != kNoPlayer) {
        if (PlayerSlot* owner = slot(player)) {
            place_player(*owner, at, yaw);
        }
        emit(ghost::game::ZombieFell{player, zombie, to_glm(at)});
    }
    if (z) {
        jolt_.characterDestroy(static_cast<int>(slot_index(zombie)));
        *z = PlayerSlot{};
        z->id = zombie;
    }
}

void Sim::fell_new_zombies(size_t first_event)
{
    std::vector<PlayerId> fallen;
    for (const ghost::game::RosterEntry& entry : roster_.entries()) {
        if (entry.downed && entry.zombie) {
            fallen.push_back(entry.id);
        }
    }
    for (const PlayerId zombie : fallen) {
        fell_zombie(zombie);
    }
    (void)first_event;
}

PlayerCommand Sim::zombie_command(PlayerSlot& s, f32 dt)
{
    PlayerCommand cmd;
    const ghost::game::NecromiteParams* rules = gameplay_.necromiteRules();
    if (!rules) {
        return cmd;
    }
    const ghost::game::ZombieTuning& tuning = rules->zombie;
    const Player& p = s.player;
    const ghost::game::PlayerState self = world_ghost_state(p);
    const Vec3 eye = eye_of(s);

    std::vector<ghost::game::ZombieTarget> targets;
    for (const ghost::game::PlayerBody& body : gameplay_.bodies()) {
        if (body.id == s.id || body.downed || body.zombie) {
            continue;
        }
        const glm::vec3 chest = ghost::game::bodyPoint(body, body.height * 0.65f);
        targets.push_back({body.feet, chest, !jolt_.raycast(to_glm(eye), chest, ghost::engine::RayFilter{true}).has_value()});
    }
    const ghost::game::MechanismState& gun = s.gun.mechanism.state();
    const auto fireable = [&](size_t k) {
        return gun.chambers[k].state == ghost::game::ChamberState::Live &&
               gameplay_.ammo().elements[gun.chambers[k].round.element].self == ghost::game::SelfEffect::None;
    };
    i32 live = 0;
    for (size_t k = 0; k < gun.chambers.size(); k++) {
        live += fireable(k) ? 1 : 0;
    }
    const ghost::game::ZombieOrder order = ghost::game::zombieThink(s.mind, tuning, self, targets, live, dt, to_glm(p.up()));
    const ghost::game::PlayerCommand& z = order.command;
    cmd.gameplay = true;
    cmd.face_view = true;
    cmd.view_dir = from_glm(ghost::game::viewForward(z.yaw, z.pitch));
    cmd.view_origin = eye;
    cmd.move_x = z.move.x;
    cmd.move_z = z.move.y;
    cmd.run = z.sprint;
    cmd.jump = z.jump;
    cmd.crouch = z.crouch;
    cmd.crawl = z.crawl;
    cmd.aim = z.aim;

    if (order.fire && live > 0) {
        const size_t count = gun.chambers.size();
        const size_t start = static_cast<size_t>(gun.aligned) % count;
        for (size_t step = 0; step < count; step++) {
            const size_t k = (start + step) % count;
            if (!fireable(k)) {
                continue;
            }
            const ghost::game::Round round = gun.chambers[k].round;
            s.gun.mechanism.setChamber(static_cast<int>(k), {ghost::game::ChamberState::Spent, round});
            if (PlayerSlot* owner = slot(s.zombie_of); owner && !owner->remote) {
                owner->gun.mechanism.setChamber(static_cast<int>(k), {ghost::game::ChamberState::Spent, round});
            }
            const MoveState& m = p.movement().state();
            const Vec3 forward = from_glm(order.aim);
            const Vec3 muzzle = eye - p.up() * 0.12f + forward * kMuzzleAhead + frame_right(m.frame, m.yaw) * 0.18f;
            emit(ghost::game::ShotFired{round, 0, s.id});
            gameplay_.fireShot(s.id, to_glm(muzzle), order.aim, tuning.spreadDeg, round, true, events_);
            break;
        }
    }

    for (auto& [id, left] : s.ram_cooled) {
        left -= dt;
    }
    std::erase_if(s.ram_cooled, [](const auto& entry) { return entry.second <= 0.0f; });
    if (ghost::game::zombieRamming(self, tuning)) {
        for (const ghost::game::PlayerBody& struck : gameplay_.bodies()) {
            if (struck.id == s.id || struck.downed || struck.zombie ||
                std::any_of(s.ram_cooled.begin(), s.ram_cooled.end(), [&](const auto& e) { return e.first == struck.id; })) {
                continue;
            }
            const glm::vec3 up = struck.up;
            const glm::vec3 d = struck.feet - self.position;
            const float rise = glm::dot(d, up);
            const glm::vec3 across = d - up * rise;
            const bool touching = glm::length(across) < struck.radius + p.movement().tuning().radius + 0.15f &&
                                  rise > -struck.height && rise < self.height;
            if (!touching) {
                continue;
            }
            const glm::vec3 moving = self.velocity - up * glm::dot(self.velocity, up);
            const glm::vec3 side = glm::abs(up.x) < 0.9f ? glm::vec3(1e-3f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1e-3f);
            const glm::vec3 way = glm::length(moving) > 0.5f ? glm::normalize(moving) : glm::normalize(across + side);
            gameplay_.hurtPlayer(struck.id, tuning.ramDamage, self.position, events_, false, s.id);
            push_player(struck.id, (way + up * 0.35f) * tuning.ramShove);
            s.ram_cooled.emplace_back(struck.id, tuning.ramCooldown);
        }
    }
    return cmd;
}

void Sim::spawn_zone_ghosts()
{
    if (role_ == SimRole::Client) {
        return;
    }
    for (u32 idx : world_.entities().live_indices()) {
        const Entity* e = world_.entities().at(idx);
        if (!e || e->kind != EntityKind::GhostSpawn) {
            continue;
        }
        for (u32 k = 0; k < e->aux_kind; k++) {
            const f32 angle = static_cast<f32>(k) * kTau / static_cast<f32>(e->aux_kind);
            const glm::vec3 offset = k == 0 ? glm::vec3(0.0f) : ghost::game::ringAround(gameplay_.upAt(to_glm(e->pos)), angle) * kZoneGhostSpread;
            gameplay_.spawnGhost(e->mesh_name.view(), to_glm(e->pos) + offset);
        }
    }
}

bool Sim::seated_armed(const PlayerSlot& s) const
{
    if (s.player.state() != PlayerState::Driving || !has_car()) {
        return false;
    }
    const VehicleConfig& cfg = vehicle_.config();
    return s.player.seat() < cfg.seat_count && !cfg.seats[s.player.seat()].drives;
}

bool Sim::aims_out_window(const PlayerSlot& s, Vec3 dir) const
{
    const RigidBody* car = phys_.body(vehicle_.body());
    if (!car) {
        return false;
    }
    const SeatConfig& seat = vehicle_.config().seats[s.player.seat()];
    const Vec3 local = rotate(conjugate(car->rot), dir);
    const f32 side = seat.door_side == 0 ? -1.0f : 1.0f;
    return local.x * side >= seat.window_cos && local.y < kWindowTop && local.y > kWindowBottom;
}

Vec3 Sim::window_origin(const PlayerSlot& s, Vec3 origin, Vec3 dir) const
{
    const RigidBody* car = phys_.body(vehicle_.body());
    if (!car) {
        return origin;
    }
    const SeatConfig& seat = vehicle_.config().seats[s.player.seat()];
    const f32 side = seat.door_side == 0 ? -1.0f : 1.0f;
    const Quat to_local = conjugate(car->rot);
    const Vec3 o = rotate(to_local, origin - car->pos);
    const Vec3 d = rotate(to_local, dir);
    const f32 plane = side * (car->half_extents.x + kWindowClearance);
    if (d.x * side <= 1e-3f) {
        return origin;
    }
    const f32 t = f_max((plane - o.x) / d.x, 0.0f);
    return origin + dir * t;
}

Vec3 Sim::shooter_velocity(const PlayerSlot& s) const
{
    (void)s;
    const RigidBody* car = phys_.body(vehicle_.body());
    return car ? car->vel : Vec3{};
}

}
