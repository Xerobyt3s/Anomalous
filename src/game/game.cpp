#include "game/game.h"
#include "carsys/items.h"
#include "core/arena.h"
#include "core/config.h"
#include "core/log.h"
#include "physics/heightfield.h"
#include "platform/filesystem.h"
#include "platform/input.h"
#include "platform/input_context.h"
#include "platform/window.h"
#include "render/debug_draw.h"
#include "render/device.h"
#include "render/gpu_mesh.h"
#include "render/terrain_render.h"
#include "render/text.h"

namespace anom {
namespace {

constexpr f32 kFallGuardMargin = 15.0f;
constexpr f32 kThrowChargeMax = 0.9f;
constexpr f32 kPlaceRange = 3.5f;
constexpr f32 kPlaceNormalY = 0.55f;
constexpr f32 kEngineAudioLocal[3] = {0.0f, 0.10f, -1.55f};
constexpr f32 kDashAudioLocal[3] = {0.0f, 0.35f, -0.35f};

Vec3 body_point(const RigidBody& body, Vec3 local)
{
    return body.pos + rotate(body.rot, local);
}

} // namespace

bool Game::init(RenderDevice& device, Arena& perm, Arena& scratch, std::string_view zone_dir)
{
    perm_ = &perm;
    scratch_ = &scratch;
    zone_dir_.assign(zone_dir);

    world_.init(perm);
    phys_.init(perm, &terrain_.heightfield());

    if (!zone_load(zone_dir, perm, scratch, world_, phys_, terrain_, spawn_, &pickups_)) {
        log_error("game: zone load failed");
        return false;
    }

    const f32 tx = spawn_.tower_present ? spawn_.tower_x : 258.0f;
    const f32 tz = spawn_.tower_present ? spawn_.tower_z : 82.0f;
    const f32 tyaw = spawn_.tower_present ? spawn_.tower_yaw_deg : 20.0f;
    tower_pos_ = Vec3{tx, terrain_.heightfield().sample(tx, tz), tz};
    tower_rot_ = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, tyaw * kDegToRad);

    if (!vehicle_.init(phys_, scratch, "assets/cars/excel.cfg", spawn_.car_pos, spawn_.car_yaw)) {
        return false;
    }

    player_.init(spawn_.player_pos, spawn_.player_yaw);
    carsys_.init();
    interact_.init();
    weather_.init(20260718ull);
    tapes_.init();
    boxes_.init(scratch, "assets/cars/excel_interact.cfg");
    interact_spawn_zone_pickups(world_, phys_, terrain_, pickups_);
    audio_.init(perm);

    editor_.init(perm);
    editor_.snapshot_world(world_, phys_);

    camera_.pos = spawn_.player_pos + Vec3{0.0f, kPlayerEyeHeight, 0.0f};
    camera_.yaw = spawn_.player_yaw;

    for (u32 idx : world_.entities().live_indices()) {
        const Entity* e = world_.entities().at(idx);
        if (e && (e->flags & kEntityFlagTower)) {
            tower_pos_ = e->pos;
            tower_rot_ = e->rot;
            break;
        }
    }

    (void)device;
    log_info("game: ready | %u entities | %u pickups", world_.count(), pickups_.count);
    return true;
}

void Game::reset_car()
{
    vehicle_.teleport(phys_, spawn_.car_pos, spawn_.car_yaw);
}

void Game::reset_player()
{
    player_.init(spawn_.player_pos, spawn_.player_yaw);
}

void Game::apply_weather_grip()
{
    if (weather_.wetness() <= 0.002f) {
        return;
    }
    const RigidBody* body = phys_.body(vehicle_.body());
    const f32 road = body ? terrain_.road_amount(body->pos.x, body->pos.z) : 0.0f;
    const f32 wet_mul = 1.0f - weather_.wetness() * f_lerp(0.40f, 0.24f, road);
    for (u32 i = 0; i < kWheelCount; i++) {
        vehicle_.effects().tire_grip_mul[i] *= wet_mul;
    }
}

