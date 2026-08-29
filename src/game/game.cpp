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
#include "render/sky.h"

namespace anom {
namespace {

constexpr f32 kFallGuardMargin = 15.0f;
constexpr f32 kThrowChargeMax = 0.9f;
constexpr f32 kPlaceRange = 3.5f;
constexpr f32 kPlaceNormalY = 0.55f;
constexpr f32 kEngineAudioLocal[3] = {0.0f, 0.10f, -1.55f};
constexpr f32 kDashAudioLocal[3] = {0.0f, 0.35f, -0.35f};
constexpr Vec3 kTowerPortLocal{0.0f, 1.35f, 0.62f};
constexpr Vec3 kCarPressLocal{0.0f, 0.60f, 0.0f};
constexpr Vec3 kCarPressHalf{1.02f, 1.00f, 2.20f};
constexpr f32 kGrassPressRange = 90.0f;
constexpr Vec3 kBuildingPressMargin{0.6f, 0.0f, 0.6f};
constexpr Vec3 kTerminalScreenOffset{0.0f, 0.047f, 0.170f};
constexpr Vec3 kTerminalScreenScale{0.304f, 0.19f, 1.0f};

Vec3 body_point(const RigidBody& body, Vec3 local)
{
    return body.pos + rotate(body.rot, local);
}

} // namespace

void Game::set_environment(const Environment& env)
{
    env_ = env;
    time_of_day_ = env.time_of_day;
}

bool Game::init(RenderDevice& device, FontChain& fonts, Arena& perm, Arena& scratch,
                std::string_view zone_dir)
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

    disks_.init(perm);
    terminal_.init(perm, disks_);
    terminal_.mapdata().init(terrain_.heightfield());
    if (!term_render_.init(fonts, scratch)) {
        log_error("game: terminal renderer init failed");
        return false;
    }
    if (!tree_render_.init(scratch)) {
        return false;
    }

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
    log_info("game: ready | %u entities | %u pickups | car at %.1f %.1f %.1f",
             world_.count(), pickups_.count, static_cast<f64>(spawn_.car_pos.x),
             static_cast<f64>(spawn_.car_pos.y), static_cast<f64>(spawn_.car_pos.z));
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
    context_.activate(InputLayer::Terminal, term_focus_);
    context_.activate(InputLayer::Editor, editor_.active());
    context_.activate(InputLayer::Panels,
                      cursor_visible && ui_.mouse_over_panel(input.mouse_pos()));
    context_.activate(InputLayer::Gameplay, !editor_.active() && !toggles_.free_cam);
}

