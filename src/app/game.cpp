#include "app/game.h"
#include "math/glm_bridge.h"
#include "assets/mesh_data.h"
#include "carsys/items.h"
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
#include "world/pickup_body.h"

#include <imgui.h>

#include <exception>

namespace anom {
namespace {
constexpr f32 kThrowChargeMax = 0.9f;
constexpr f32 kPlaceRange = 3.5f;
constexpr f32 kPlaceNormalY = 0.55f;
constexpr f32 kChromaBase = 1.5f;
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
constexpr f32 kEngineAudioLocal[3] = {0.0f, 0.10f, -1.55f};
constexpr f32 kDashAudioLocal[3] = {0.0f, 0.35f, -0.35f};
constexpr Vec3 kCarPressLocal{0.0f, 0.60f, 0.0f};
constexpr Vec3 kCarPressHalf{1.02f, 1.00f, 2.20f};
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
    audio_.init(perm);
    if (!term_render_.init(fonts, scratch)) {
        log_error("game: terminal renderer init failed");
        return false;
    }
    if (!tree_render_.init(scratch)) {
        return false;
    }
    try {
        lightning_.emplace();
    } catch (const std::exception& e) {
        log_error("game: lightning unavailable: %s", e.what());
    }

    editor_.init(perm);
    editor_.set_meshes(&meshes_);
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
    pending_.throw_power = -1.0f;
    pending_.place_commit = false;
    pending_.stow_cable = false;
    pending_.terminal_leave = false;
    pending_.terminal_keys = 0;
    pending_.terminal_char_count = 0;
    pending_.dummy_cycle = false;
    pending_.dummy_script = -1;
    pending_.holster = false;
}

void Game::drive_input(const Input& input)
{
    pending_.throttle = input.down(Key::W) ? 1.0f : 0.0f;
    pending_.reverse = input.down(Key::S) ? 1.0f : 0.0f;
    pending_.steer = (input.down(Key::D) ? 1.0f : 0.0f) - (input.down(Key::A) ? 1.0f : 0.0f);
    pending_.handbrake = input.down(Key::Space);
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
    pending_.move_x = (input.down(Key::D) ? 1.0f : 0.0f) - (input.down(Key::A) ? 1.0f : 0.0f);
    pending_.move_z = (input.down(Key::W) ? 1.0f : 0.0f) - (input.down(Key::S) ? 1.0f : 0.0f);
    pending_.run = input.down(Key::LeftShift);
    pending_.crouch = input.down(Key::LeftControl);
    if (input.pressed(Key::Space)) {
        pending_.jump = true;
    }
    if (input.pressed(Key::X)) {
        pending_.crawl = true;
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
    pending_.view_origin = camera_.pos;
    pending_.view_dir = camera_.forward();

    if (terminal_focused()) {
        window.set_cursor_captured(false);
        terminal_input(input);
        return;
    }

    if (global) {
        if (input.pressed(Key::Escape) && !editor_.active()) {
            menu_open_ = !menu_open_;
        }
        if (input.pressed(Key::R) && !editor_.active()) {
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
        if (input.pressed(Key::F10) && !editor_.active()) {
            pending_.dummy_cycle = true;
        }
        if (input.pressed(Key::F8)) {
            editor_.toggle(sim_.world(), sim_.phys());
            if (editor_.active()) {
                sim_.reset_car();
                reset_player();
            }
            build_input_context(input);
        }
        if (input.pressed(Key::C) && !editor_.active()) {
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
            pending_.look_dx += input.mouse_delta().x;
            pending_.look_dy += input.mouse_delta().y;
        }
    }

    pending_.gameplay = true;
    if (sim_.player(local_).driving()) {
        drive_input(input);
    } else {
        foot_input(input, frame_dt);
    }
    pending_.use_down = input.down(Key::E);
    if (input.pressed(Key::E)) {
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
    view_.camera(sim_.player(local_), sim_.phys(), &sim_.vehicle(), alpha_, frame_dt, toggles_.chase_cam, pending_.look_dx,
                 pending_.look_dy, camera_);
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
    }
    alpha_ = static_cast<f32>(clock_.alpha());
    bodies_.update(sim_, local_, alpha_, frame_dt);
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

void Game::consume_events()
{
    using namespace ghost::game;
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
            audio_.play_at(SFX_IMPACT, f_clamp(impact->strength, 0.2f, 1.0f), 1.0f, from_glm(impact->position));
        } else if (const auto* installed = std::get_if<PartInstalled>(&event)) {
            audio_.play_at(SFX_RATCHET, 0.5f, 1.0f, from_glm(installed->position));
        } else if (const auto* removed = std::get_if<PartRemoved>(&event)) {
            audio_.play_at(SFX_RATCHET, 0.5f, 0.8f, from_glm(removed->position));
        } else if (std::holds_alternative<ZoneLoaded>(event)) {
            editor_.snapshot_world(sim_.world(), sim_.phys());
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
    const bool cursor_visible = editor_.active() || toggles_.free_cam || toggles_.tuning;

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

    const Vec3 engine_pos = body_point(*body, Vec3{kEngineAudioLocal[0], kEngineAudioLocal[1],
                                                   kEngineAudioLocal[2]});
    const Vec3 dash_pos = body_point(*body, Vec3{kDashAudioLocal[0], kDashAudioLocal[1],
                                                 kDashAudioLocal[2]});
    audio_.set_listener(camera_.pos, camera_.forward(), camera_.up(), sim_.player(local_).vel());
    audio_.set_car(engine_pos, body->pos, dash_pos, body->vel);
    audio_.set_occlusion(sim_.player(local_).driving() ? 0.55f : 1.0f, frame_dt);

    audio_.set_engine(drivetrain_rpm(sim_.vehicle().train()),
                      f_clamp01(sim_.vehicle().input().throttle), sim_.carsys().engine_on,
                      sim_.carsys().crank_active, frame_dt);

    const f32 speed = f_abs(sim_.vehicle().forward_speed(sim_.phys()));
    u32 grounded = 0;
    f32 skid = 0.0f;
    for (u32 i = 0; i < kWheelCount; i++) {
        const Wheel& w = sim_.vehicle().wheel(i);
        if (w.grounded) {
            grounded++;
        }
        skid = f_max(skid, f_clamp01((f_abs(w.slide_lat) - 1.5f) / 6.0f));
    }
    const f32 road = sim_.terrain().road_amount(body->pos.x, body->pos.z);
    audio_.set_rolling(speed, road, grounded > 0, sim_.weather().wetness(), frame_dt);
    audio_.set_skid(skid, frame_dt);
    audio_.set_horn(false, frame_dt);
    audio_.set_rain(sim_.weather().rain() * (sim_.player(local_).driving() ? 0.35f : 1.0f),
                    sim_.player(local_).driving() ? sim_.weather().rain() : 0.0f, frame_dt);
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

void Game::draw_hud(DebugDraw& debug, TextRenderer& text, Vec2 viewport, f32 frame_dt)
{
    const f32 cx = viewport.x * 0.5f;
    const f32 cy = viewport.y * 0.5f;

    if (!editor_.active() && !toggles_.free_cam && !viewfinder_) {
        debug.line_2d(cx - 6.0f, cy, cx + 6.0f, cy, kDdWhite);
        debug.line_2d(cx, cy - 6.0f, cx, cy + 6.0f, kDdWhite);
    }

    if (sim_.player(local_).driving()) {
        const f32 speed = f_abs(sim_.vehicle().forward_speed(sim_.phys())) * 3.6f;
        const i32 gear = sim_.vehicle().train().gear;
        debug.text_2d(text, 24.0f, viewport.y - 96.0f, 26.0f, kDdWhite, "%3.0f km/h",
                      static_cast<f64>(speed));
        debug.text_2d(text, 24.0f, viewport.y - 64.0f, 20.0f, kDdWhite, "%4.0f rpm  gear %s%s",
                      static_cast<f64>(drivetrain_rpm(sim_.vehicle().train())),
                      gear < 0 ? "R" : (gear == 0 ? "N" : "D"),
                      sim_.vehicle().train().manual ? " [M]" : "");
    }

    const std::string_view prompt = sim_.interact(local_).prompt();
    if (!prompt.empty()) {
        const f32 width = text.measure(prompt, 20.0f);
        debug.text_2d(text, cx - width * 0.5f, cy + 60.0f, 20.0f, kDdWhite, "%.*s",
                      static_cast<int>(prompt.size()), prompt.data());
        if (sim_.interact(local_).action_is_hold() && sim_.interact(local_).hold_progress() > 0.0f) {
            const f32 w = 160.0f;
            debug.rect_2d(cx - w * 0.5f, cy + 74.0f, cx + w * 0.5f, cy + 82.0f, kDdGray);
            debug.rect_2d_filled(cx - w * 0.5f, cy + 74.0f,
                                 cx - w * 0.5f + w * sim_.interact(local_).hold_progress(), cy + 82.0f,
                                 kDdYellow);
        }
    }

    if (sim_.interact(local_).hands().kind != ITEM_NONE) {
        const std::string_view name = item_name(sim_.interact(local_).hands().kind);
        debug.text_2d(text, 24.0f, 28.0f, 18.0f, kDdYellow, "holding %.*s",
                      static_cast<int>(name.size()), name.data());
        if (throw_charge_ > 0.0f) {
            debug.text_2d(text, 24.0f, 50.0f, 16.0f, kDdOrange, "throw %.0f%%",
                          static_cast<f64>(throw_charge_ / kThrowChargeMax * 100.0f));
        }
    }

    if (place_active_ && sim_.interact(local_).hands().kind != ITEM_NONE) {
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
        ui_.label("engine %s", sim_.carsys().engine_on ? "running" : "off");
        ui_.label("fuel %.1f L", static_cast<f64>(sim_.carsys().fluids.fuel));
        ui_.label("oil %.2f", static_cast<f64>(sim_.carsys().fluids.oil));
        ui_.label("coolant %.0f C", static_cast<f64>(sim_.carsys().fluids.coolant_temp));
        ui_.label("battery %.2f", static_cast<f64>(sim_.carsys().elec.battery_charge));
        ui_.label("weather %s %.2f", weather_mode_name(sim_.weather().mode()),
                  static_cast<f64>(sim_.weather().rain()));
        ui_.panel_end();
    }

    if (toggles_.tuning) {
        ui_.panel_begin("tuning", 16.0f, 16.0f, 260.0f);
        VehicleConfig& cfg = sim_.vehicle().config();
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
    meshes_.bind(device.assets(), sim_.world());
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
        const Vec3 centre = body->pos + rotate(body->rot, kCarPressLocal - com);
        terrain_renderer.add_press_volume(centre, kCarPressHalf, body->rot);
        terrain_renderer.add_patch_press(centre, kCarPressHalf, body->rot);
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
        const Wheel& w = sim_.vehicle().wheel(i);
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
    if (held.kind == ITEM_NONE || editor_.active() || toggles_.free_cam || place_active_
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
            const Vec3 local = Vec3{f_lerp(1.06f, 0.96f, pour), f_lerp(0.38f, 0.30f, pour),
                                    1.30f}
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

    const Quat rot = camera_.frame * quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, -camera_.yaw);
    const Vec3 pos = hands_item_pos() - rotate(rot, item_mesh_center(held.kind)) * scale;
    const Mat4 base = mat4_trs(pos, rot, Vec3{scale, scale, scale});
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
                      sim_.disks().camera_exposures_left(&sim_.terminal().fs()));
        debug.text_2d(text, x1 - 170.0f, y1 - 26.0f, 17.0f, kDdGray, "LMB SHUTTER");
    }

    if (capture_msg_until_ > 0.0f && sim_.player(local_).state() == PlayerState::OnFoot) {
        debug.text_2d(text, viewport.x * 0.5f - 80.0f, viewport.y * 0.80f, 18.0f, kDdCyan,
                      "%s", capture_msg_.c_str());
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

    if (device.shadow_begin(camera_.pos)) {
        terrain_renderer.draw(device);
        draw_entities(device);
        draw_debris(device, time);
        tree_render_.draw(device);
        draw_vehicle(device);
        car_render_.draw(device, sim_.carsys(), sim_.vehicle(), sim_.phys(), alpha_, 0.0f, 0);
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
    bodies_.draw(device.view_proj(), camera_.pos, -env_.sun_dir, time);

    device.set_fall_rotation(quat_from_to(Vec3{0.0f, -1.0f, 0.0f}, sim_.phys().up_at(camera_.pos) * -1.0f,
                                          Vec3{1.0f, 0.0f, 0.0f}));
    device.draw_rain(sim_.weather().rain(), sim_.weather().wind(), cam_vel_, time);
    device.draw_snow(sim_.weather().snow(), sim_.weather().wind(), time);
    car_render_.draw_glass(device, sim_.carsys(), sim_.vehicle(), sim_.phys(), alpha_, time);

    draw_coil_arcs(device, debug);
    draw_debug_overlays(debug);
    bodies_.draw_names(debug, sim_);
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

    draw_hud(debug, text, viewport, frame_dt);
    draw_viewfinder(debug, text, viewport);
    if (terminal_focused()) {
        debug.text_2d(text, viewport.x * 0.5f - 80.0f, viewport.y - 10.0f, 14.0f, kDdGray,
                      "ESC to look away");
    }
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
        ImGui::SeparatorText("Direct connection");
        if (ImGui::Button("Host a game")) {
            net_.host(sim_, ghost::game::net::kDefaultPort);
        }
        ImGui::InputText("Host address", join_address_, sizeof(join_address_));
        if (ImGui::Button("Join")) {
            net_.join(sim_, join_address_, ghost::game::net::kDefaultPort, name_edit_);
        }
    } else {
        ImGui::Text("%s", net_.role() == NetSession::Role::Host ? "Hosting" : "Joined");
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