void Game::sync_pickup_transforms()
{
    Pool<Entity>& pool = world_.entities();
    for (u32 idx : pool.live_indices()) {
        Entity* e = pool.at(idx);
        if (!e || e->kind != EntityKind::PartPickup || !e->body.valid()) {
            continue;
        }
        const RigidBody* body = phys_.body(e->body);
        if (!body) {
            continue;
        }
        const ItemKind kind = static_cast<ItemKind>(e->aux_kind);
        e->rot = body->rot * item_cargo_rot(kind);
        e->pos = body->pos - rotate(e->rot, item_mesh_center(kind));
    }
}

void Game::tick(f32 dt, const PlayerCommand& cmd)
{
    weather_.tick(dt);
    carsys_.rain_level = weather_.rain();
    carsys_.tick(vehicle_, phys_, dt);
    apply_weather_grip();

    vehicle_.tick(phys_, dt);
    phys_.tick(dt);
    player_.tick(phys_, &vehicle_, cmd, dt);

    sync_pickup_transforms();

    telem_rpm_.push(drivetrain_rpm(vehicle_.train()));
    telem_slip_.push((vehicle_.wheel(WHEEL_RL).slip_ratio
                      + vehicle_.wheel(WHEEL_RR).slip_ratio) * 0.5f);
    telem_load_.push(vehicle_.wheel(WHEEL_FL).load * 0.001f);
    telem_speed_.push(f_abs(vehicle_.forward_speed(phys_)) * 3.6f);

    tick_count_++;
}

void Game::drive_input(const Input& input, PlayerCommand& cmd, f32 frame_dt)
{
    (void)cmd;
    (void)frame_dt;

    const f32 forward = input.down(Key::W) ? 1.0f : 0.0f;
    const f32 reverse = input.down(Key::S) ? 1.0f : 0.0f;
    const f32 steer = (input.down(Key::D) ? 1.0f : 0.0f) - (input.down(Key::A) ? 1.0f : 0.0f);
    const bool handbrake = input.down(Key::Space) || carsys_.handbrake_latched;
    vehicle_.driver_input(phys_, forward, reverse, steer, handbrake);

    if (input.pressed(Key::L)) {
        carsys_.headlight_switch = !carsys_.headlight_switch;
    }
    if (input.pressed(Key::T)) {
        vehicle_.train().manual = !vehicle_.train().manual;
    }
    if (vehicle_.train().manual) {
        if (input.pressed(Key::Up)) {
            drivetrain_request_shift(vehicle_.train(), 1);
        }
        if (input.pressed(Key::Down)) {
            drivetrain_request_shift(vehicle_.train(), -1);
        }
    }
    if (input.pressed(Key::G) && carsys_.key_inserted && !carsys_.engine_on
        && interact_.action() == InteractAction::Crank) {
        carsys_.key_inserted = false;
        carsys_.crank_request = false;
        interact_.set_has_key(true);
    }
}