void Game::handle_input(Window& window, const Input& input, f32 frame_dt)
{
    build_input_context(input);
    update_camera_item(input, frame_dt);
    const bool global = context_.keyboard(InputLayer::Global);

    if (term_focus_) {
        window.set_cursor_captured(false);
        vehicle_.set_input(VehicleInput{});
        terminal_keys(input);
        if (input.pressed(Key::Escape)) {
            term_focus_ = false;
        }
        pending_cmd_ = PlayerCommand{};
        return;
    }

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

    interact_.set_tower(true, tower_port_pos());

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
    if (interact_.take_terminal_request() && carsys_.computer_on) {
        term_focus_ = true;
    }

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

void Game::track_camera_velocity(f32 frame_dt)
{
    cam_vel_ = Vec3{};
    if (prev_cam_valid_ && frame_dt > 1e-4f) {
        cam_vel_ = (camera_.pos - prev_cam_pos_) * (1.0f / frame_dt);
        if (length(cam_vel_) > 60.0f) {
            cam_vel_ = Vec3{};
        }
    }
    prev_cam_pos_ = camera_.pos;
    prev_cam_valid_ = true;
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
    track_camera_velocity(frame_dt);
    update_cables(frame_dt);
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

    if (!editor_.active() && !toggles_.free_cam && !viewfinder_) {
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

    if (place_active_ && interact_.hands().kind != ITEM_NONE) {
        if (place_valid_) {
            debug.text_2d(text, cx - 130.0f, viewport.y * 0.66f, 17.0f, kDdWhite,
                          "release [F] to place | scroll to rotate");
        } else {
            debug.text_2d(text, cx - 90.0f, viewport.y * 0.66f, 17.0f, kDdGray,
                          "no surface to place on");
        }
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
        if (!e || e->mesh || e->mesh_name.empty() || e->kind == EntityKind::Trigger
            || e->kind == EntityKind::Tree) {
            continue;
        }
        e->mesh = device.assets().mesh(e->mesh_name.view());
    }
}

void Game::draw_entities(RenderDevice& device)
{
    const Pool<Entity>& pool = world_.entities();
    for (u32 idx : pool.live_indices()) {
        const Entity* e = pool.at(idx);
        if (e && e->mesh && e->kind != EntityKind::Tree) {
            device.draw_mesh(e->mesh,
                             mat4_trs(e->pos, e->rot, Vec3{e->scale, e->scale, e->scale}));
        }
    }
}

void Game::collect_trees()
{
    tree_render_.begin_frame();
    const Pool<Entity>& pool = world_.entities();
    for (u32 idx : pool.live_indices()) {
        const Entity* e = pool.at(idx);
        if (e && e->kind == EntityKind::Tree) {
            tree_render_.submit(e->aux_kind,
                                mat4_trs(e->pos, e->rot, Vec3{e->scale, e->scale, e->scale}));
        }
    }
}

void Game::update_grass_press(TerrainRenderer& terrain_renderer)
{
    terrain_renderer.clear_press_volumes();

    if (const RigidBody* body = phys_.body(vehicle_.body())) {
        const Vec3 com = vehicle_.config().com_offset;
        const Vec3 centre = body->pos + rotate(body->rot, kCarPressLocal - com);
        terrain_renderer.add_press_volume(centre, kCarPressHalf, body->rot);
    }

    const Pool<Entity>& pool = world_.entities();
    for (u32 idx : pool.live_indices()) {
        const Entity* e = pool.at(idx);
        if (!e || e->kind != EntityKind::Building || !e->mesh || !e->mesh->loaded) {
            continue;
        }
        if (distance_sq(e->pos, camera_.pos) > kGrassPressRange * kGrassPressRange) {
            continue;
        }
        const Aabb& b = e->mesh->bounds;
        const Vec3 local_centre = (b.min + b.max) * 0.5f * e->scale;
        const Vec3 half = (b.max - b.min) * 0.5f * e->scale + kBuildingPressMargin;
        terrain_renderer.add_press_volume(e->pos + rotate(e->rot, local_centre), half, e->rot);
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

u32 Game::terminal_screen_texture() const
{
    return terminal_.powered() ? term_render_.texture() : 0;
}

void Game::draw_loose_terminal(RenderDevice& device, u32 screen_texture)
{
    if (carsys_.parts[PART_COMPUTER].installed) {
        return;
    }
    const Entity* loose = find_pickup(ITEM_COMPUTER);
    if (!loose) {
        return;
    }

    const Mat4 base = mat4_trs(loose->pos, loose->rot,
                               Vec3{loose->scale, loose->scale, loose->scale});
    if (screen_texture) {
        device.draw_lit_quad(base * mat4_trs(kTerminalScreenOffset, quat_identity(),
                                             kTerminalScreenScale),
                             screen_texture, 0.0f);
    }
    if (carsys_.floppy_disk >= 0) {
        device.draw_mesh(device.assets().mesh("part_floppy"),
                         base * mat4_trs(Vec3{0.0f, -0.119f, 0.223f}, quat_identity(),
                                         Vec3{1.0f, 1.0f, 1.0f}));
    }
}

void Game::draw_place_preview(RenderDevice& device, DebugDraw& debug)
{
    if (!place_active_ || !place_valid_ || interact_.hands().kind == ITEM_NONE) {
        return;
    }
    const ItemKind kind = interact_.hands().kind;
    const Quat yaw_rot = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, place_yaw_);
    const Quat rot = yaw_rot * item_cargo_rot(kind);
    const Vec3 pos = place_pos_ - rotate(rot, item_mesh_center(kind));

    device.draw_mesh(device.assets().mesh(item_mesh(kind)),
                     mat4_trs(pos, rot, Vec3{1.0f, 1.0f, 1.0f}));
    debug.obb(place_pos_, yaw_rot, item_cargo_half(kind), dd_rgba(120, 220, 140, 255));
}

void Game::draw_viewmodel(RenderDevice& device)
{
    const Item& held = interact_.hands();
    if (held.kind == ITEM_NONE || editor_.active() || toggles_.free_cam || place_active_
        || viewfinder_) {
        return;
    }
    if (player_.state() != PlayerState::OnFoot && !player_.driving()) {
        return;
    }
    if (interact_.action() == InteractAction::PlaceCargo) {
        return;
    }

    AssetCache& assets = device.assets();

    if (interact_.action() == InteractAction::Refuel && held.kind == ITEM_JERRYCAN) {
        if (const RigidBody* body = phys_.body(vehicle_.body())) {
            const f32 pour = interact_.hold_progress();
            const Vec3 local = Vec3{f_lerp(1.06f, 0.96f, pour), f_lerp(0.38f, 0.30f, pour),
                                    1.30f}
                             - vehicle_.config().com_offset;
            const Quat tilt = quat_from_axis_angle(Vec3{0.0f, 0.0f, 1.0f},
                                                   f_lerp(0.45f, 1.95f, pour));
            const Mat4 base = mat4_trs(lerp(body->prev_pos, body->pos, alpha_),
                                       slerp(body->prev_rot, body->rot, alpha_),
                                       Vec3{1.0f, 1.0f, 1.0f});
            device.draw_mesh(assets.mesh(item_mesh(ITEM_JERRYCAN)),
                             base * mat4_trs(local, tilt, Vec3{1.0f, 1.0f, 1.0f}));
            return;
        }
    }

    f32 scale = held.kind == ITEM_TIRE ? 0.45f : 0.85f;
    if (antenna_variant_for_item(held.kind) >= 0) {
        scale = 0.5f;
    }
    if (player_.driving()) {
        scale *= 0.75f;
    }

    const Quat rot = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, -camera_.yaw);
    const Vec3 pos = hands_item_pos() - rotate(rot, item_mesh_center(held.kind)) * scale;
    const Mat4 base = mat4_trs(pos, rot, Vec3{scale, scale, scale});
    device.draw_mesh(assets.mesh(item_mesh(held.kind)), base);

    const u32 screen_texture = terminal_screen_texture();
    if (held.kind == ITEM_COMPUTER && screen_texture) {
        device.draw_lit_quad(base * mat4_trs(kTerminalScreenOffset, quat_identity(),
                                             kTerminalScreenScale),
                             screen_texture, 0.0f);
    }
}

void Game::update_camera_item(const Input& input, f32 frame_dt)
{
    viewfinder_ = player_.state() == PlayerState::OnFoot && !term_focus_ && !toggles_.free_cam
               && !editor_.active() && interact_.hands().kind == ITEM_CAMERA
               && input.down(MouseButton::Right);
    if (viewfinder_ && input.pressed(MouseButton::Left)) {
        capture_pending_ = true;
    }
    capture_flash_ = f_max(capture_flash_ - frame_dt * 3.0f, 0.0f);
    if (capture_msg_until_ > 0.0f) {
        capture_msg_until_ -= frame_dt;
    }
}

void Game::capture_exposure(RenderDevice& device)
{
    if (!capture_pending_) {
        return;
    }
    capture_pending_ = false;

    ArenaScope temp(*scratch_);
    u8* shot = scratch_->push_array<u8>(kPhotoBytes);
    if (!shot || !device.read_backbuffer_rgb(*scratch_, shot, kPhotoWidth, kPhotoHeight)) {
        return;
    }

    if (disks_.camera_capture(&terminal_.fs(), shot)) {
        capture_flash_ = 1.0f;
        audio_.play(SFX_RATCHET, 0.55f, 1.9f);
        capture_msg_.format("exposure saved - %u left",
                            disks_.camera_exposures_left(&terminal_.fs()));
    } else {
        audio_.play(SFX_THUMP, 0.30f, 0.7f);
        capture_msg_.assign("film spent");
    }
    capture_msg_until_ = 2.5f;
}

void Game::draw_viewfinder(DebugDraw& debug, TextRenderer& text, Vec2 viewport)
{
    if (capture_flash_ > 0.0f) {
        const u8 a = static_cast<u8>(capture_flash_ * 210.0f);
        debug.rect_2d_filled(0.0f, 0.0f, viewport.x, viewport.y, dd_rgba(235, 245, 235, a));
    }

    if (viewfinder_) {
        const f32 frame_h = viewport.y * 0.80f;
        const f32 frame_w = frame_h * 1.6f;
        const f32 x0 = (viewport.x - frame_w) * 0.5f;
        const f32 y0 = (viewport.y - frame_h) * 0.5f;
        const f32 x1 = x0 + frame_w;
        const f32 y1 = y0 + frame_h;

        const u32 shade = dd_rgba(8, 10, 8, 215);
        debug.rect_2d_filled(0.0f, 0.0f, viewport.x, y0, shade);
        debug.rect_2d_filled(0.0f, y1, viewport.x, viewport.y, shade);
        debug.rect_2d_filled(0.0f, y0, x0, y1, shade);
        debug.rect_2d_filled(x1, y0, viewport.x, y1, shade);

        const u32 line = dd_rgba(220, 230, 220, 200);
        const f32 b = 26.0f;
        debug.rect_2d(x0, y0, x1, y1, dd_rgba(160, 170, 160, 90));
        debug.rect_2d_filled(x0, y0, x0 + b, y0 + 3.0f, line);
        debug.rect_2d_filled(x0, y0, x0 + 3.0f, y0 + b, line);
        debug.rect_2d_filled(x1 - b, y0, x1, y0 + 3.0f, line);
        debug.rect_2d_filled(x1 - 3.0f, y0, x1, y0 + b, line);
        debug.rect_2d_filled(x0, y1 - 3.0f, x0 + b, y1, line);
        debug.rect_2d_filled(x0, y1 - b, x0 + 3.0f, y1, line);
        debug.rect_2d_filled(x1 - b, y1 - 3.0f, x1, y1, line);
        debug.rect_2d_filled(x1 - 3.0f, y1 - b, x1, y1, line);

        const f32 cx = viewport.x * 0.5f;
        const f32 cy = viewport.y * 0.5f;
        debug.rect_2d_filled(cx - 14.0f, cy - 1.0f, cx + 14.0f, cy + 1.0f, line);
        debug.rect_2d_filled(cx - 1.0f, cy - 14.0f, cx + 1.0f, cy + 14.0f, line);

        debug.text_2d(text, x0 + 8.0f, y1 - 26.0f, 17.0f, kDdWhite, "EXP %02u",
                      disks_.camera_exposures_left(&terminal_.fs()));
        debug.text_2d(text, x1 - 170.0f, y1 - 26.0f, 17.0f, kDdGray, "LMB SHUTTER");
    }

    if (capture_msg_until_ > 0.0f && player_.state() == PlayerState::OnFoot) {
        debug.text_2d(text, viewport.x * 0.5f - 80.0f, viewport.y * 0.80f, 18.0f, kDdCyan,
                      "%s", capture_msg_.c_str());
    }
}

bool Game::build_video_camera(Camera& out) const
{
    if (interact_.hands().kind == ITEM_CAMERA) {
        out = camera_;
        return true;
    }
    const Entity* cam = find_pickup(ITEM_CAMERA);
    if (!cam) {
        return false;
    }
    const Vec3 eye = cam->pos + rotate(cam->rot, Vec3{0.0f, 0.06f, -0.10f});
    const Vec3 fwd = rotate(cam->rot, Vec3{0.0f, 0.0f, -1.0f});
    out = Camera{};
    out.pos = eye;
    out.look_at(eye + fwd);
    return true;
}

void Game::render_video_feed(RenderDevice& device, TerrainRenderer& terrain_renderer,
                             f32 frame_dt, f32 time)
{
    if (!terminal_.video_active()) {
        video_timer_ = 0.0f;
        terminal_.set_video_texture(0);
        return;
    }
    video_timer_ -= frame_dt;
    if (video_timer_ > 0.0f) {
        return;
    }
    video_timer_ = 0.1f;

    Camera feed;
    if (!build_video_camera(feed) || !device.video_begin(feed)) {
        return;
    }

    terrain_renderer.draw(device);
    draw_entities(device);
    carsys_.cables[CABLE_COAX].render(device);
    carsys_.cables[CABLE_BUS].render(device);
    draw_vehicle(device);
    car_render_.draw(device, carsys_, vehicle_, phys_, alpha_, 0.0f, 0);
    device.draw_sky(time);
    car_render_.draw_glass(device, carsys_, vehicle_, phys_, alpha_, time);
    device.draw_rain(weather_.rain(), weather_.wind(), Vec3{}, time);

    terminal_.set_video_texture(device.video_end());
}

void Game::render(RenderDevice& device, TerrainRenderer& terrain_renderer, DebugDraw& debug,
                  TextRenderer& text, const Input& input, Vec2 viewport, f32 time, f32 frame_dt)
{
    text.begin_frame();
    debug.begin_frame();
    ui_.begin_frame(input, &debug, &text);
    bind_entity_meshes(device);
    update_audio(frame_dt);

    env_.time_of_day = time_of_day_;
    env_.sun_dir = -sun_direction_for_time(time_of_day_);
    device.set_environment(env_);

    render_video_feed(device, terrain_renderer, frame_dt, time);
    update_terminal(input, frame_dt);

    device.set_screen_fx(screen_fx_);
    if (terminal_.powered()) {
        term_render_.render(device, disks_, terminal_.screen(), terminal_.scene(), time);
    }
    const u32 screen_texture = terminal_screen_texture();

    if (term_anim_ >= 0.995f && terminal_.powered()) {
        device.set_time(time);
        device.begin_frame(camera_, static_cast<i32>(viewport.x), static_cast<i32>(viewport.y));
        device.end_frame();
        device.post_process(time);
        device.blit_texture(0.0f, 0.0f, viewport.x, viewport.y, screen_texture, 1.0f, time);
        debug.text_2d(text, viewport.x * 0.5f - 80.0f, viewport.y - 10.0f, 14.0f, kDdGray,
                      "ESC to look away");
        debug.flush_overlay(device, text);
        text.flush(device);
        return;
    }

    device.set_weather(weather_.wetness(), weather_.overcast());
    device.set_windshield(carsys_.windshield_wet, carsys_.wiper_sweep, carsys_.glass_wet);

    collect_trees();

    if (device.shadow_begin(camera_.pos)) {
        terrain_renderer.draw(device);
        draw_entities(device);
        tree_render_.draw(device);
        draw_vehicle(device);
        car_render_.draw(device, carsys_, vehicle_, phys_, alpha_, 0.0f, 0);
        device.shadow_end();
    }

    device.set_time(time);
    device.begin_frame(camera_, static_cast<i32>(viewport.x), static_cast<i32>(viewport.y));
    device.draw_sky(time);
    terrain_renderer.draw(device);
    update_grass_press(terrain_renderer);
    terrain_renderer.draw_scrub(device, camera_.pos, time);

    draw_entities(device);
    tree_render_.draw(device);
    draw_vehicle(device);
    draw_viewmodel(device);
    draw_loose_terminal(device, screen_texture);
    draw_place_preview(device, debug);
    car_render_.draw(device, carsys_, vehicle_, phys_, alpha_, frame_dt, screen_texture);
    carsys_.cables[CABLE_COAX].render(device);
    carsys_.cables[CABLE_BUS].render(device);
    device.flush_meshes();

    device.draw_rain(weather_.rain(), weather_.wind(), cam_vel_, time);
    car_render_.draw_glass(device, carsys_, vehicle_, phys_, alpha_, time);

    draw_debug_overlays(debug);
    editor_.render(ui_, debug, text, input, *scratch_, camera_, world_, phys_, terrain_,
                   &vehicle_, &boxes_, &tapes_, viewport);
    debug.flush_world(device);

    device.end_frame();
    device.post_process(time);

    if (term_anim_ > 0.80f && terminal_.powered()) {
        const f32 fade = f_clamp01((term_anim_ - 0.80f) / 0.18f);
        device.blit_texture(0.0f, 0.0f, viewport.x, viewport.y, term_render_.texture(),
                            fade * fade, time);
    }

    capture_exposure(device);

    draw_hud(debug, text, viewport, frame_dt);
    draw_viewfinder(debug, text, viewport);
    if (term_focus_) {
        debug.text_2d(text, viewport.x * 0.5f - 80.0f, viewport.y - 10.0f, 14.0f, kDdGray,
                      "ESC to look away");
    }
    debug.flush_overlay(device, text);
    text.flush(device);
}


const Entity* Game::find_pickup(ItemKind kind) const
{
    const Pool<Entity>& pool = world_.entities();
    for (u32 idx : pool.live_indices()) {
        const Entity* e = pool.at(idx);
        if (e && e->kind == EntityKind::PartPickup
            && static_cast<ItemKind>(e->aux_kind) == kind) {
            return e;
        }
    }
    return nullptr;
}

Vec3 Game::hands_item_pos() const
{
    const bool driving = player_.driving();
    const f32 reach = driving ? 0.40f : 0.62f;
    const f32 drop = driving ? 0.22f : 0.34f;

    Vec3 pos = camera_.pos + camera_.forward() * reach
             + camera_.right() * (driving ? 0.20f : 0.30f);
    pos.y -= drop;
    return pos;
}

Vec3 Game::tower_port_pos() const
{
    return tower_pos_ + rotate(tower_rot_, kTowerPortLocal);
}

bool Game::terminal_transform(Vec3& out_pos, Quat& out_rot) const
{
    if (carsys_.parts[PART_COMPUTER].installed) {
        const RigidBody* body = phys_.body(vehicle_.body());
        if (!body) {
            return false;
        }
        const Vec3 socket = part_def(PART_COMPUTER).socket_pos - vehicle_.config().com_offset;
        out_pos = body->pos + rotate(body->rot, socket);
        out_rot = body->rot * part_computer_rest_rot();
        return true;
    }
    if (interact_.hands().kind == ITEM_COMPUTER) {
        out_pos = hands_item_pos();
        out_rot = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, -camera_.yaw);
        return true;
    }
    if (const Entity* loose = find_pickup(ITEM_COMPUTER)) {
        out_pos = loose->pos;
        out_rot = loose->rot;
        return true;
    }
    return false;
}

void Game::update_cables(f32 frame_dt)
{
    const RigidBody* body = phys_.body(vehicle_.body());
    Vec3 term_pos;
    Quat term_rot = quat_identity();

    if (!body || !terminal_transform(term_pos, term_rot)) {
        for (u32 k = 0; k < CABLE_KIND_COUNT; k++) {
            if (carsys_.cables[k].state != CableState::Stowed) {
                carsys_.cables[k].reset();
            }
        }
        carsys_.coax_target = kCoaxTargetAntenna;
        carsys_.bus_target = kBusTargetCar;
        interact_.set_cable_drag(-1);
        return;
    }

    const Vec3 car_origin = body->pos + rotate(body->rot, -vehicle_.config().com_offset);
    const Vec3 roots[2] = {kConnectorCoaxLocal, kConnectorBusLocal};
    const Vec3 jacks[2] = {kAntennaJackLocal, kBayJackLocal};

    CableObstacle term_obstacle;
    term_obstacle.pos = term_pos;
    term_obstacle.rot = term_rot;
    term_obstacle.center = Vec3{0.0f, 0.0f, 0.03f};
    term_obstacle.half = Vec3{0.23f, 0.20f, 0.24f};

    Vec3 reel_anchor;
    bool have_reel = false;
    if (interact_.hands().kind == ITEM_REEL) {
        reel_anchor = hands_item_pos();
        have_reel = true;
    } else if (const Entity* reel = find_pickup(ITEM_REEL)) {
        reel_anchor = reel->pos + Vec3{0.0f, 0.10f, 0.0f};
        have_reel = true;
    }

    for (u32 k = 0; k < CABLE_KIND_COUNT; k++) {
        Cable& cable = carsys_.cables[k];
        if (cable.state == CableState::Stowed) {
            cable.sim_init = false;
            continue;
        }
        if (cable.via_reel && !have_reel) {
            drop_cable(k);
            continue;
        }

        const Vec3* anchor = cable.via_reel ? &reel_anchor : nullptr;
        const Vec3 root = term_pos + rotate(term_rot, roots[k]);

        Vec3 end;
        if (cable.state == CableState::Plugged) {
            if (k == CABLE_COAX && carsys_.coax_target == kCoaxTargetCamera) {
                if (interact_.hands().kind == ITEM_CAMERA) {
                    end = hands_item_pos();
                } else if (const Entity* cam = find_pickup(ITEM_CAMERA)) {
                    end = cam->pos + rotate(cam->rot, Vec3{-0.13f, 0.0f, 0.0f});
                } else {
                    cable.reset();
                    carsys_.coax_target = kCoaxTargetAntenna;
                    continue;
                }
            } else if (k == CABLE_BUS && carsys_.bus_target == kBusTargetTower) {
                end = tower_port_pos();
            } else {
                end = car_origin + rotate(body->rot, jacks[k]);
            }
        } else {
            end = camera_.pos + camera_.forward() * 0.50f + Vec3{0.0f, -0.18f, 0.0f};
        }

        const f32 span = cable.span(root, end, anchor);
        const bool over_span = span > cable.max_len() * 1.06f;
        const bool over_arc = cable.sim_init
                           && cable.current_length() > cable.max_len() * 1.18f;
        if (over_span || over_arc) {
            drop_cable(k);
            continue;
        }

        CableSimInput in;
        in.root = root;
        in.end = &end;
        in.anchor = anchor;
        in.terrain = &terrain_;
        in.phys = &phys_;
        in.exclude_body = vehicle_.body();
        in.car_pos = car_origin;
        in.car_rot = body->rot;
        in.obstacles = &term_obstacle;
        in.obstacle_count = 1;
        cable.sim(in, frame_dt);
    }
}

void Game::drop_cable(u32 kind)
{
    carsys_.cables[kind].reset();
    if (kind == CABLE_COAX) {
        carsys_.coax_target = kCoaxTargetAntenna;
    }
    if (kind == CABLE_BUS) {
        carsys_.bus_target = kBusTargetCar;
    }
    if (interact_.cable_drag() == static_cast<i32>(kind)) {
        interact_.set_cable_drag(-1);
    }
    if (const RigidBody* body = phys_.body(vehicle_.body())) {
        audio_.play_at(SFX_THUMP, 0.45f, 1.15f,
                       body_point(*body, Vec3{0.3f, 0.3f, 0.0f}));
    }
}


void Game::build_term_view(const Input& input, TermView& out) const
{
    const RigidBody* body = phys_.body(vehicle_.body());

    out.sys = &carsys_;
    out.veh = &vehicle_;
    out.phys = const_cast<PhysWorld*>(&phys_);
    out.terrain = &terrain_;
    out.time_of_day = time_of_day_;
    out.weather_rain = weather_.rain();
    out.weather_wetness = weather_.wetness();
    out.weather_mode = static_cast<i32>(weather_.mode());
    out.car_pos = body ? body->pos : Vec3{};
    out.garage_pos = spawn_.car_pos;
    out.speed_kmh = f_abs(vehicle_.forward_speed(phys_)) * 3.6f;
    out.rpm = drivetrain_rpm(vehicle_.train());
    out.orbit = term_focus_ ? (input.down(Key::Right) ? 1.0f : 0.0f)
                                - (input.down(Key::Left) ? 1.0f : 0.0f)
                            : 0.0f;
    out.zoom = term_focus_ ? (input.down(Key::Up) ? 1.0f : 0.0f)
                               - (input.down(Key::Down) ? 1.0f : 0.0f)
                           : 0.0f;

    const Cable& coax = carsys_.cables[CABLE_COAX];
    const Cable& bus = carsys_.cables[CABLE_BUS];
    out.coax_state = coax.state == CableState::Plugged
                       ? (coax.linked ? PORT_LINKED : PORT_PLUGGED)
                       : PORT_UNPLUGGED;
    out.bus_state = bus.state == CableState::Plugged
                      ? (bus.linked ? PORT_LINKED : PORT_PLUGGED)
                      : PORT_UNPLUGGED;
    out.coax_camera = carsys_.coax_target == kCoaxTargetCamera;
    out.antenna_tier = !out.coax_camera && carsys_.parts[PART_ANTENNA].installed
                         ? carsys_.parts[PART_ANTENNA].variant
                         : -1;
    out.bus_tower = carsys_.bus_target == kBusTargetTower;
    out.tower_breached = tower_breached_;
    out.tower_pos = tower_pos_;
}

void Game::terminal_keys(const Input& input)
{
    for (const u32 codepoint : input.chars()) {
        if (codepoint < 128) {
            terminal_.key_char(static_cast<char>(codepoint));
        }
    }

    static const struct { Key key; TermKey term; } kMap[10] = {
        {Key::Enter, TermKey::Enter},   {Key::Backspace, TermKey::Backspace},
        {Key::Delete, TermKey::Delete}, {Key::Up, TermKey::Up},
        {Key::Down, TermKey::Down},     {Key::Q, TermKey::Quit},
        {Key::Left, TermKey::Left},     {Key::Right, TermKey::Right},
        {Key::Home, TermKey::Home},     {Key::End, TermKey::End},
    };
    for (const auto& entry : kMap) {
        if (input.pressed(entry.key)) {
            terminal_.key(entry.term);
        }
    }
}

void Game::update_terminal(const Input& input, f32 frame_dt)
{
    if (carsys_.computer_on != term_prev_power_) {
        if (carsys_.computer_on) {
            terminal_.power(true);
            term_render_.clear_persistence();
            term_power_elapsed_ = 0.0f;
        } else {
            term_power_elapsed_ = f_min(term_power_elapsed_, 0.45f);
        }
        term_prev_power_ = carsys_.computer_on;
    }
    if (carsys_.computer_on) {
        term_power_elapsed_ += frame_dt;
    } else {
        term_power_elapsed_ = f_max(term_power_elapsed_ - frame_dt * 1.6f, 0.0f);
        if (term_power_elapsed_ <= 0.0f && terminal_.powered()) {
            terminal_.power(false);
        }
    }

    const f32 vfx = terminal_.virus_fx();
    ScreenFx fx;
    fx.power_seconds = term_power_elapsed_;
    fx.burn = f_max(0.6f - carsys_.parts[PART_COMPUTER].condition, 0.0f) * 0.5f + 0.35f * vfx;
    fx.shake = f_min(carsys_.impact_cooldown * 2.0f, 0.5f) + 0.25f * vfx;
    fx.pixelate = terminal_.pixelate();
    screen_fx_ = fx;

    if (term_focus_ && !carsys_.computer_on) {
        term_focus_ = false;
    }
    term_anim_ = f_approach_exp(term_anim_, term_focus_ ? 1.0f : 0.0f, 7.0f, frame_dt);

    if (const RigidBody* body = phys_.body(vehicle_.body())) {
        terminal_.mapdata().visit(body->pos);
    }
    if (!carsys_.computer_on) {
        return;
    }

    TermView view;
    build_term_view(input, view);
    terminal_.set_disk(carsys_.floppy_disk);
    terminal_.update(view, frame_dt);

    const TermRequest req = terminal_.take_request();
    if (req.breach_open) {
        tower_breached_ = true;
        audio_.play(SFX_RATCHET, 0.4f, 1.4f);
    }
    if (req.tape_write && carsys_.tape_inserted >= 0) {
        carsys_.tape_inserted = 1 + req.tape_write_value;
        carsys_.deck_play = false;
        audio_.play(SFX_RATCHET, 0.5f, 1.6f);
    }
    if (req.time_set) {
        time_of_day_ = req.time_value - std::floor(req.time_value);
    }
    if (req.weather_mode >= 0) {
        weather_.set_mode(static_cast<WeatherMode>(req.weather_mode));
    }
    for (u32 k = 0; k < 2; k++) {
        if (req.link_port[k] && carsys_.cables[k].state == CableState::Plugged) {
            carsys_.cables[k].linked = true;
            audio_.play(SFX_THUMP, 0.14f, 3.2f);
        }
    }
    if (req.power_off) {
        carsys_.computer_on = false;
        term_focus_ = false;
    }
    if (terminal_.screen().take_click()) {
        audio_.play(SFX_THUMP, 0.10f, 2.6f);
    }
}

} // namespace anom
