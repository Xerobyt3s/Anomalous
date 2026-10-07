#include "app/game.h"
#include "engine/net/steam.h"
#include "engine/assets/asset_path.h"
#include "math/glm_bridge.h"
#include "assets/mesh_data.h"
#include "carsys/items.h"
#include "carsys/car_lights.h"
#include "core/arena.h"
#include "core/config.h"
#include "core/log.h"
#include "physics/heightfield.h"
#include "platform/filesystem.h"
#include "platform/input.h"
#include "platform/input_context.h"
#include "platform/window.h"
#include "render/cable_render.h"
#include "render/debug_draw.h"
#include "render/device.h"
#include "render/gpu_mesh.h"
#include "render/terrain_render.h"
#include "render/text.h"
#include "render/sky.h"
#include "world/destination.h"
#include "world/scenes.h"
#include "world/pickup_body.h"

#include <imgui.h>

#include <chrono>
#include <exception>
#include <limits>
#include <random>

namespace anom {
namespace {
constexpr f32 kPlaceRange = 3.5f;
constexpr f32 kPlaceNormalY = 0.55f;
constexpr f32 kChromaBase = 1.5f;
constexpr f32 kHasteFovDeg = 6.0f;
constexpr f32 kWeatherSliceDistance = 0.01f;
constexpr f32 kTankFloor = -0.11f;
constexpr f32 kTankInner = 0.22f;
constexpr Vec3 kPrinterTankOffset{0.0f, 0.28f, 0.0f};
constexpr f32 kTankRadius = 0.1f;
constexpr u32 kMaxGrassPatches = 16;
constexpr f32 kPressUprightY = 0.995f;
constexpr f32 kChromaTurn = 6.0f;
constexpr f32 kChromaTurnFull = 1.6f;
constexpr f32 kChromaAttack = 14.0f;
constexpr f32 kChromaRelease = 3.0f;
constexpr f32 kChromaHaze = 1.6f;
constexpr f32 kHazeDensity = 0.0055f;
constexpr f32 kHazeChroma = 1.0f;
constexpr f32 kHazeShimmer = 1.0f;
constexpr Vec3 kHazeTint{0.42f, 0.44f, 0.72f};
constexpr f32 kSteerKeyRateIn = 4.0f;
constexpr f32 kSteerKeyRateOut = 9.0f;
constexpr f32 kThrottleKeyRate = 4.0f;
constexpr f32 kTapTime = 0.3f;
constexpr f32 kGrassPressRange = 90.0f;
constexpr f32 kArcOnsetCharge = 0.30f;
constexpr f32 kJumpCollapse = 0.85f;
constexpr f32 kJumpArrive = 1.10f;
constexpr u32 kArcSteps = 6;
constexpr f32 kArcGlowToIntensity = 1.0f / 6.0f;
constexpr f32 kArcHaloScale = 6.0f;
constexpr Vec3 kBuildingPressMargin{0.6f, 0.0f, 0.6f};
constexpr Vec3 kTerminalScreenOffset{0.0f, 0.047f, 0.170f};
constexpr Vec3 kTerminalScreenScale{0.304f, 0.19f, 1.0f};

Vec3 body_point(const RigidBody& body, Vec3 local)
{
    return body.pos + rotate(body.rot, local);
}

}

void Game::set_environment(const Environment& env)
{
    env_ = env;
    sim_.set_time_of_day(env.time_of_day);
}

bool Game::init(RenderDevice& device, FontChain& fonts, Arena& perm, Arena& scratch, std::string_view zone_dir)
{
    perm_ = &perm;
    scratch_ = &scratch;
    if (!sim_.init(perm, scratch, zone_dir)) {
        return false;
    }
    sim_.weather().init(static_cast<u64>(std::random_device{}())
                        ^ static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!term_render_.init(fonts, scratch)) {
        log_error("game: terminal renderer init failed");
        return false;
    }
    if (const auto text = ghost::engine::readAsset("assets/data/car_fx.json")) {
        car_fx_tuning_ = ghost::game::parseCarFxTuning(*text);
    }
    car_smoke_.setTuning(car_fx_tuning_);
    skid_marks_.setTuning(car_fx_tuning_);
    if (!tree_render_.init(scratch)) {
        return false;
    }
    try {
        lightning_.emplace();
    } catch (const std::exception& e) {
        log_error("game: lightning unavailable: %s", e.what());
    }

    try {
        play_.emplace(sim_.gameplay(), sim_.physics());
        play_->setPost(device.post());
        OthersHooks others;
        others.jolt = [this](u8 id, const glm::vec3& dir, float strength, bool head) { bodies_.jolt(id, dir, strength, head); };
        others.recoil = [this](u8 id) { bodies_.recoil(id); };
        others.pose = [this](u8 id, ghost::game::BodyPose& pose, float& head, glm::vec3& color, float& height) {
            return bodies_.pose_of(id, pose, head, color, height);
        };
        play_->setOthers(std::move(others));
        bodies_.set_gun_points(play_->gunPoints());
        audio_.init(play_->gameAudio().engine());
    } catch (const std::exception& e) {
        log_error("game: revolver view unavailable: %s", e.what());
    }
    editor_.init(perm);
    editor_.set_meshes(&meshes_);
    {
        std::vector<std::string> ghost_types;
        for (const ghost::game::GhostDef& def : sim_.gameplay().ghostData().types) {
            ghost_types.push_back(def.name);
        }
        editor_.scene().set_ghost_types(std::move(ghost_types));
    }
    editor_.snapshot_world(sim_.world(), sim_.phys());

    const ZoneSpawn& spawn = sim_.spawn();
    camera_.pos = spawn.player_pos + Vec3{0.0f, kPlayerEyeHeight, 0.0f};
    camera_.yaw = spawn.player_yaw;

    (void)device;
    log_info("game: ready | %u entities | car at %.1f %.1f %.1f", sim_.world().count(),
             static_cast<f64>(spawn.car_pos.x), static_cast<f64>(spawn.car_pos.y), static_cast<f64>(spawn.car_pos.z));
    return true;
}

void Game::reset_player()
{
    sim_.reset_player(local_);
    view_.reset();
}

void Game::poll_hot_reload(Arena& scratch, f64 now)
{
    if (now >= next_cfg_poll_) {
        next_cfg_poll_ = now + 1.0;
        sim_.poll_hot_reload(scratch, now);
    }
}

void Game::clear_pending_edges()
{
    pending_.jump = false;
    pending_.crawl = false;
    pending_.interact = false;
    pending_.use_pressed = false;
    pending_.look_dx = 0.0f;
    pending_.look_dy = 0.0f;
    pending_.headlights_toggle = false;
    pending_.manual_toggle = false;
    pending_.shift = 0;
    pending_.take_key = false;
    pending_.recover = false;
    pending_.reset_car = false;
    pending_.handbrake_toggle = false;
    pending_.ignition_tap = false;
    pending_.wipers_cycle = false;
    pending_.throw_power = -1.0f;
    pending_.place_commit = false;
    pending_.stow_cable = false;
    pending_.terminal_leave = false;
    pending_.terminal_keys = 0;
    pending_.terminal_char_count = 0;
    pending_.dummy_cycle = false;
    pending_.dummy_script = -1;
    pending_.spawn_ghost = -1;
    pending_.specimen_type = -1;
    pending_.specimen_place = false;
    pending_.specimen_remove = false;
    pending_.cowboy_target = -1;
    pending_.scene = pending_scene_;
    pending_scene_ = -1;
    pending_.holster = false;
    pending_.cock = false;
    pending_.cylinder = false;
    pending_.close_cylinder = false;
    pending_.eject = false;
    pending_.speedload = false;
    pending_.turn = 0;
    pending_.load_element = -1;
    pending_.quick_fill_element = -1;
}

void Game::drive_input(const Input& input, f32 frame_dt)
{
    const f32 steer_target = (input.down(Key::D) ? 1.0f : 0.0f) - (input.down(Key::A) ? 1.0f : 0.0f);
    if (steer_target * steer_key_ < 0.0f) {
        steer_key_ = 0.0f;
    }
    steer_key_ = f_move_toward(steer_key_, steer_target,
                               (steer_target != 0.0f ? kSteerKeyRateIn : kSteerKeyRateOut) * frame_dt);
    throttle_key_ = input.down(Key::W) ? f_move_toward(throttle_key_, 1.0f, kThrottleKeyRate * frame_dt) : 0.0f;
    pending_.throttle = throttle_key_;
    pending_.reverse = input.down(Key::S) ? 1.0f : 0.0f;
    pending_.steer = steer_key_;

    if (input.pressed(Key::Space)) {
        handbrake_held_ = 0.0f;
    }
    if (input.down(Key::Space)) {
        handbrake_held_ += frame_dt;
    }
    pending_.handbrake = input.down(Key::Space);
    if (input.released(Key::Space) && handbrake_held_ < kTapTime) {
        pending_.handbrake_toggle = true;
    }

    if (input.pressed(Key::I)) {
        ignition_held_ = 0.0f;
    }
    if (input.down(Key::I)) {
        ignition_held_ += frame_dt;
    }
    pending_.crank = input.down(Key::I);
    if (input.released(Key::I) && ignition_held_ < kTapTime) {
        pending_.ignition_tap = true;
    }

    pending_.horn = input.down(Key::H);
    if (input.pressed(Key::X)) {
        pending_.wipers_cycle = true;
    }
    look_behind_ = input.down(Key::V);

    DriveIntent pad;
    drive_intent_from_pad(input, frame_dt, pad_taps_, pad);
    if (steer_key_ == 0.0f) {
        pending_.steer = pad.steer;
    }
    pending_.throttle = f_max(pending_.throttle, pad.throttle);
    pending_.reverse = f_max(pending_.reverse, pad.reverse);
    pending_.handbrake = pending_.handbrake || pad.handbrake;
    pending_.handbrake_toggle = pending_.handbrake_toggle || pad.handbrake_tap;
    pending_.crank = pending_.crank || pad.crank;
    pending_.ignition_tap = pending_.ignition_tap || pad.ignition_tap;
    pending_.horn = pending_.horn || pad.horn;
    look_behind_ = look_behind_ || pad.look_behind;
    pad_use_down_ = pad.use_down;
    pad_use_pressed_ = pad.use_pressed;
    pad_look_ = pad.look;
    if (pad.headlights) {
        pending_.headlights_toggle = !pending_.headlights_toggle;
    }
    if (pad.manual) {
        pending_.manual_toggle = !pending_.manual_toggle;
    }
    if (pad.shift_up) {
        pending_.shift++;
    }
    if (pad.shift_down) {
        pending_.shift--;
    }
    if (pad.chase) {
        toggles_.chase_cam = !toggles_.chase_cam;
    }
    if (pad.menu) {
        menu_open_ = !menu_open_;
    }
    if (input.pressed(Key::L)) {
        pending_.headlights_toggle = !pending_.headlights_toggle;
    }
    if (input.pressed(Key::T)) {
        pending_.manual_toggle = !pending_.manual_toggle;
    }
    if (input.pressed(Key::Up)) {
        pending_.shift++;
    }
    if (input.pressed(Key::Down)) {
        pending_.shift--;
    }
    if (input.pressed(Key::G)) {
        pending_.take_key = true;
    }
}

void Game::foot_input(const Input& input, f32 frame_dt)
{
    Interact& interact = sim_.interact(local_);
    steer_key_ = 0.0f;
    throttle_key_ = 0.0f;
    pending_.move_x = (input.down(Key::D) ? 1.0f : 0.0f) - (input.down(Key::A) ? 1.0f : 0.0f);
    pending_.move_z = (input.down(Key::W) ? 1.0f : 0.0f) - (input.down(Key::S) ? 1.0f : 0.0f);
    const bool moving = pending_.move_x != 0.0f || pending_.move_z != 0.0f;
    if (!moving) {
        sprint_on_ = false;
    } else if (input.pressed(Key::LeftShift)) {
        sprint_on_ = !sprint_on_;
    }
    pending_.run = sprint_on_;
    if (sprint_on_ || input.pressed(Key::Space) || input.pressed(Key::X)) {
        crouch_on_ = false;
    } else if (input.pressed(Key::C)) {
        crouch_on_ = !crouch_on_;
    }
    pending_.crouch = input.down(Key::LeftControl) || crouch_on_;
    if (input.pressed(Key::Space)) {
        pending_.jump = true;
    }
    if (input.pressed(Key::X)) {
        pending_.crawl = true;
    }
    FootIntent pad;
    foot_intent_from_pad(input, frame_dt, pad);
    if (pending_.move_x == 0.0f && pending_.move_z == 0.0f) {
        pending_.move_x = pad.move.x;
        pending_.move_z = pad.move.y;
    }
    pending_.run = pending_.run || pad.sprint;
    pending_.crouch = pending_.crouch || pad.crouch;
    pending_.jump = pending_.jump || pad.jump;
    pad_use_down_ = pad.use_down;
    pad_use_pressed_ = pad.use_pressed;
    pad_look_ = pad.look;
    look_behind_ = false;
    if (pad.menu) {
        menu_open_ = !menu_open_;
    }
    if (play_) {
        play_->readInput(input, true, frame_dt, play_frame(), pending_);
    }

    if (interact.cable_drag() >= 0) {
        if (input.pressed(Key::G)) {
            pending_.stow_cable = true;
        }
        throw_charge_ = 0.0f;
        return;
    }

    if (interact.hands().kind == ITEM_NONE) {
        throw_charge_ = 0.0f;
        place_active_ = false;
        place_valid_ = false;
        return;
    }

    if (input.down(Key::G)) {
        throw_charge_ = f_min(throw_charge_ + frame_dt, kThrowChargeMax);
    }
    if (input.released(Key::G)) {
        pending_.throw_power = throw_charge_ < 0.12f ? 0.0f : throw_charge_ / kThrowChargeMax;
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
        const std::optional<ghost::engine::RayHit> hit = sim_.physics().raycast(
            to_glm(aim.origin), to_glm(aim.origin + aim.dir * kPlaceRange));
        place_valid_ = hit && dot(from_glm(hit->normal), sim_.player(local_).up()) > kPlaceNormalY;
        if (place_valid_) {
            const f32 lift = item_cargo_half(interact.hands().kind).y + 0.015f;
            place_pos_ = from_glm(hit->point) + sim_.player(local_).up() * lift;
        }
    } else if (place_active_) {
        if (place_valid_) {
            pending_.place_commit = true;
            pending_.place_pos = place_pos_;
            pending_.place_yaw = place_yaw_;
        }
        place_active_ = false;
        place_valid_ = false;
    }
}

void Game::terminal_input(const Input& input)
{
    for (const u32 codepoint : input.chars()) {
        if (codepoint < 128 && pending_.terminal_char_count < kCommandMaxChars) {
            pending_.terminal_chars[pending_.terminal_char_count++] = static_cast<char>(codepoint);
        }
    }
    static const struct {
        Key key;
        TermKey term;
    } kMap[10] = {
        {Key::Enter, TermKey::Enter}, {Key::Backspace, TermKey::Backspace}, {Key::Delete, TermKey::Delete},
        {Key::Up, TermKey::Up},       {Key::Down, TermKey::Down},           {Key::Q, TermKey::Quit},
        {Key::Left, TermKey::Left},   {Key::Right, TermKey::Right},         {Key::Home, TermKey::Home},
        {Key::End, TermKey::End},
    };
    for (const auto& entry : kMap) {
        if (input.pressed(entry.key)) {
            pending_.terminal_keys |= 1u << static_cast<u32>(entry.term);
        }
    }
    pending_.terminal_orbit = (input.down(Key::Right) ? 1.0f : 0.0f) - (input.down(Key::Left) ? 1.0f : 0.0f);
    pending_.terminal_zoom = (input.down(Key::Up) ? 1.0f : 0.0f) - (input.down(Key::Down) ? 1.0f : 0.0f);
    if (input.pressed(Key::Escape)) {
        pending_.terminal_leave = true;
    }
}

void Game::handle_input(Window& window, const Input& input, f32 frame_dt)
{
    build_input_context(input);
    update_camera_item(input, frame_dt);
    const bool global = context_.keyboard(InputLayer::Global);

    pending_.gameplay = false;
    pending_.move_x = 0.0f;
    pending_.move_z = 0.0f;
    pending_.run = false;
    pending_.crouch = false;
    pending_.use_down = false;
    pending_.throttle = 0.0f;
    pending_.reverse = 0.0f;
    pending_.steer = 0.0f;
    pending_.handbrake = false;
    pending_.crank = false;
    pending_.terminal_orbit = 0.0f;
    pending_.terminal_zoom = 0.0f;
    pending_.trigger = false;
    pending_.aim = false;
    pending_.view_origin = camera_.pos;
    pending_.view_dir = camera_.forward();
    if (play_) {
        pending_.muzzle = from_glm(play_->aimOrigin());
        pending_.barrel_dir = from_glm(play_->aimDirection());
    }

    if (terminal_focused()) {
        window.set_cursor_captured(false);
        terminal_input(input);
        return;
    }

    if (global) {
        if (input.pressed(Key::Escape) && !editor_.active()) {
            menu_open_ = !menu_open_;
        }
        if (input.pressed(Key::R) && !editor_.active() && !gun_out()) {
            if (input.down(Key::LeftShift)) {
                pending_.reset_car = true;
            } else {
                pending_.recover = true;
            }
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
        if (input.pressed(Key::F9)) {
            toggles_.debug_panels = !toggles_.debug_panels;
        }
        if (input.pressed(Key::F11)) {
            toggles_.creatures = !toggles_.creatures;
        }
        if (input.pressed(Key::F10) && !editor_.active()) {
            pending_.dummy_cycle = true;
        }
        if (input.pressed(Key::F8) && ghost::engine::assetsEmbedded()) {
            log_info("editor: not available in a shipping build (assets are packed into the executable)");
        } else if (input.pressed(Key::F8)) {
            editor_.toggle(sim_.world(), sim_.phys());
            if (editor_.active()) {
                sim_.reset_car();
                reset_player();
            }
            build_input_context(input);
        }
        if (input.pressed(Key::C) && !editor_.active() && sim_.player(local_).driving()) {
            toggles_.chase_cam = !toggles_.chase_cam;
        }
    }

    if (editor_.active()) {
        window.set_cursor_captured(context_.cursor_captured());
        const FramebufferSize size = window.framebuffer_size();
        const Vec2 viewport{static_cast<f32>(size.width), static_cast<f32>(size.height)};
        editor_.update(input, context_, camera_, sim_.world(), sim_.phys(), sim_.terrain(), *perm_, *scratch_,
                       &sim_.vehicle(), &sim_.boxes(), viewport, sim_.zone_dir(), frame_dt);
        return;
    }

    if (toggles_.free_cam) {
        window.set_cursor_captured(camera_.fly_update(input, frame_dt));
        return;
    }

    if (!context_.keyboard(InputLayer::Gameplay)) {
        window.set_cursor_captured(false);
        return;
    }

    window.set_cursor_captured(context_.cursor_captured() && !toggles_.tuning && !toggles_.debug_panels);
    if (window.cursor_captured()) {
        if (sim_.player(local_).driving() && toggles_.chase_cam) {
            view_.chase_look(input.mouse_delta().x, input.mouse_delta().y);
        } else {
            if (!play_ || !play_->radialOpen()) {
                const f32 zoom = look_zoom();
                pending_.look_dx += input.mouse_delta().x * zoom;
                pending_.look_dy += input.mouse_delta().y * zoom;
            }
        }
    }

    pending_.gameplay = true;
    if (sim_.player(local_).driving()) {
        drive_input(input, frame_dt);
    } else {
        foot_input(input, frame_dt);
    }
    if (window.cursor_captured() && (pad_look_.x != 0.0f || pad_look_.y != 0.0f)) {
        const f32 dx = pad_look_.x / kLookSensitivity;
        const f32 dy = pad_look_.y / kLookSensitivity;
        if (sim_.player(local_).driving() && toggles_.chase_cam) {
            view_.chase_look(dx, dy);
        } else if (!play_ || !play_->radialOpen()) {
            pending_.look_dx += dx;
            pending_.look_dy += dy;
        }
    }
    pending_.use_down = input.down(Key::E) || pad_use_down_;
    if (input.pressed(Key::E) || pad_use_pressed_) {
        pending_.use_pressed = true;
    }
}

void Game::update_camera(f32 frame_dt)
{
    if (toggles_.free_cam || editor_.active()) {
        if (!(camera_.frame == quat_identity()) || camera_.roll != 0.0f) {
            const Vec3 forward = camera_.forward();
            camera_.frame = quat_identity();
            camera_.roll = 0.0f;
            camera_.look_at(camera_.pos + forward);
        }
        return;
    }
    view_.set_look_behind(look_behind_);
    view_.camera(sim_.player(local_), sim_.phys(), &sim_.vehicle(), alpha_, frame_dt, toggles_.chase_cam, pending_.look_dx,
                 pending_.look_dy, camera_);
    if (play_) {
        play_->adjustCamera(camera_, play_frame(), frame_dt);
    }
}

void Game::sample_car_fx(bool tick, f32 dt)
{
    const Vehicle& vehicle = sim_.vehicle();
    const RigidBody* car = sim_.phys().body(vehicle.body());
    if (!car || !sim_.has_car()) {
        return;
    }
    ghost::game::WheelSample wheels[kWheelCount];
    for (u32 i = 0; i < kWheelCount; i++) {
        const Wheel& w = vehicle.wheel(i);
        wheels[i].contact = to_glm(w.contact_point);
        wheels[i].normal = to_glm(w.contact_normal);
        wheels[i].forward = to_glm(rotate(car->rot, Vec3{std::sin(w.steer_rad), 0.0f, -std::cos(w.steer_rad)}));
        wheels[i].slideLat = w.slide_lat;
        wheels[i].slideLong = w.slide_long;
        wheels[i].slipAngle = w.slip_angle;
        wheels[i].slipRatio = w.slip_ratio;
        wheels[i].steered = vehicle.config().wheels[i].steered;
        wheels[i].grounded = w.grounded;
        wheels[i].road = vehicle.effects().surface_road[i] > 0.5f;
    }
    if (tick) {
        for (u32 i = 0; i < kWheelCount; i++) {
            skid_marks_.sample(i, wheels[i]);
        }
        skid_marks_.update(dt);
        return;
    }
    const Vec3 gravity = sim_.phys().gravity_at(car->pos);
    const Vec3 up = length_sq(gravity) > 1e-6f ? normalize(gravity) * -1.0f : Vec3{0.0f, 1.0f, 0.0f};
    car_smoke_.emitWheels(wheels, to_glm(car->vel), to_glm(up), dt);
    const CarSys& sys = sim_.carsys();
    const f32 heat = sys.engine_on ? sys.fluids.coolant_temp - 105.0f : 0.0f;
    const Vec3 hood = lerp(car->prev_pos, car->pos, alpha_)
                    + rotate(slerp(car->prev_rot, car->rot, alpha_),
                             car_layout().steam_point - Vec3{0.0f, sys.hood_open > 0.5f ? car_layout().steam_point.y : 0.0f, 0.0f}
                                 - vehicle.config().com_offset);
    car_smoke_.emitSteam(to_glm(hood), heat, to_glm(up), dt);
    car_smoke_.update(dt, to_glm(up), to_glm(sim_.weather().wind_velocity()));
    if (play_) {
        play_->setCarFx(&car_smoke_, &skid_marks_, car_fx_tuning_.smokeThickness);
    }
}

void Game::advance(f32 frame_dt)
{
    if (editor_.active() && net_.role() == NetSession::Role::Solo) {
        sim_.watch_islands(frame_dt);
        sim_.guard_against_falling();
        alpha_ = 0.0f;
        consume_events();
        report_island_rejections();
        return;
    }

    net_.steamUpdate(sim_, frame_dt, menu_open_);
    net_.receive(sim_);
    local_ = sim_.local_id();
    const f64 scaled = static_cast<f64>(frame_dt) * (toggles_.slow_mo && net_.role() == NetSession::Role::Solo ? 0.1 : 1.0);
    const i32 ticks = clock_.advance(scaled);
    const Vehicle& vehicle = sim_.vehicle();
    for (i32 i = 0; i < ticks; i++) {
        net_.commands(sim_, pending_, slot_cmds_);
        sim_.tick(slot_cmds_, kFixedDt);
        net_.afterTick(sim_, pending_);
        local_ = sim_.local_id();
        clear_pending_edges();
        telem_rpm_.push(drivetrain_rpm(vehicle.train()));
        telem_slip_.push((vehicle.wheel(WHEEL_RL).slip_ratio + vehicle.wheel(WHEEL_RR).slip_ratio) * 0.5f);
        telem_load_.push(vehicle.wheel(WHEEL_FL).load * 0.001f);
        telem_speed_.push(f_abs(vehicle.forward_speed(sim_.phys())) * 3.6f);
        sample_car_fx(true, kFixedDt);
    }
    sample_car_fx(false, frame_dt);
    alpha_ = static_cast<f32>(clock_.alpha());
    bodies_.update(sim_, local_, third_person(), alpha_, frame_dt);
    if (play_) {
        play_->update(frame_dt, play_frame());
        pending_.look_dy -= play_->takeRecoilPitch() / kLookSensitivity;
    }
    consume_events();
    report_island_rejections();
    update_camera(frame_dt);
    update_chroma(frame_dt);
    track_camera_velocity(frame_dt);
}

void Game::report_island_rejections()
{
    const IslandField& islands = sim_.islands();
    if (islands.generation() == islands_reported_) {
        return;
    }
    islands_reported_ = islands.generation();
    if (islands.rejected().empty()) {
        return;
    }
    FixedString<128> message;
    message.format("rejected %s", islands.rejected()[0].c_str());
    for (size_t i = 1; i < islands.rejected().size() && i < 3; i++) {
        message.format("%s, %s", FixedString<128>(message).c_str(), islands.rejected()[i].c_str());
    }
    editor_.status(message.view());
}

PlayFrame Game::play_frame() const
{
    PlayFrame f;
    const PlayerSlot* s = sim_.slot(local_);
    if (!s) {
        return f;
    }
    const Player& p = s->player;
    const MoveState& m = p.movement().state();
    f.local = local_;
    f.state = ghost_state(m);
    f.previous = ghost_state(p.movement().previous());
    f.world = f.state;
    f.world.position = to_glm(lerp(p.prev_pos(), p.pos(), alpha_));
    f.world.velocity = to_glm(m.vel);
    frame_view_angles(quat_identity(), camera_.forward(), f.world.yaw, f.world.pitch);
    f.onFoot = p.state() == PlayerState::OnFoot;
    f.armed = f.onFoot || sim_.seated_armed(*s);
    f.windowBlocked = s->window_blocked;
    f.downed = sim_.roster().downed(local_);
    f.canDraw = s->interact.hands().kind == ITEM_NONE && s->interact.cable_drag() < 0;
    f.holdingItem = s->interact.hands().kind != ITEM_NONE;
    f.lowering = s->lowering;
    f.driving = p.driving();
    f.mechanism = sim_.gun_view(local_, alpha_);
    f.gun = &s->gun.mechanism.state();
    f.pouch = &s->gun.pouch;
    f.walkSpeed = p.movement().tuning().walk_speed;
    f.hasteFov = m.haste_time > 0.0f ? kHasteFovDeg : 0.0f;
    return f;
}

bool Game::gun_out() const
{
    const PlayerSlot* s = sim_.slot(local_);
    if (!s || (s->player.state() != PlayerState::OnFoot && !sim_.seated_armed(*s))) {
        return false;
    }
    const MoveState& m = s->player.movement().state();
    return !m.holstered || m.holster < 1.0f;
}

f32 Game::look_zoom() const
{
    if (!play_ || !gun_out()) {
        return 1.0f;
    }
    const ghost::game::ViewmodelTuning& t = play_->viewmodel().tuning();
    const f32 fov = f_lerp(t.fovHip, t.fovAds, play_->viewmodel().aimBlend());
    return std::tan(fov * kDegToRad * 0.5f) / std::tan(t.fovHip * kDegToRad * 0.5f);
}

Vec3 Game::car_dash_pos() const
{
    const RigidBody* body = sim_.phys().body(sim_.vehicle().body());
    if (!body) {
        return Vec3{};
    }
    return body_point(*body, car_layout().dash_audio);
}

void Game::consume_events()
{
    using namespace ghost::game;
    if (play_) {
        play_->onEvents(sim_.events(), play_frame(), sim_.role() == SimRole::Client);
    }
    for (const GameEvent& event : sim_.events()) {
        if (const auto* e = std::get_if<TravelArmed>(&event)) {
            (void)e;
            audio_.play(SFX_RATCHET, 0.5f, 1.8f);
        } else if (std::holds_alternative<TravelStarted>(event)) {
            audio_.play(SFX_RATCHET, 0.9f, 0.6f);
        } else if (std::holds_alternative<TravelArrived>(event)) {
            prev_cam_valid_ = false;
            cam_vel_ = Vec3{};
            view_.reset();
        } else if (const auto* dropped = std::get_if<CableDropped>(&event)) {
            audio_.play_at(SFX_THUMP, 0.45f, 1.15f, from_glm(dropped->position));
        } else if (std::holds_alternative<TowerBreached>(event)) {
            audio_.play(SFX_RATCHET, 0.4f, 1.4f);
        } else if (std::holds_alternative<TapeWritten>(event)) {
            audio_.play(SFX_RATCHET, 0.5f, 1.6f);
        } else if (std::holds_alternative<PortLinked>(event)) {
            audio_.play(SFX_THUMP, 0.14f, 3.2f);
        } else if (std::holds_alternative<TerminalClicked>(event)) {
            audio_.play(SFX_THUMP, 0.10f, 2.6f);
        } else if (const auto* powered = std::get_if<TerminalPowered>(&event)) {
            if (powered->on) {
                term_render_.clear_persistence();
            }
        } else if (const auto* photo = std::get_if<PhotoTaken>(&event)) {
            if (photo->player != local_) {
                continue;
            }
            if (photo->saved) {
                capture_flash_ = 1.0f;
                audio_.play(SFX_RATCHET, 0.55f, 1.9f);
                capture_msg_.format("exposure saved - %u left", photo->left);
            } else {
                audio_.play(SFX_THUMP, 0.30f, 0.7f);
                capture_msg_.assign("film spent");
            }
            capture_msg_until_ = 2.5f;
        } else if (const auto* door = std::get_if<DoorMoved>(&event)) {
            audio_.play_at(door->opening ? SFX_DOOR_OPEN : SFX_DOOR_CLOSE, 0.7f, 1.0f, from_glm(door->position));
        } else if (const auto* hood = std::get_if<HoodMoved>(&event)) {
            audio_.play_at(SFX_HOOD, 0.6f, hood->opening ? 1.0f : 0.9f, from_glm(hood->position));
        } else if (const auto* trunk = std::get_if<TrunkMoved>(&event)) {
            audio_.play_at(SFX_HOOD, 0.6f, trunk->opening ? 1.1f : 1.0f, from_glm(trunk->position));
        } else if (const auto* started = std::get_if<EngineStarted>(&event)) {
            audio_.play_at(SFX_ENGINE_START, 0.6f, 1.0f, from_glm(started->position));
        } else if (const auto* impact = std::get_if<CarImpact>(&event)) {
            audio_.play_at(SFX_IMPACT, f_clamp(0.25f + impact->strength, 0.25f, 1.0f),
                           1.1f - 0.4f * f_clamp01(impact->strength), from_glm(impact->position));
            if (sim_.player(local_).driving()) {
                view_.kick(impact->strength);
            }
            if (impact->strength > 0.3f) {
                car_render_.spawn_sparks(from_glm(impact->position),
                                         6u + static_cast<u32>(20.0f * f_clamp01(impact->strength)));
            }
        } else if (const auto* stopped = std::get_if<EngineStopped>(&event)) {
            if (stopped->stalled) {
                audio_.play_at(SFX_THUMP, 0.5f, 0.5f, from_glm(stopped->position));
            }
        } else if (const auto* lever = std::get_if<HandbrakeMoved>(&event)) {
            audio_.play_at(SFX_RATCHET, 0.45f, lever->set ? 1.3f : 0.9f, from_glm(lever->position));
        } else if (std::holds_alternative<HeadlightsSwitched>(event)) {
            audio_.play_at(SFX_THUMP, 0.12f, 3.0f, car_dash_pos());
        } else if (std::holds_alternative<WipersSwitched>(event)) {
            audio_.play_at(SFX_THUMP, 0.12f, 2.6f, car_dash_pos());
        } else if (std::holds_alternative<KeyMoved>(event)) {
            audio_.play_at(SFX_THUMP, 0.12f, 2.4f, car_dash_pos());
        } else if (const auto* cap = std::get_if<FuelCapMoved>(&event)) {
            audio_.play_at(SFX_FLAP, 0.3f, 1.0f, from_glm(cap->position));
        } else if (std::holds_alternative<DiskMoved>(event)) {
            audio_.play_at(SFX_FLAP, 0.3f, 1.6f, car_dash_pos());
        } else if (std::holds_alternative<TapeMoved>(event)) {
            audio_.play_at(SFX_FLAP, 0.3f, 1.5f, car_dash_pos());
        } else if (const auto* fuel = std::get_if<Refuelled>(&event)) {
            audio_.play_at(SFX_WHIR, 0.4f, 1.0f, from_glm(fuel->position));
        } else if (const auto* oil = std::get_if<OilFilled>(&event)) {
            audio_.play_at(SFX_WHIR, 0.4f, 1.1f, from_glm(oil->position));
        } else if (std::holds_alternative<TankFilled>(event)) {
            audio_.play_at(SFX_WHIR, 0.4f, 0.9f, car_dash_pos());
        } else if (const auto* cargo = std::get_if<CargoMoved>(&event)) {
            audio_.play_at(SFX_THUMP, 0.4f, cargo->placed ? 1.0f : 1.2f, from_glm(cargo->position));
        } else if (std::holds_alternative<GearShifted>(event)) {
            audio_.play_at(SFX_THUMP, 0.18f, 1.8f, car_dash_pos());
        } else if (const auto* installed = std::get_if<PartInstalled>(&event)) {
            audio_.play_at(SFX_RATCHET, 0.5f, 1.0f, from_glm(installed->position));
        } else if (const auto* removed = std::get_if<PartRemoved>(&event)) {
            audio_.play_at(SFX_RATCHET, 0.5f, 0.8f, from_glm(removed->position));
        } else if (std::holds_alternative<ZoneLoaded>(event)) {
            editor_.snapshot_world(sim_.world(), sim_.phys());
            car_smoke_.clear();
            skid_marks_.clear();
        }
    }
    sim_.events().clear();
    net_.eventsConsumed();
}

void Game::update_screen_fx(f32 frame_dt)
{
    Terminal& terminal = sim_.terminal();
    const CarSys& sys = sim_.carsys();
    const f32 vfx = terminal.virus_fx();
    ScreenFx fx;
    fx.power_seconds = sim_.terminal_power_seconds();
    fx.burn = f_max(0.6f - sys.parts[PART_COMPUTER].condition, 0.0f) * 0.5f + 0.35f * vfx;
    fx.shake = f_min(sys.impact_cooldown * 2.0f, 0.5f) + 0.25f * vfx;
    fx.pixelate = terminal.pixelate();
    screen_fx_ = fx;
    term_anim_ = f_approach_exp(term_anim_, terminal_focused() ? 1.0f : 0.0f, 7.0f, frame_dt);
    arc_rng_ = arc_rng_ * 1664525u + 1013904223u;
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
    if (!net_.sendPhoto(std::span<const u8>(shot, kPhotoBytes))) {
        sim_.queue_photo(shot, local_);
    }
}

void Game::build_input_context(const Input& input)
{
    const bool cursor_visible = editor_.active() || toggles_.free_cam || toggles_.tuning || toggles_.creatures;

    context_.begin_frame();
    context_.activate(InputLayer::TextField, ui_.text_active());
    context_.activate(InputLayer::Terminal, terminal_focused());
    context_.activate(InputLayer::Editor, editor_.active());
    context_.activate(InputLayer::Panels,
                      cursor_visible && ui_.mouse_over_panel(input.mouse_pos()));
    context_.activate(InputLayer::Gameplay, !editor_.active() && !toggles_.free_cam && !menu_open_);
}

void Game::update_chroma(f32 frame_dt)
{
    const f32 turn = f_clamp01(sim_.player(local_).up_turn_rate() / kChromaTurnFull);
    f32 inside = 0.0f;
    for (u32 i = 0; i < sim_.islands().haze_count(); i++) {
        const HazeSphere& h = sim_.islands().haze()[i];
        const f32 depth = 1.0f - length(camera_.pos - h.centre) / f_max(h.radius, 1.0f);
        inside = f_max(inside, f_clamp01(depth * 2.5f));
    }
    const f32 target = kChromaBase * sim_.player(local_).field_presence() + kChromaTurn * turn + kChromaHaze * inside;
    const f32 rate = target > chroma_ ? kChromaAttack : kChromaRelease;
    chroma_ = f_approach_exp(chroma_, target, rate, frame_dt);
    if (toggles_.free_cam || editor_.active()) {
        chroma_ = 0.0f;
    }
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

void Game::update_audio(f32 frame_dt)
{
    if (!audio_.ok()) {
        return;
    }
    const RigidBody* body = sim_.phys().body(sim_.vehicle().body());
    if (!body) {
        return;
    }

    const Vec3 engine_pos = body_point(*body, car_layout().engine_audio);
    const Vec3 dash_pos = body_point(*body, car_layout().dash_audio);
    audio_.set_listener(camera_.pos, camera_.forward(), camera_.up(), sim_.player(local_).vel());
    audio_.set_car(engine_pos, body->pos, dash_pos, body->vel);
    audio_.set_occlusion(sim_.player(local_).driving() ? 0.55f : 1.0f, frame_dt);

    const CarSys& cs = sim_.carsys();
    const bool seated = sim_.player(local_).driving();
    const bool car_here = sim_.has_car();
    audio_.set_engine(drivetrain_rpm(sim_.vehicle().train()),
                      f_clamp01(sim_.vehicle().input().throttle), car_here && cs.engine_on,
                      car_here && cs.crank_active, frame_dt, sim_.vehicle().train().shifting);

    const f32 speed = f_abs(sim_.vehicle().forward_speed(sim_.phys()));
    u32 grounded = 0;
    f32 squeal = 0.0f;
    f32 grass_skid = 0.0f;
    const VehicleEffects& fx = sim_.vehicle().effects();
    for (u32 i = 0; i < kWheelCount; i++) {
        const Wheel& w = sim_.vehicle().wheel(i);
        if (!w.grounded) {
            continue;
        }
        grounded++;
        const f32 skid = skid_intensity(w.slide_lat, w.slide_long);
        squeal = f_max(squeal, skid * f_lerp(0.35f, 1.0f, fx.surface_road[i]));
        grass_skid = f_max(grass_skid, skid * (1.0f - fx.surface_road[i]));
    }
    const f32 road = sim_.terrain().road_amount(body->pos.x, body->pos.z);
    audio_.set_skid_surface(car_here ? squeal : 0.0f, car_here ? grass_skid : 0.0f, frame_dt);
    audio_.set_rolling(car_here ? speed : 0.0f, road, car_here && grounded > 0, sim_.weather().wetness(), frame_dt);
    audio_.set_horn(car_here && cs.horn_on, frame_dt);
    audio_.set_wind(length(sim_.player(local_).vel()), seated, frame_dt);
    audio_.set_rain(sim_.weather().rain() * (seated ? 0.35f : 1.0f), seated ? sim_.weather().rain() : 0.0f, frame_dt);

    const bool tape_wanted = cs.deck_play && cs.tape_inserted >= 0;
    if (tape_wanted && !audio_.tape_playing()) {
        audio_.tape_play(sim_.tapes().path(cs.tape_inserted));
    } else if (!tape_wanted && audio_.tape_playing()) {
        audio_.tape_stop();
    }
    audio_.set_tape(cs.elec.powered[CONSUMER_DECK] ? 1.0f : 0.0f, cs.tape_cond, 0.55f, frame_dt);

    const f32 sweep_delta = cs.wiper_sweep - wiper_prev_sweep_;
    if (cs.wiper_mode > 0 && f_abs(sweep_delta) > 1e-4f && f_abs(wiper_prev_delta_) > 1e-4f
        && (sweep_delta > 0.0f) != (wiper_prev_delta_ > 0.0f)) {
        audio_.play_at(SFX_WIPER, seated ? 0.28f : 0.10f, 0.95f + 0.1f * static_cast<f32>(cs.wiper_mode), car_dash_pos());
    }
    if (f_abs(sweep_delta) > 1e-4f) {
        wiper_prev_delta_ = sweep_delta;
    }
    wiper_prev_sweep_ = cs.wiper_sweep;
}

static Vec3 snap_to_hull(Vec3 local, Vec3 half)
{
    const f32 nose = f_clamp01(f_abs(local.z) / half.z);
    half.y *= 1.0f - 0.50f * nose * nose;
    Vec3 p{f_clamp(local.x, -half.x, half.x), f_clamp(local.y, -half.y, half.y),
           f_clamp(local.z, -half.z, half.z)};
    const f32 fx = f_abs(p.x) / half.x;
    const f32 fy = f_abs(p.y) / half.y;
    const f32 fz = f_abs(p.z) / half.z;
    if (fx >= fy && fx >= fz) {
        p.x = p.x < 0.0f ? -half.x : half.x;
    } else if (fy >= fz) {
        p.y = p.y < 0.0f ? -half.y : half.y;
    } else {
        p.z = p.z < 0.0f ? -half.z : half.z;
    }
    return p;
}

void Game::draw_coil_arcs(RenderDevice& device, DebugDraw& debug)
{
    if (!lightning_ || !sim_.carsys().parts[PART_COIL].installed || sim_.travel_charge() < kArcOnsetCharge) {
        return;
    }
    const RigidBody* body = sim_.phys().body(sim_.vehicle().body());
    if (!body) {
        return;
    }

    const f32 heat = f_clamp01((sim_.travel_charge() - kArcOnsetCharge) / (1.0f - kArcOnsetCharge));
    const Vec3 com = sim_.vehicle().config().com_offset;
    const Vec3 half = sim_.vehicle().config().half_extents;
    const Mat4 base = mat4_trs(body->pos, body->rot, Vec3{1.0f, 1.0f, 1.0f});

    u32 rng = arc_rng_;
    auto next = [&rng]() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return static_cast<f32>(rng & 0xFFFFFFu) / 16777216.0f;
    };

    const Vec3 tint = lerp(Vec3{0.26f, 0.46f, 1.00f}, Vec3{0.44f, 0.62f, 1.00f}, heat);
    const f32 glow = (5.0f + 7.0f * heat) * kArcGlowToIntensity;
    ghost::game::BoltStyle style;
    style.color = to_glm(tint);
    style.haloScale = kArcHaloScale;

    lightning_->begin(to_glm(device.view_proj()), to_glm(camera_.pos));

    const u32 body_arcs = 2 + static_cast<u32>(heat * 9.0f);
    for (u32 arc = 0; arc < body_arcs; arc++) {
        Vec3 walk[kArcSteps + 1];
        walk[0] = snap_to_hull(Vec3{(next() - 0.5f) * 2.2f * half.x, (next() - 0.5f) * 2.2f * half.y,
                                    (next() - 0.5f) * 2.2f * half.z},
                               half);
        const f32 step = 0.16f + 0.22f * next();
        for (u32 i = 0; i < kArcSteps; i++) {
            walk[i + 1] = snap_to_hull(walk[i] + Vec3{(next() - 0.5f) * step, (next() - 0.5f) * step,
                                                      (next() - 0.5f) * step * 1.6f},
                                       half);
        }
        ghost::game::BoltParams params;
        params.levels = 2;
        params.jaggedness = 0.16f;
        params.branches = 0;
        ghost::game::Bolt bolt;
        ghost::game::BoltPath& path = bolt.paths.emplace_back();
        for (u32 i = 0; i < kArcSteps; i++) {
            const ghost::game::Bolt segment =
                ghost::game::buildBolt(to_glm(walk[i]), to_glm(walk[i + 1]), rng + i * 7919u, params);
            const std::vector<glm::vec3>& points = segment.paths[0].points;
            for (size_t k = i == 0 ? 0 : 1; k < points.size(); k++) {
                path.points.push_back(points[k]);
            }
        }
        for (size_t k = 0; k < path.points.size(); k++) {
            path.points[k] = to_glm(transform_point(base, snap_to_hull(from_glm(path.points[k]), half) - com));
            path.width.push_back(1.0f);
            path.reach.push_back(static_cast<f32>(k) / static_cast<f32>(path.points.size() - 1));
        }
        style.width = 0.014f;
        style.intensity = (0.55f + 0.55f * heat) * glow;
        lightning_->draw(bolt, style);
    }

    const Vec3 crown = transform_point(base, part_def(PART_COIL).socket_pos - com + Vec3{0.0f, 0.43f, 0.0f});
    const u32 crown_arcs = 1 + static_cast<u32>(heat * 5.0f);
    for (u32 arc = 0; arc < crown_arcs; arc++) {
        const Vec3 roof = transform_point(base, Vec3{(next() - 0.5f) * 1.6f * half.x, half.y * 0.92f,
                                                     (next() - 0.5f) * 1.4f * half.z} - com);
        ghost::game::BoltParams params;
        params.levels = 6;
        params.jaggedness = 0.20f;
        params.branches = 1;
        style.width = 0.026f;
        style.intensity = (0.9f + 0.7f * heat) * glow;
        lightning_->draw(ghost::game::buildBolt(to_glm(crown), to_glm(roof), rng + arc * 104729u, params), style);
        next();
    }

    for (u32 w = 0; w < kWheelCount; w++) {
        const Wheel& wheel = sim_.vehicle().wheel(w);
        if (!wheel.grounded || heat < 0.25f) {
            continue;
        }
        const Vec3 contact = wheel.contact_point;
        const u32 sparks = 1 + static_cast<u32>(heat * 3.0f);
        for (u32 i = 0; i < sparks; i++) {
            const Vec3 tip = contact + Vec3{(next() - 0.5f) * 0.30f, 0.10f + next() * 0.22f,
                                            (next() - 0.5f) * 0.30f};
            ghost::game::BoltParams params;
            params.levels = 4;
            params.jaggedness = 0.22f;
            params.branches = 0;
            style.width = 0.018f;
            style.intensity = (0.7f + 0.5f * heat) * glow;
            lightning_->draw(ghost::game::buildBolt(to_glm(contact), to_glm(tip), rng + i * 31u + w, params), style);
        }
        debug.circle(contact + Vec3{0.0f, 0.02f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, 0.18f + 0.22f * heat,
                     dd_rgba(30, 20, 16, static_cast<u8>(90 + 120 * heat)));
    }

    lightning_->end();
    device.reset_state_cache();
}

void Game::draw_debug_overlays(DebugDraw& debug)
{
    if (toggles_.collision) {
        const StaticGrid& statics = sim_.phys().statics();
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
    const Pool<RigidBody>& bodies = sim_.phys().bodies();
    for (u32 idx : bodies.live_indices()) {
        const RigidBody* body = bodies.at(idx);
        if (!body) {
            continue;
        }
        debug.obb(body->pos, body->rot, body->half_extents,
                  body->asleep ? kDdGray : kDdGreen);
    }
    for (u32 idx : sim_.world().entities().live_indices()) {
        const Entity* e = sim_.world().entities().at(idx);
        PickupState pickup;
        if (e && pickup_body_state(sim_.phys(), *e, pickup)) {
            debug.obb(pickup.pos, pickup.rot, item_cargo_half(static_cast<ItemKind>(e->aux_kind)),
                      pickup.active ? kDdGreen : kDdGray);
        }
    }
    for (u32 i = 0; i < kWheelCount; i++) {
        const Wheel& w = sim_.vehicle().wheel(i);
        if (!w.grounded) {
            continue;
        }
        debug.line(w.contact_point, w.contact_point + w.force_susp * 0.0002f, kDdCyan);
        debug.line(w.contact_point, w.contact_point + w.force_long * 0.0004f, kDdGreen);
        debug.line(w.contact_point, w.contact_point + w.force_lat * 0.0004f, kDdRed);
    }
}

void Game::bind_entity_meshes(RenderDevice& device)
{
    meshes_.bind(device.assets(), sim_.world());
}

void Game::push_tank_fill(const glm::mat4& base, const glm::vec3& center, const u16* doses)
{
    if (!doses) {
        return;
    }
    const ghost::game::AmmoData& ammo = sim_.gameplay().ammo();
    const f32 capacity = f_max(static_cast<f32>(sim_.vehicle().config().synth.tank_capacity), 1.0f);
    f32 level = kTankFloor;
    for (u32 m = 0; m < kSynthMaterials && m < ammo.materials.size(); m++) {
        if (doses[m] == 0) {
            continue;
        }
        const f32 height = f_min(static_cast<f32>(doses[m]) / capacity, 1.0f) * kTankInner;
        const glm::mat4 model = base * glm::translate(glm::mat4(1.0f), center + glm::vec3(0.0f, level, 0.0f))
                              * glm::scale(glm::mat4(1.0f), glm::vec3(kTankRadius, f_max(height, 0.002f), kTankRadius));
        tank_fill_.push_back({model, ammo.materials[m].color});
        level += height;
    }
}

void Game::draw_entities(RenderDevice& device)
{
    const Pool<Entity>& pool = sim_.world().entities();
    for (u32 idx : pool.live_indices()) {
        const Entity* e = pool.at(idx);
        const GpuMesh* mesh = meshes_.get(idx);
        if (e && mesh && e->kind != EntityKind::Tree) {
            device.draw_mesh(mesh, mat4_trs(e->pos, e->rot, Vec3{e->scale, e->scale, e->scale}));
        }
    }
}

void Game::collect_trees()
{
    tree_render_.begin_frame();
    const Pool<Entity>& pool = sim_.world().entities();
    for (u32 idx : pool.live_indices()) {
        const Entity* e = pool.at(idx);
        if (e && e->kind == EntityKind::Tree) {
            tree_render_.submit(e->aux_kind,
                                mat4_trs(e->pos, e->rot, Vec3{e->scale, e->scale, e->scale}));
        }
    }
}

void Game::draw_grass_patches(RenderDevice& device, TerrainRenderer& terrain_renderer, f32 time)
{
    ScrubPatch patches[kMaxGrassPatches];
    u32 count = 0;
    const Pool<Entity>& pool = sim_.world().entities();
    for (u32 idx : pool.live_indices()) {
        const Entity* e = pool.at(idx);
        if (!e || count >= kMaxGrassPatches || e->mesh_name.empty()
            || (e->kind != EntityKind::Building && e->kind != EntityKind::StaticMesh
                && e->kind != EntityKind::Island)) {
            continue;
        }
        FixedString<48> patch_name;
        if (e->kind == EntityKind::Island) {
            patch_name.format("isl_%s", e->mesh_name.c_str());
        } else {
            patch_name.assign(e->mesh_name.view());
        }
        const u32 texture = terrain_renderer.patch_texture(*scratch_, patch_name.view());
        if (!texture) {
            continue;
        }
        ScrubPatch& p = patches[count++];
        p.pos = e->pos;
        p.rot = e->rot;
        p.scale = e->scale;
        p.texture = texture;
    }
    terrain_renderer.draw_scrub_patches(device, camera_.pos, time, patches, count);
}

void Game::sync_island_gpu(RenderDevice& device, TerrainRenderer& terrain_renderer)
{
    const std::vector<BuiltIsland>& built = sim_.islands().islands();
    if (islands_uploaded_ != sim_.islands().generation()) {
        island_meshes_.assign(built.size(), nullptr);
        for (size_t i = 0; i < built.size(); i++) {
            const BuiltIsland& island = built[i];
            AmshSubmesh sub{};
            sub.first_index = 0;
            sub.index_count = static_cast<u32>(island.mesh.indices.size());
            std::snprintf(sub.material, sizeof(sub.material), "%.*s", static_cast<int>(kIslandMaterial.size()),
                          kIslandMaterial.data());
            MeshData data;
            data.vertices = std::span<const AmshVertex>(island.mesh.vertices);
            data.indices = std::span<const u32>(island.mesh.indices);
            data.submeshes = std::span<const AmshSubmesh>(&sub, 1);
            data.bounds = island.mesh.bounds;
            FixedString<48> name;
            name.format("isl_%s", island.name.c_str());
            island_meshes_[i] = device.assets().create_mesh(name.view(), data);
        }
        if (!debris_meshes_[0]) {
            for (u32 v = 0; v < IslandField::kRockVariants + IslandField::kTurfVariants; v++) {
                const bool rock = v < IslandField::kRockVariants;
                const BakedMesh& mesh = rock ? sim_.islands().rock_variant(v)
                                             : sim_.islands().turf_variant(v - IslandField::kRockVariants);
                AmshSubmesh sub{};
                sub.index_count = static_cast<u32>(mesh.indices.size());
                std::snprintf(sub.material, sizeof(sub.material), "%.*s", static_cast<int>(kIslandMaterial.size()),
                              kIslandMaterial.data());
                MeshData data;
                data.vertices = std::span<const AmshVertex>(mesh.vertices);
                data.indices = std::span<const u32>(mesh.indices);
                data.submeshes = std::span<const AmshSubmesh>(&sub, 1);
                data.bounds = mesh.bounds;
                FixedString<32> name;
                name.format("debris_%s_%u", rock ? "rock" : "turf", rock ? v : v - IslandField::kRockVariants);
                debris_meshes_[v] = device.assets().create_mesh(name.view(), data);
            }
        }
        islands_uploaded_ = sim_.islands().generation();
        islands_patches_dirty_ = true;
    }
    Pool<Entity>& pool = sim_.world().entities();
    for (u32 idx : pool.live_indices()) {
        Entity* e = pool.at(idx);
        if (!e || e->kind != EntityKind::Island) {
            continue;
        }
        const GpuMesh* mesh = nullptr;
        for (size_t i = 0; i < built.size() && i < island_meshes_.size(); i++) {
            if (built[i].name == e->mesh_name.view()) {
                mesh = island_meshes_[i];
            }
        }
        meshes_.set(idx, mesh);
    }
    if (islands_patches_dirty_) {
        for (const BuiltIsland& island : built) {
            FixedString<48> name;
            name.format("isl_%s", island.name.c_str());
            terrain_renderer.register_patch_heights(name.view(), island.grass.data(), IslandField::kGrassRes,
                                                    IslandField::kGrassRes);
        }
        islands_patches_dirty_ = false;
    }
}

void Game::draw_debris(RenderDevice& device, f32 time)
{
    if (!debris_meshes_[0]) {
        return;
    }
    for (const DebrisInstance& d : sim_.islands().debris()) {
        const u32 slot = d.kind == DebrisKind::Rock ? d.variant : IslandField::kRockVariants + d.variant;
        device.draw_mesh(debris_meshes_[slot], IslandField::debris_transform(d, time));
    }
}

void Game::update_grass_press(TerrainRenderer& terrain_renderer)
{
    terrain_renderer.clear_press_volumes();

    if (const RigidBody* body = sim_.phys().body(sim_.vehicle().body())) {
        const Vec3 com = sim_.vehicle().config().com_offset;
        const Vec3 centre = body->pos + rotate(body->rot, car_layout().press_center - com);
        terrain_renderer.add_press_volume(centre, car_layout().press_half, body->rot);
        terrain_renderer.add_patch_press(centre, car_layout().press_half, body->rot);
    }

    const Pool<Entity>& pool = sim_.world().entities();
    for (u32 idx : pool.live_indices()) {
        const Entity* e = pool.at(idx);
        const GpuMesh* mesh = meshes_.get(idx);
        if (!e || e->kind != EntityKind::Building || !mesh || !mesh->loaded) {
            continue;
        }
        if (distance_sq(e->pos, camera_.pos) > kGrassPressRange * kGrassPressRange) {
            continue;
        }
        const Aabb& b = mesh->bounds;
        if (rotate(e->rot, Vec3{0.0f, 1.0f, 0.0f}).y < kPressUprightY) {
            continue;
        }
        const Vec3 local_centre = (b.min + b.max) * 0.5f * e->scale;
        const Vec3 half = (b.max - b.min) * 0.5f * e->scale + kBuildingPressMargin;
        terrain_renderer.add_press_volume(e->pos + rotate(e->rot, local_centre), half, e->rot);
    }
}

void Game::draw_vehicle(RenderDevice& device)
{
    const RigidBody* body = sim_.phys().body(sim_.vehicle().body());
    const VehicleConfig& cfg = sim_.vehicle().config();
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
        Wheel w = sim_.vehicle().wheel(i);
        w.compression = f_lerp(w.prev_compression, w.compression, alpha_);
        w.spin_angle = f_wrap_angle(w.prev_spin_angle + f_wrap_angle(w.spin_angle - w.prev_spin_angle) * alpha_);
        const f32 drop = cfg.wheels[i].travel - w.compression;
        const f32 radius_mul = sim_.vehicle().effects().tire_radius_mul[i];
        const f32 wobble = radius_mul < 0.95f ? std::sin(w.spin_angle) * (1.0f - radius_mul) * 0.10f
                                              : 0.0f;
        const Vec3 local = w.attach_local - Vec3{0.0f, drop - wobble, 0.0f};
        const Vec3 center = pos + rotate(rot, local);

        const Quat q = wheel_visual_rot(rot, w, cfg.wheels[i].pos.x > 0.0f);
        device.draw_mesh(wheel_mesh, mat4_trs(center, q, Vec3{1.0f, radius_mul, radius_mul}));
    }
}

u32 Game::terminal_screen_texture() const
{
    return sim_.terminal().powered() ? term_render_.texture() : 0;
}

void Game::draw_loose_terminal(RenderDevice& device, u32 screen_texture)
{
    if (sim_.carsys().parts[PART_COMPUTER].installed) {
        return;
    }
    const Entity* loose = sim_.find_pickup(ITEM_COMPUTER);
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
    if (sim_.carsys().floppy_disk >= 0) {
        device.draw_mesh(device.assets().mesh("part_floppy"),
                         base * mat4_trs(Vec3{0.0f, -0.119f, 0.223f}, quat_identity(),
                                         Vec3{1.0f, 1.0f, 1.0f}));
    }
}

void Game::draw_place_preview(RenderDevice& device, DebugDraw& debug)
{
    if (!place_active_ || !place_valid_ || sim_.interact(local_).hands().kind == ITEM_NONE) {
        return;
    }
    const ItemKind kind = sim_.interact(local_).hands().kind;
    const Quat yaw_rot = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, place_yaw_);
    const Quat rot = yaw_rot * item_cargo_rot(kind);
    const Vec3 pos = place_pos_ - rotate(rot, item_mesh_center(kind));

    device.draw_mesh(device.assets().mesh(item_mesh(kind)),
                     mat4_trs(pos, rot, Vec3{1.0f, 1.0f, 1.0f}));
    debug.obb(place_pos_, yaw_rot, item_cargo_half(kind), dd_rgba(120, 220, 140, 255));
}

void Game::draw_viewmodel(RenderDevice& device)
{
    const Item& held = sim_.interact(local_).hands();
    if (held.kind == ITEM_NONE || editor_.active() || toggles_.free_cam || terminal_focused() || place_active_
        || viewfinder_) {
        return;
    }
    if (sim_.player(local_).state() != PlayerState::OnFoot && !sim_.player(local_).driving()) {
        return;
    }
    if (sim_.interact(local_).action() == InteractAction::PlaceCargo) {
        return;
    }

    AssetCache& assets = device.assets();

    if (sim_.interact(local_).action() == InteractAction::Refuel && held.kind == ITEM_JERRYCAN) {
        if (const RigidBody* body = sim_.phys().body(sim_.vehicle().body())) {
            const f32 pour = sim_.interact(local_).hold_progress();
            const Vec3 local = lerp(car_layout().jerrycan_from, car_layout().jerrycan_to, pour)
                             - sim_.vehicle().config().com_offset;
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
    if (sim_.player(local_).driving()) {
        scale *= 0.75f;
    }

    Mat4 base;
    if (play_) {
        const glm::mat4 to_world = glm::inverse(to_glm(camera_.view())) * play_->itemToView();
        base = from_glm(to_world) * mat4_trs(-item_mesh_center(held.kind) * scale, quat_identity(), Vec3{scale, scale, scale});
    } else {
        const Quat rot = camera_.frame * quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, -camera_.yaw);
        const Vec3 pos = hands_item_pos() - rotate(rot, item_mesh_center(held.kind)) * scale;
        base = mat4_trs(pos, rot, Vec3{scale, scale, scale});
    }
    device.begin_viewmodel();
    device.draw_mesh(assets.mesh(item_mesh(held.kind)), base);

    const u32 screen_texture = terminal_screen_texture();
    if (held.kind == ITEM_COMPUTER && screen_texture) {
        device.draw_lit_quad(base * mat4_trs(kTerminalScreenOffset, quat_identity(),
                                             kTerminalScreenScale),
                             screen_texture, 0.0f);
    }
    device.end_viewmodel();
}

void Game::update_camera_item(const Input& input, f32 frame_dt)
{
    viewfinder_ = sim_.player(local_).state() == PlayerState::OnFoot && !terminal_focused() && !toggles_.free_cam
               && !editor_.active() && sim_.interact(local_).hands().kind == ITEM_CAMERA
               && input.down(MouseButton::Right);
    if (viewfinder_ && input.pressed(MouseButton::Left)) {
        capture_pending_ = true;
    }
    capture_flash_ = f_max(capture_flash_ - frame_dt * 3.0f, 0.0f);
    if (capture_msg_until_ > 0.0f) {
        capture_msg_until_ -= frame_dt;
    }
}

bool Game::build_video_camera(Camera& out) const
{
    if (sim_.interact(local_).hands().kind == ITEM_CAMERA) {
        out = camera_;
        return true;
    }
    const Entity* cam = sim_.find_pickup(ITEM_CAMERA);
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
    if (!sim_.terminal().video_active()) {
        video_timer_ = 0.0f;
        sim_.terminal().set_video_texture(0);
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
    draw_cable(device, sim_.carsys().cables[CABLE_COAX]);
    draw_cable(device, sim_.carsys().cables[CABLE_BUS]);
    draw_vehicle(device);
    car_render_.draw(device, sim_.carsys(), sim_.vehicle(), sim_.phys(), alpha_, 0.0f, 0);
    device.draw_sky(time);
    car_render_.draw_glass(device, sim_.carsys(), sim_.vehicle(), sim_.phys(), alpha_, time);
    device.draw_rain(sim_.weather().rain(), sim_.weather().wind(), Vec3{}, time);
    device.draw_snow(sim_.weather().snow(), sim_.weather().wind(), time);

    sim_.terminal().set_video_texture(device.video_end());
}

void Game::render(RenderDevice& device, TerrainRenderer& terrain_renderer, DebugDraw& debug,
                  TextRenderer& text, const Input& input, Vec2 viewport, f32 time, f32 frame_dt)
{
    text.begin_frame();
    debug.begin_frame();
    ui_.begin_frame(input, &debug, &text);
    bind_entity_meshes(device);
    update_audio(frame_dt);

    env_.time_of_day = sim_.time_of_day();
    env_.sun_dir = -sun_direction_for_time(sim_.time_of_day());
    device.set_environment(env_);

    render_video_feed(device, terrain_renderer, frame_dt, time);
    update_screen_fx(frame_dt);

    if (sim_.take_terrain_dirty() || terrain_dirty_) {
        terrain_renderer.shutdown();
        if (!terrain_renderer.init(device, *scratch_, sim_.terrain().heightfield(),
                                   sim_.terrain().roadmask(), sim_.terrain().mask_size())) {
            log_error("travel: terrain rebuild failed");
        }
        terrain_dirty_ = false;
        islands_patches_dirty_ = true;
    }
    sync_island_gpu(device, terrain_renderer);

    device.set_screen_fx(screen_fx_);
    Vec4 haze[IslandField::kMaxHaze];
    for (u32 i = 0; i < sim_.islands().haze_count(); i++) {
        const HazeSphere& h = sim_.islands().haze()[i];
        haze[i] = Vec4{h.centre.x, h.centre.y, h.centre.z, h.radius};
    }
    device.set_haze(haze, sim_.islands().haze_count(), kHazeDensity, kHazeChroma, kHazeShimmer, kHazeTint,
                    IslandField::kHazeMargin);
    f32 warp = 0.0f;
    f32 flash = 0.0f;
    travel_warp(warp, flash);
    if (sim_.terminal().powered()) {
        term_render_.render(device, sim_.disks(), sim_.terminal().screen(), sim_.terminal().scene(), time);
    }
    const u32 screen_texture = terminal_screen_texture();

    if (term_anim_ >= 0.995f && sim_.terminal().powered()) {
        device.set_time(time);
        device.begin_frame(camera_, static_cast<i32>(viewport.x), static_cast<i32>(viewport.y));
        device.end_frame();
        device.set_travel_warp(warp, flash);
        device.set_chroma(chroma_, 0.0f);
        device.post_process(time);
        device.blit_texture(0.0f, 0.0f, viewport.x, viewport.y, screen_texture, 1.0f, time);
        debug.text_2d(text, viewport.x * 0.5f - 80.0f, viewport.y - 10.0f, 14.0f, kDdGray,
                      "ESC to look away");
        debug.flush_overlay(device, text);
        text.flush(device);
        return;
    }

    device.set_weather(sim_.weather().overcast(), sim_.weather().wetness());
    device.set_snow(sim_.weather().snow_cover(), sim_.weather().snow(), sim_.weather().wind());
    device.set_windshield(sim_.carsys().windshield_wet, sim_.carsys().wiper_sweep, sim_.carsys().glass_wet);

    collect_trees();

    {
        PointLight lights[kCarLightMax];
        u32 light_count = 0;
        const RigidBody* car = sim_.phys().body(sim_.vehicle().body());
        if (car && sim_.has_car()) {
            light_count = car_lights(lerp(car->prev_pos, car->pos, alpha_), slerp(car->prev_rot, car->rot, alpha_),
                                     sim_.vehicle(), sim_.carsys(), lights);
        }
        PointLight extra[RenderDevice::kMaxPointLights];
        u32 extra_count = 0;
        if (play_) {
            for (const ghost::game::FogLight& light : play_->frameLights()) {
                if (extra_count < RenderDevice::kMaxPointLights) {
                    extra[extra_count] = PointLight{};
                    extra[extra_count].pos = from_glm(light.position);
                    extra[extra_count].color = from_glm(light.color);
                    extra_count++;
                }
            }
        }
        if (play_) {
            std::vector<LampGlare> glare;
            for (u32 i = 0; i < light_count; i++) {
                if (lights[i].cone > -1.0f) {
                    glare.push_back({to_glm(lights[i].pos), to_glm(lights[i].dir),
                                     glm::normalize(to_glm(lights[i].color) + glm::vec3(1e-4f)) * 1.2f});
                }
            }
            play_->setLampGlare(std::move(glare), sim_.weather().wetness());
        }
        PointLight merged[RenderDevice::kMaxPointLights];
        const u32 merged_count = merge_lights(lights, light_count, extra, extra_count, merged,
                                              RenderDevice::kMaxPointLights);
        device.set_point_lights(merged, merged_count);
    }

    props_.clear();
    for (u32 idx : sim_.world().entities().live_indices()) {
        const Entity* e = sim_.world().entities().at(idx);
        if (e && e->kind == EntityKind::Prop) {
            const glm::vec3 color{static_cast<f32>((e->aux_data >> 16) & 0xFFu) / 255.0f,
                                  static_cast<f32>((e->aux_data >> 8) & 0xFFu) / 255.0f, static_cast<f32>(e->aux_data & 0xFFu) / 255.0f};
            props_.push_back({to_glm(e->pos), to_glm(e->half), color, static_cast<ghost::game::Surface>(e->aux_kind)});
        } else if (e && e->kind == EntityKind::Bench) {
            props_.push_back({to_glm(e->pos - Vec3{0.0f, kBenchHalf.y, 0.0f}), to_glm(kBenchHalf), glm::vec3(0.36f, 0.24f, 0.13f),
                              ghost::game::Surface::Wood});
        }
    }
    if (device.shadow_begin(camera_.pos)) {
        terrain_renderer.draw(device);
        draw_entities(device);
        draw_debris(device, time);
        tree_render_.draw(device);
        draw_vehicle(device);
        car_render_.draw(device, sim_.carsys(), sim_.vehicle(), sim_.phys(), alpha_, 0.0f, 0);
        if (play_) {
            device.flush_meshes();
            play_->renderPropShadows(props_, to_glm(device.shadow_matrix()), device.shaders().program("shadow"));
            device.reset_state_cache();
        }
        device.shadow_end();
    }

    device.set_time(time);
    device.begin_frame(camera_, static_cast<i32>(viewport.x), static_cast<i32>(viewport.y));
    device.draw_sky(time);
    terrain_renderer.draw(device);
    update_grass_press(terrain_renderer);
    terrain_renderer.draw_scrub(device, camera_.pos, time);
    draw_grass_patches(device, terrain_renderer, time);

    draw_entities(device);
    draw_debris(device, time);
    tree_render_.draw(device);
    draw_vehicle(device);
    draw_viewmodel(device);
    draw_loose_terminal(device, screen_texture);
    draw_place_preview(device, debug);
    car_render_.draw(device, sim_.carsys(), sim_.vehicle(), sim_.phys(), alpha_, frame_dt, screen_texture);
    draw_cable(device, sim_.carsys().cables[CABLE_COAX]);
    draw_cable(device, sim_.carsys().cables[CABLE_BUS]);
    device.flush_meshes();
    hud_view_proj_ = device.view_proj();
    bodies_.draw(device.view_proj(), camera_.pos, -env_.sun_dir, time);
    device.set_fall_rotation(quat_from_to(Vec3{0.0f, -1.0f, 0.0f}, sim_.phys().up_at(camera_.pos) * -1.0f,
                                          Vec3{1.0f, 0.0f, 0.0f}));
    const auto draw_weather = [&]() {
        device.reset_state_cache();
        device.draw_rain(sim_.weather().rain(), sim_.weather().wind(), cam_vel_, time);
        device.draw_snow(sim_.weather().snow(), sim_.weather().wind(), time);
    };
    loose_tanks_.clear();
    for (u32 idx : sim_.world().entities().live_indices()) {
        const Entity* e = sim_.world().entities().at(idx);
        if (!e || e->kind != EntityKind::PartPickup) {
            continue;
        }
        const ItemKind kind = static_cast<ItemKind>(e->aux_kind);
        const i32 id = static_cast<i32>(e->aux_data);
        const Vec3 scale{e->scale, e->scale, e->scale};
        if (kind == ITEM_TANK) {
            const LooseTank* tank = sim_.carsys().loose_tank(id);
            loose_tanks_.push_back({mat4_trs(e->pos, e->rot, scale), tank ? tank->doses : nullptr});
        } else if (kind == ITEM_PRINTER) {
            const LoosePrinter* printer = sim_.carsys().loose_printer(id);
            if (printer && printer->has_tank) {
                const Mat4 model = mat4_trs(e->pos + rotate(e->rot, kPrinterTankOffset * e->scale), e->rot, scale);
                device.draw_mesh(device.assets().mesh("part_tank"), model);
                loose_tanks_.push_back({model, printer->bay.tank});
            }
        }
    }
    const auto draw_glass = [&]() {
        device.reset_state_cache();
        car_render_.draw_glass(device, sim_.carsys(), sim_.vehicle(), sim_.phys(), alpha_, time);
        for (const LooseTankView& tank : loose_tanks_) {
            device.draw_glass(device.assets().mesh("part_tank_glass"), tank.model, time);
        }
    };
    const auto draw_arcs = [&]() { draw_coil_arcs(device, debug); };
    const bool sliced = play_ && !editor_.active() && !toggles_.free_cam;
    if (play_) {
        FrameLights prop_lights;
        prop_lights.sunDirection = to_glm(-env_.sun_dir);
        play_->renderProps(props_, to_glm(device.view_proj()), to_glm(camera_.pos), prop_lights);
        tank_fill_.clear();
        const RigidBody* car = sim_.phys().body(sim_.vehicle().body());
        if (car && sim_.has_car() && sim_.carsys().parts[PART_TANK].installed) {
            const glm::mat4 base = to_glm(mat4_trs(lerp(car->prev_pos, car->pos, alpha_), slerp(car->prev_rot, car->rot, alpha_),
                                                   Vec3{1.0f, 1.0f, 1.0f}));
            push_tank_fill(base, to_glm(part_def(PART_TANK).socket_pos - sim_.vehicle().config().com_offset),
                           sim_.carsys().synth.tank);
        }
        for (const LooseTankView& tank : loose_tanks_) {
            push_tank_fill(to_glm(tank.model), glm::vec3(0.0f), tank.doses);
        }
        play_->renderCylinders(tank_fill_, to_glm(device.view_proj()), to_glm(camera_.pos), prop_lights);
    }
    bodies_.held_items(held_items_);
    for (const HeldItem& held : held_items_) {
        device.draw_mesh(device.assets().mesh(item_mesh(held.kind)), held.model);
    }
    if (play_) {
        bodies_.guns(other_guns_);
        play_->renderOtherGuns(other_guns_, to_glm(device.view_proj()), to_glm(camera_.pos));
    }
    if (sliced) {
        f32 glass_distance = 0.0f;
        if (const RigidBody* body = sim_.phys().body(sim_.vehicle().body())) {
            glass_distance = length(body->pos - camera_.pos);
        }
        see_through_.clear();
        see_through_.push_back({glass_distance, draw_glass});
        see_through_.push_back({glass_distance, draw_arcs});
        see_through_.push_back({kWeatherSliceDistance, draw_weather});
        see_through_.push_back({0.0f, {}, [&](const ghost::game::DrawWindow& w) { device.draw_island_haze(w.nearD, w.farD, play_->sceneDepthCopy()); }});
        const f32 aspect = viewport.x / f_max(viewport.y, 1.0f);
        FrameLights lights;
        lights.sunDirection = to_glm(-env_.sun_dir);
        play_->prepareGun(camera_, aspect);
        play_->renderWorld(to_glm(device.view_proj()), to_glm(camera_.pos), lights);
        device.flush_meshes();
        play_->renderEffects(to_glm(device.view_proj()), camera_, alpha_, see_through_);
        if (!terminal_focused()) {
            play_->renderGun(camera_, aspect, play_frame(), lights);
        }
        play_->finishFrame(to_glm(device.view_proj()), to_glm(camera_.pos));
    }

    if (!sliced) {
        device.draw_island_haze(-1.0f, std::numeric_limits<f32>::infinity(), device.copy_scene_depth());
        draw_weather();
        draw_glass();
        draw_arcs();
    }

    draw_debug_overlays(debug);
    editor_.render(ui_, debug, text, input, *scratch_, camera_, sim_.world(), sim_.phys(), sim_.terrain(),
                   &sim_.vehicle(), &sim_.boxes(), &sim_.tapes(), viewport);
    debug.flush_world(device);

    device.end_frame();
    device.set_travel_warp(warp, flash);
    device.set_chroma(chroma_, 0.0f);
    device.post_process(time);

    if (term_anim_ > 0.80f && sim_.terminal().powered()) {
        const f32 fade = f_clamp01((term_anim_ - 0.80f) / 0.18f);
        device.blit_texture(0.0f, 0.0f, viewport.x, viewport.y, term_render_.texture(),
                            fade * fade, time);
    }

    capture_exposure(device);

    debug.flush_overlay(device, text);
    text.flush(device);
}

Vec3 Game::hands_item_pos() const
{
    const bool driving = sim_.player(local_).driving();
    const f32 reach = driving ? 0.40f : 0.62f;
    const f32 drop = driving ? 0.22f : 0.34f;

    Vec3 pos = camera_.pos + camera_.forward() * reach
             + camera_.right() * (driving ? 0.20f : 0.30f);
    pos.y -= drop;
    return pos;
}

void Game::travel_warp(f32& warp, f32& flash) const
{
    warp = 0.0f;
    flash = 0.0f;
    if (sim_.travel_jump() < 0.0f) {
        return;
    }
    if (sim_.travel_jump() < kJumpCollapse) {
        const f32 t = sim_.travel_jump() / kJumpCollapse;
        warp = t * t;
        flash = f_clamp01((t - 0.82f) / 0.18f);
    } else {
        const f32 t = f_clamp01((sim_.travel_jump() - kJumpCollapse) / kJumpArrive);
        warp = (1.0f - t) * (1.0f - t);
        flash = 1.0f - f_clamp01(t / 0.30f);
    }
}

void Game::draw_imgui(Window& window)
{
    draw_viewfinder();
    draw_play_hud();
    draw_debug_panels();
    draw_creatures_panel();
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    if (net_.role() == NetSession::Role::Client && !net_.welcomed()) {
        const ImVec2 extent = ImGui::CalcTextSize(net_.status().c_str());
        ImGui::GetForegroundDrawList()->AddText({(size.x - extent.x) * 0.5f, size.y * 0.3f},
                                                ImGui::GetColorU32({1.0f, 1.0f, 1.0f, 0.9f}), net_.status().c_str());
    }
    if (!menu_open_) {
        return;
    }
    ImGui::SetNextWindowPos({size.x * 0.5f - 170.0f, 40.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({340.0f, 0.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Multiplayer");
    if (net_.role() == NetSession::Role::Solo) {
        if (ImGui::InputText("Name", name_edit_, sizeof(name_edit_))) {
            net_.setName(name_edit_);
        }
        if (ghost::engine::steam::available()) {
            ImGui::SeparatorText("Steam");
            if (net_.steamJoining()) {
                ImGui::TextDisabled("Joining...");
            } else {
                if (ImGui::Button("Host for friends")) {
                    net_.hostSteam(sim_);
                }
                if (net_.friendGames().empty()) {
                    ImGui::TextDisabled("No friends hosting right now");
                } else {
                    ImGui::Text("Friends playing:");
                    for (std::size_t i = 0; i < net_.friendGames().size(); ++i) {
                        ImGui::PushID(static_cast<int>(i));
                        ImGui::BulletText("%s", net_.friendGames()[i].name.c_str());
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Join")) {
                            net_.joinSteam(sim_, net_.friendGames()[i].lobby);
                        }
                        ImGui::PopID();
                    }
                }
            }
        } else {
            ImGui::TextDisabled("Steam not running: direct connection only");
        }
        ImGui::SeparatorText("Direct connection");
        if (ImGui::Button("Host a game")) {
            net_.host(sim_, ghost::game::net::kDefaultPort);
        }
        ImGui::InputText("Host address", join_address_, sizeof(join_address_));
        if (ImGui::Button("Join")) {
            net_.join(sim_, join_address_, ghost::game::net::kDefaultPort, name_edit_);
        }
    } else {
        ImGui::Text("%s", net_.role() == NetSession::Role::Host ? (net_.viaSteam() ? "Hosting on Steam" : "Hosting") : "Joined");
        if (net_.role() == NetSession::Role::Host && net_.viaSteam() && ImGui::Button("Invite friends")) {
            ghost::engine::steam::openInviteDialog();
        }
        u32 count = 0;
        for (const PlayerSlot& s : sim_.slots()) {
            count += s.active ? 1u : 0u;
        }
        ImGui::Text("Players: %u", count);
        for (const PlayerSlot& s : sim_.slots()) {
            if (s.active) {
                ImGui::BulletText("%s%s", s.name.c_str(), s.id == local_ ? " (you)" : "");
            }
        }
        if (ImGui::Button(net_.role() == NetSession::Role::Host ? "Stop hosting" : "Leave")) {
            net_.leave(sim_, "Left the game");
            local_ = sim_.local_id();
        }
    }
    if (net_.role() != NetSession::Role::Client) {
        ImGui::SeparatorText("Scene");
        const i32 current = scene_index_of_dir(sim_.zone_dir());
        for (u32 i = 0; i < scenes().size(); i++) {
            const SceneEntry& entry = scenes()[i];
            const std::string label = std::string(static_cast<i32>(i) == current ? "Restart: " : "") + std::string(entry.name);
            if (ImGui::Button(label.c_str())) {
                pending_scene_ = static_cast<i32>(i);
                menu_open_ = false;
            }
        }
    } else {
        ImGui::Text("Scene: %s", sim_.scene().name.empty() ? std::string(sim_.zone_dir()).c_str() : sim_.scene().name.c_str());
    }
    if (!net_.status().empty()) {
        ImGui::TextWrapped("%s", net_.status().c_str());
    }
    ImGui::Separator();
    if (ImGui::Button("Resume")) {
        menu_open_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Quit")) {
        window.request_close();
    }
    ImGui::End();
}

}