void Game::foot_input(const Input& input, PlayerCommand& cmd, f32 frame_dt)
{
    VehicleInput parked;
    parked.handbrake = carsys_.handbrake_latched;
    vehicle_.set_input(parked);

    cmd.move_x = (input.down(Key::D) ? 1.0f : 0.0f) - (input.down(Key::A) ? 1.0f : 0.0f);
    cmd.move_z = (input.down(Key::W) ? 1.0f : 0.0f) - (input.down(Key::S) ? 1.0f : 0.0f);
    cmd.run = input.down(Key::LeftShift);
    if (input.pressed(Key::Space)) {
        pending_jump_ = true;
    }

    if (interact_.cable_drag() >= 0) {
        if (input.pressed(Key::G)) {
            carsys_.cables[interact_.cable_drag()].reset();
            interact_.set_cable_drag(-1);
        }
        throw_charge_ = 0.0f;
        return;
    }

    if (interact_.hands().kind == ITEM_NONE) {
        throw_charge_ = 0.0f;
        place_active_ = false;
        place_valid_ = false;
        return;
    }

    if (input.down(Key::G)) {
        throw_charge_ = f_min(throw_charge_ + frame_dt, kThrowChargeMax);
    }
    if (input.released(Key::G)) {
        const f32 power = throw_charge_ < 0.12f ? 0.0f : throw_charge_ / kThrowChargeMax;
        const Vec3 eye = player_.pos() + Vec3{0.0f, 1.38f, 0.0f};
        interact_.drop(world_, phys_, eye, camera_.forward(), power);
        throw_charge_ = 0.0f;
    }

    if (input.down(Key::F)) {
        if (!place_active_) {
            place_active_ = true;
            place_yaw_ = -camera_.yaw;
        }
        place_yaw_ += input.scroll() * 0.45f;

        Ray aim;
        aim.origin = camera_.pos;
        aim.dir = camera_.forward();
        PhysRayHit hit;
        place_valid_ = phys_.raycast(aim, kPlaceRange, &hit) && hit.normal.y > kPlaceNormalY;
        if (place_valid_) {
            const f32 lift = item_cargo_half(interact_.hands().kind).y + 0.015f;
            place_pos_ = hit.point + Vec3{0.0f, lift, 0.0f};
        }
    } else if (place_active_) {
        if (place_valid_ && interact_.hands().kind != ITEM_NONE) {
            interact_spawn_pickup(world_, phys_, interact_.hands(), place_pos_, place_yaw_,
                                  Vec3{});
            interact_.hands().kind = ITEM_NONE;
        }
        place_active_ = false;
        place_valid_ = false;
    }
}

void Game::build_input_context(const Input& input)
{
    const bool cursor_visible = editor_.active() || toggles_.free_cam || toggles_.tuning;

    context_.begin_frame();
    context_.activate(InputLayer::TextField, ui_.text_active());
    context_.activate(InputLayer::Editor, editor_.active());
    context_.activate(InputLayer::Panels,
                      cursor_visible && ui_.mouse_over_panel(input.mouse_pos()));
    context_.activate(InputLayer::Gameplay, !editor_.active() && !toggles_.free_cam);
}

void Game::handle_input(Window& window, const Input& input, f32 frame_dt)
{
    build_input_context(input);
    const bool global = context_.keyboard(InputLayer::Global);

    if (global) {
        if (input.pressed(Key::Escape) && !editor_.active()) {
            window.request_close();
        }
        if (input.pressed(Key::R) && !editor_.active()) {
            reset_car();
        }
        if (input.pressed(Key::F1)) {
            toggles_.telemetry = !toggles_.telemetry;
        }
        if (input.pressed(Key::F2)) {
            toggles_.phys_debug = !toggles_.phys_debug;
        }
        if (input.pressed(Key::F3)) {
            toggles_.collision = !toggles_.collision;
        }
        if (input.pressed(Key::F4)) {
            toggles_.carsys = !toggles_.carsys;
        }
        if (input.pressed(Key::F5)) {
            toggles_.slow_mo = !toggles_.slow_mo;
        }
        if (input.pressed(Key::F6)) {
            toggles_.free_cam = !toggles_.free_cam;
        }
        if (input.pressed(Key::F7)) {
            toggles_.tuning = !toggles_.tuning;
        }
        if (input.pressed(Key::F8)) {
            editor_.toggle(world_, phys_);
            if (editor_.active()) {
                reset_car();
                reset_player();
            }
            build_input_context(input);
        }
        if (input.pressed(Key::C) && !editor_.active()) {
            toggles_.chase_cam = !toggles_.chase_cam;
        }
    }

    PlayerCommand cmd;

    if (editor_.active()) {
        window.set_cursor_captured(context_.cursor_captured());
        const FramebufferSize size = window.framebuffer_size();
        const Vec2 viewport{static_cast<f32>(size.width), static_cast<f32>(size.height)};
        editor_.update(input, context_, camera_, world_, phys_, terrain_, *perm_, *scratch_,
                       &vehicle_, &boxes_, viewport, zone_dir_.view(), frame_dt);
        vehicle_.set_input(VehicleInput{});
        pending_jump_ = false;
        pending_interact_ = false;
        pending_cmd_ = cmd;
        return;
    }

    if (toggles_.free_cam) {
        window.set_cursor_captured(camera_.fly_update(input, frame_dt));
        vehicle_.set_input(VehicleInput{});
        pending_jump_ = false;
        pending_interact_ = false;
        pending_cmd_ = cmd;
        return;
    }

    if (!context_.keyboard(InputLayer::Gameplay)) {
        window.set_cursor_captured(false);
        vehicle_.set_input(VehicleInput{});
        pending_cmd_ = cmd;
        return;
    }

    window.set_cursor_captured(context_.cursor_captured() && !toggles_.tuning);
    if (window.cursor_captured()) {
        player_.look(input.mouse_delta().x, input.mouse_delta().y);
    }

    if (player_.driving()) {
        drive_input(input, cmd, frame_dt);
    } else {
        foot_input(input, cmd, frame_dt);
    }

    Ray view_ray;
    view_ray.origin = camera_.pos;
    view_ray.dir = camera_.forward();

    interact_.set_tower(true, tower_pos_ + rotate(tower_rot_, Vec3{0.0f, 1.35f, 0.62f}));

    InteractContext ctx;
    ctx.player = &player_;
    ctx.veh = &vehicle_;
    ctx.sys = &carsys_;
    ctx.world = &world_;
    ctx.phys = &phys_;
    ctx.tapes = &tapes_;
    ctx.view_ray = view_ray;
    ctx.e_down = input.down(Key::E);
    ctx.e_pressed = input.pressed(Key::E);
    interact_.update(boxes_, ctx, frame_dt);

    player_.set_speed_mul(f_max(1.0f - item_mass(interact_.hands().kind) * 0.012f, 0.6f));

    if (input.pressed(Key::E)) {
        if (interact_.action() == InteractAction::EnterCar) {
            pending_interact_ = true;
        } else if (interact_.action() == InteractAction::ExitCar) {
            player_.set_exit_pref(interact_.target_side() == 0 ? -1 : 1);
            pending_interact_ = true;
        }
    }

    pending_cmd_ = cmd;
}

void Game::guard_against_falling()
{
    const f32 floor = terrain_.heightfield().min_height() - kFallGuardMargin;
    const RigidBody* car = phys_.body(vehicle_.body());
    if (car && (car->pos.y < floor || !body_state_valid(*car))) {
        reset_car();
    }
    if (player_.state() == PlayerState::OnFoot && player_.pos().y < floor) {
        reset_player();
    }
}

void Game::update_camera(f32 frame_dt)
{
    if (toggles_.free_cam || editor_.active()) {
        return;
    }
    player_.camera(phys_, &vehicle_, alpha_, frame_dt, camera_);
}

void Game::advance(f32 frame_dt)
{
    if (editor_.active()) {
        accumulator_ = 0.0;
        alpha_ = 0.0f;
        guard_against_falling();
        return;
    }

    accumulator_ += static_cast<f64>(frame_dt) * (toggles_.slow_mo ? 0.1 : 1.0);
    while (accumulator_ >= kFixedDt) {
        PlayerCommand cmd = pending_cmd_;
        cmd.jump = pending_jump_;
        cmd.interact = pending_interact_;
        pending_jump_ = false;
        pending_interact_ = false;
        tick(kFixedDt, cmd);
        accumulator_ -= kFixedDt;
    }

    guard_against_falling();
    alpha_ = static_cast<f32>(accumulator_ / kFixedDt);
    update_camera(frame_dt);
}

void Game::poll_hot_reload(Arena& scratch, f64 now)
{
    if (now >= next_cfg_poll_) {
        next_cfg_poll_ = now + 1.0;
        vehicle_.poll_config_reload(phys_, scratch);
    }
    boxes_.poll(scratch, now);
}

void Game::update_audio(f32 frame_dt)
{
    if (!audio_.ok()) {
        return;
    }
    const RigidBody* body = phys_.body(vehicle_.body());
    if (!body) {
        return;
    }

    const Vec3 engine_pos = body_point(*body, Vec3{kEngineAudioLocal[0], kEngineAudioLocal[1],
                                                   kEngineAudioLocal[2]});
    const Vec3 dash_pos = body_point(*body, Vec3{kDashAudioLocal[0], kDashAudioLocal[1],
                                                 kDashAudioLocal[2]});
    audio_.set_listener(camera_.pos, camera_.forward(), player_.vel());
    audio_.set_car(engine_pos, body->pos, dash_pos, body->vel);
    audio_.set_occlusion(player_.driving() ? 0.55f : 1.0f, frame_dt);

    audio_.set_engine(drivetrain_rpm(vehicle_.train()),
                      f_clamp01(vehicle_.input().throttle), carsys_.engine_on,
                      carsys_.crank_active, frame_dt);

    const f32 speed = f_abs(vehicle_.forward_speed(phys_));
    u32 grounded = 0;
    f32 skid = 0.0f;
    for (u32 i = 0; i < kWheelCount; i++) {
        const Wheel& w = vehicle_.wheel(i);
        if (w.grounded) {
            grounded++;
        }
        skid = f_max(skid, f_clamp01((f_abs(w.slide_lat) - 1.5f) / 6.0f));
    }
    const f32 road = terrain_.road_amount(body->pos.x, body->pos.z);
    audio_.set_rolling(speed, road, grounded > 0, weather_.wetness(), frame_dt);
    audio_.set_skid(skid, frame_dt);
    audio_.set_horn(false, frame_dt);
    audio_.set_rain(weather_.rain() * (player_.driving() ? 0.35f : 1.0f),
                    player_.driving() ? weather_.rain() : 0.0f, frame_dt);
}

void Game::draw_debug_overlays(DebugDraw& debug)
{
    if (toggles_.collision) {
        const StaticGrid& statics = phys_.statics();
        for (u32 i = 0; i < statics.tri_count(); i++) {
            const StaticTri& tri = statics.tri(i);
            debug.line(tri.a, tri.b, kDdDark);
            debug.line(tri.b, tri.c, kDdDark);
            debug.line(tri.c, tri.a, kDdDark);
        }
    }
    if (!toggles_.phys_debug) {
        return;
    }
    const Pool<RigidBody>& bodies = phys_.bodies();
    for (u32 idx : bodies.live_indices()) {
        const RigidBody* body = bodies.at(idx);
        if (!body) {
            continue;
        }
        debug.obb(body->pos, body->rot, body->half_extents,
                  body->asleep ? kDdGray : kDdGreen);
    }
    for (u32 i = 0; i < kWheelCount; i++) {
        const Wheel& w = vehicle_.wheel(i);
        if (!w.grounded) {
            continue;
        }
        debug.line(w.contact_point, w.contact_point + w.force_susp * 0.0002f, kDdCyan);
        debug.line(w.contact_point, w.contact_point + w.force_long * 0.0004f, kDdGreen);
        debug.line(w.contact_point, w.contact_point + w.force_lat * 0.0004f, kDdRed);
    }
}

void Game::draw_hud(DebugDraw& debug, TextRenderer& text, Vec2 viewport, f32 frame_dt)
{
    const f32 cx = viewport.x * 0.5f;
    const f32 cy = viewport.y * 0.5f;

    if (!editor_.active() && !toggles_.free_cam) {
        debug.line_2d(cx - 6.0f, cy, cx + 6.0f, cy, kDdWhite);
        debug.line_2d(cx, cy - 6.0f, cx, cy + 6.0f, kDdWhite);
    }

    if (player_.driving()) {
        const f32 speed = f_abs(vehicle_.forward_speed(phys_)) * 3.6f;
        const i32 gear = vehicle_.train().gear;
        debug.text_2d(text, 24.0f, viewport.y - 96.0f, 26.0f, kDdWhite, "%3.0f km/h",
                      static_cast<f64>(speed));
        debug.text_2d(text, 24.0f, viewport.y - 64.0f, 20.0f, kDdWhite, "%4.0f rpm  gear %s%s",
                      static_cast<f64>(drivetrain_rpm(vehicle_.train())),
                      gear < 0 ? "R" : (gear == 0 ? "N" : "D"),
                      vehicle_.train().manual ? " [M]" : "");
    }

    const std::string_view prompt = interact_.prompt();
    if (!prompt.empty()) {
        const f32 width = text.measure(prompt, 20.0f);
        debug.text_2d(text, cx - width * 0.5f, cy + 60.0f, 20.0f, kDdWhite, "%.*s",
                      static_cast<int>(prompt.size()), prompt.data());
        if (interact_.action_is_hold() && interact_.hold_progress() > 0.0f) {
            const f32 w = 160.0f;
            debug.rect_2d(cx - w * 0.5f, cy + 74.0f, cx + w * 0.5f, cy + 82.0f, kDdGray);
            debug.rect_2d_filled(cx - w * 0.5f, cy + 74.0f,
                                 cx - w * 0.5f + w * interact_.hold_progress(), cy + 82.0f,
                                 kDdYellow);
        }
    }

    if (interact_.hands().kind != ITEM_NONE) {
        const std::string_view name = item_name(interact_.hands().kind);
        debug.text_2d(text, 24.0f, 28.0f, 18.0f, kDdYellow, "holding %.*s",
                      static_cast<int>(name.size()), name.data());
        if (throw_charge_ > 0.0f) {
            debug.text_2d(text, 24.0f, 50.0f, 16.0f, kDdOrange, "throw %.0f%%",
                          static_cast<f64>(throw_charge_ / kThrowChargeMax * 100.0f));
        }
    }

    if (place_active_ && place_valid_) {
        debug.circle(place_pos_, Vec3{0.0f, 1.0f, 0.0f}, 0.25f, kDdGreen);
    }

    if (toggles_.telemetry) {
        ui_.panel_begin("telemetry", viewport.x - 276.0f, 16.0f, 260.0f);
        ui_.graph("rpm", telem_rpm_.samples, telem_rpm_.head, 0.0f, 7000.0f, kDdGreen);
        ui_.graph("km/h", telem_speed_.samples, telem_speed_.head, 0.0f, 180.0f, kDdCyan);
        ui_.graph("slip", telem_slip_.samples, telem_slip_.head, -1.0f, 1.0f, kDdOrange);
        ui_.graph("load kN", telem_load_.samples, telem_load_.head, 0.0f, 8.0f, kDdYellow);
        ui_.panel_end();
    }

    if (toggles_.carsys) {
        ui_.panel_begin("carsys", 16.0f, viewport.y - 260.0f, 240.0f);
        ui_.label("engine %s", carsys_.engine_on ? "running" : "off");
        ui_.label("fuel %.1f L", static_cast<f64>(carsys_.fluids.fuel));
        ui_.label("oil %.2f", static_cast<f64>(carsys_.fluids.oil));
        ui_.label("coolant %.0f C", static_cast<f64>(carsys_.fluids.coolant_temp));
        ui_.label("battery %.2f", static_cast<f64>(carsys_.elec.battery_charge));
        ui_.label("weather %s %.2f", weather_mode_name(weather_.mode()),
                  static_cast<f64>(weather_.rain()));
        ui_.panel_end();
    }

    if (toggles_.tuning) {
        ui_.panel_begin("tuning", 16.0f, 16.0f, 260.0f);
        VehicleConfig& cfg = vehicle_.config();
        ui_.slider("peak mu", cfg.tire_peak_mu, 0.4f, 2.0f);
        ui_.slider("slide mu", cfg.tire_slide_mu, 0.2f, 1.6f);
        ui_.slider("diff lock", cfg.diff_lock, 0.0f, 1.0f);
        ui_.slider("arb front", cfg.arb_front, 0.0f, 40000.0f);
        ui_.slider("arb rear", cfg.arb_rear, 0.0f, 40000.0f);
        ui_.label("%.1f fps", frame_dt > 0.0f ? 1.0 / static_cast<f64>(frame_dt) : 0.0);
        ui_.panel_end();
    }
}

void Game::bind_entity_meshes(RenderDevice& device)
{
    Pool<Entity>& pool = world_.entities();
    for (u32 idx : pool.live_indices()) {
        Entity* e = pool.at(idx);
        if (!e || e->mesh || e->mesh_name.empty() || e->kind == EntityKind::Trigger) {
            continue;
        }
        e->mesh = device.assets().mesh(e->mesh_name.view());
    }
}

void Game::draw_vehicle(RenderDevice& device)
{
    const RigidBody* body = phys_.body(vehicle_.body());
    const VehicleConfig& cfg = vehicle_.config();
    if (!body || cfg.body_mesh.empty()) {
        return;
    }

    const Vec3 pos = lerp(body->prev_pos, body->pos, alpha_);
    const Quat rot = slerp(body->prev_rot, body->rot, alpha_);
    const Vec3 one{1.0f, 1.0f, 1.0f};

    const Mat4 base = mat4_trs(pos, rot, one);
    device.draw_mesh(device.assets().mesh(cfg.body_mesh.view()),
                     base * mat4_trs(-cfg.com_offset, quat_identity(), one));

    if (cfg.wheel_mesh.empty()) {
        return;
    }
    const GpuMesh* wheel_mesh = device.assets().mesh(cfg.wheel_mesh.view());
    for (u32 i = 0; i < kWheelCount; i++) {
        const Wheel& w = vehicle_.wheel(i);
        const f32 drop = cfg.wheels[i].travel - w.compression;
        const f32 radius_mul = vehicle_.effects().tire_radius_mul[i];
        const f32 wobble = radius_mul < 0.95f ? std::sin(w.spin_angle) * (1.0f - radius_mul) * 0.10f
                                              : 0.0f;
        const Vec3 local = w.attach_local - Vec3{0.0f, drop - wobble, 0.0f};
        const Vec3 center = pos + rotate(rot, local);

        Quat q = rot * quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, w.steer_rad);
        q = q * quat_from_axis_angle(Vec3{1.0f, 0.0f, 0.0f}, w.spin_angle);
        if (cfg.wheels[i].pos.x > 0.0f) {
            q = q * quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, kPi);
        }
        device.draw_mesh(wheel_mesh, mat4_trs(center, q, Vec3{1.0f, radius_mul, radius_mul}));
    }
}

void Game::render(RenderDevice& device, TerrainRenderer& terrain_renderer, DebugDraw& debug,
                  TextRenderer& text, const Input& input, Vec2 viewport, f32 time, f32 frame_dt)
{
    text.begin_frame();
    debug.begin_frame();
    ui_.begin_frame(input, &debug, &text);
    bind_entity_meshes(device);
    update_audio(frame_dt);

    device.set_weather(weather_.wetness(), weather_.overcast());
    device.set_windshield(carsys_.windshield_wet, carsys_.wiper_sweep, carsys_.glass_wet);

    if (device.shadow_begin(camera_.pos)) {
        terrain_renderer.draw(device);
        device.shadow_end();
    }

    device.set_time(time);
    device.begin_frame(camera_, static_cast<i32>(viewport.x), static_cast<i32>(viewport.y));
    device.draw_sky(time);
    terrain_renderer.draw(device);
    terrain_renderer.draw_scrub(device, camera_.pos, time);

    const Pool<Entity>& pool = world_.entities();
    for (u32 idx : pool.live_indices()) {
        const Entity* e = pool.at(idx);
        if (!e || !e->mesh) {
            continue;
        }
        device.draw_mesh(e->mesh, mat4_trs(e->pos, e->rot, Vec3{e->scale, e->scale, e->scale}));
    }

    draw_vehicle(device);
    car_render_.draw(device, carsys_, vehicle_, phys_, alpha_, frame_dt, 0);
    device.draw_rain(weather_.rain(), weather_.wind(), camera_.pos, time);
    car_render_.draw_glass(device, carsys_, vehicle_, phys_, alpha_, time);

    draw_debug_overlays(debug);
    editor_.render(ui_, debug, text, input, *scratch_, camera_, world_, phys_, terrain_,
                   &vehicle_, &boxes_, &tapes_, viewport);
    debug.flush_world(device);

    device.end_frame();
    device.post_process(time);

    draw_hud(debug, text, viewport, frame_dt);
    debug.flush_overlay(device, text);
    text.flush(device);
}

} // namespace anom
