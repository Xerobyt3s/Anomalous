#include "assets/watcher.h"
#include "carsys/items.h"
#include "core/arena.h"
#include "core/log.h"
#include "core/version.h"
#include "engine/debug/imgui_layer.h"
#include "engine/debug/log.h"
#include "app/game.h"
#include "engine/net/steam.h"
#include "world/scenes.h"
#include "math/vmath.h"
#include "platform/clock.h"
#include "platform/gl_loader.h"
#include "platform/window.h"
#include "render/debug_draw.h"
#include "render/device.h"
#include "render/fontchain.h"
#include "physics/body.h"
#include "world/destination.h"
#include "render/screenshot.h"
#include "render/sky.h"
#include "render/terrain_render.h"
#include "render/text.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>

#include <imgui.h>

namespace {
struct Options {
    i32 width = 1600;
    i32 height = 900;
    i64 max_frames = -1;
    const char* screenshot = nullptr;
    f32 exposure = 1.10f;
    f32 time_of_day = 0.32f;
    bool retro = true;
    bool free_cam = false;
    bool terminal = false;
    const char* term_cmd = nullptr;
    const char* term_keys = nullptr;
    i64 term_cmd_frame = 300;
    i64 term_key_stride = 30;
    bool photo = false;
    bool place = false;
    bool coil = false;
    bool drive = false;
    bool lights = false;
    bool cowboy = false;
    bool creatures = false;
    bool cockpit = false;
    i64 burnout_frame = 0;
    bool walk = false;
    bool have_carat = false;
    anom::Vec3 carat{};
    bool have_playerat = false;
    f32 pitch_deg = 0.0f;
    const char* hold = nullptr;
    i32 dummy = -1;
    bool host = false;
    bool novsync = false;
    bool gun = false;
    bool shoot = false;
    bool aim = false;
    const char* ghost = nullptr;
    const char* rounds = nullptr;
    const char* scene = nullptr;
    const char* install = nullptr;
    bool ride = false;
    bool host_steam = false;
    i32 materials = 0;
    const char* join = nullptr;
    const char* name = nullptr;
    anom::Vec3 playerat{};
    f32 playerat_yaw = 0.0f;
    const char* zone = "assets/zones/testzone";
    i64 jump_frame = -1;
    i64 steer_frame = 0;
    f32 charge = 0.0f;
    bool term_loose = false;
    i64 photo_frame = 150;
    bool panels = false;
    bool brush = false;
    f32 rain = 0.0f;
    bool snow = false;
    bool have_campos = false;
    anom::Vec3 campos{0.0f, 0.0f, 0.0f};
    bool have_camlook = false;
    f32 cam_yaw = 0.0f;
    f32 cam_pitch = 0.0f;
};

Options parse_options(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; i++) {
        if (std::strncmp(argv[i], "--frames=", 9) == 0) {
            options.max_frames = std::atoll(argv[i] + 9);
        } else if (std::strncmp(argv[i], "--width=", 8) == 0) {
            options.width = std::atoi(argv[i] + 8);
        } else if (std::strncmp(argv[i], "--height=", 9) == 0) {
            options.height = std::atoi(argv[i] + 9);
        } else if (std::strncmp(argv[i], "--screenshot=", 13) == 0) {
            options.screenshot = argv[i] + 13;
        } else if (std::strncmp(argv[i], "--exposure=", 11) == 0) {
            options.exposure = static_cast<f32>(std::atof(argv[i] + 11));
        } else if (std::strncmp(argv[i], "--hold=", 7) == 0) {
            options.hold = argv[i] + 7;
        } else if (std::strncmp(argv[i], "--dummy=", 8) == 0) {
            options.dummy = std::atoi(argv[i] + 8);
        } else if (std::strcmp(argv[i], "--novsync") == 0) {
            options.novsync = true;
        } else if (std::strcmp(argv[i], "--gun") == 0) {
            options.gun = true;
        } else if (std::strcmp(argv[i], "--shoot") == 0) {
            options.gun = true;
            options.shoot = true;
        } else if (std::strcmp(argv[i], "--aim") == 0) {
            options.gun = true;
            options.aim = true;
        } else if (std::strncmp(argv[i], "--ghost=", 8) == 0) {
            options.ghost = argv[i] + 8;
        } else if (std::strncmp(argv[i], "--materials=", 12) == 0) {
            options.materials = std::atoi(argv[i] + 12);
        } else if (std::strncmp(argv[i], "--install=", 10) == 0) {
            options.install = argv[i] + 10;
        } else if (std::strncmp(argv[i], "--scene=", 8) == 0) {
            options.scene = argv[i] + 8;
        } else if (std::strncmp(argv[i], "--rounds=", 9) == 0) {
            options.rounds = argv[i] + 9;
        } else if (std::strcmp(argv[i], "--host") == 0) {
            options.host = true;
        } else if (std::strcmp(argv[i], "--host-steam") == 0) {
            options.host_steam = true;
        } else if (std::strncmp(argv[i], "--join=", 7) == 0) {
            options.join = argv[i] + 7;
        } else if (std::strcmp(argv[i], "--join") == 0 && i + 1 < argc) {
            options.join = argv[++i];
        } else if (std::strncmp(argv[i], "--name=", 7) == 0) {
            options.name = argv[i] + 7;
        } else if (std::strncmp(argv[i], "--tod=", 6) == 0) {
            options.time_of_day = static_cast<f32>(std::atof(argv[i] + 6));
        } else if (std::strncmp(argv[i], "--rain=", 7) == 0) {
            options.rain = static_cast<f32>(std::atof(argv[i] + 7));
        } else if (std::strcmp(argv[i], "--snow") == 0) {
            options.snow = true;
        } else if (std::strcmp(argv[i], "--noretro") == 0) {
            options.retro = false;
        } else if (std::strcmp(argv[i], "--brush") == 0) {
            options.brush = true;
        } else if (std::strcmp(argv[i], "--panels") == 0) {
            options.panels = true;
        } else if (std::strcmp(argv[i], "--photo") == 0) {
            options.photo = true;
        } else if (std::strncmp(argv[i], "--charge=", 9) == 0) {
            options.charge = static_cast<f32>(std::atof(argv[i] + 9));
            options.coil = true;
        } else if (std::strncmp(argv[i], "--drive=", 8) == 0) {
            options.drive = true;
            options.steer_frame = std::atoll(argv[i] + 8);
        } else if (std::strcmp(argv[i], "--drive") == 0) {
            options.drive = true;
        } else if (std::strncmp(argv[i], "--burnout=", 10) == 0) {
            options.drive = true;
            options.burnout_frame = std::atoll(argv[i] + 10);
        } else if (std::strcmp(argv[i], "--cockpit") == 0) {
            options.drive = true;
            options.cockpit = true;
        } else if (std::strcmp(argv[i], "--creatures") == 0) {
            options.creatures = true;
        } else if (std::strcmp(argv[i], "--cowboy") == 0) {
            options.cowboy = true;
        } else if (std::strcmp(argv[i], "--lights") == 0) {
            options.lights = true;
        } else if (std::strcmp(argv[i], "--ride") == 0) {
            options.ride = true;
        } else if (std::strcmp(argv[i], "--walk") == 0) {
            options.walk = true;
        } else if (std::strcmp(argv[i], "--carat") == 0 && i + 3 < argc) {
            options.carat = anom::Vec3{static_cast<f32>(std::atof(argv[i + 1])),
                                       static_cast<f32>(std::atof(argv[i + 2])),
                                       static_cast<f32>(std::atof(argv[i + 3])) * anom::kDegToRad};
            options.have_carat = true;
            i += 3;
        } else if (std::strcmp(argv[i], "--playerat") == 0 && i + 4 < argc) {
            options.playerat = anom::Vec3{static_cast<f32>(std::atof(argv[i + 1])),
                                          static_cast<f32>(std::atof(argv[i + 2])),
                                          static_cast<f32>(std::atof(argv[i + 3]))};
            options.playerat_yaw = static_cast<f32>(std::atof(argv[i + 4])) * anom::kDegToRad;
            options.have_playerat = true;
            i += 4;
        } else if (std::strncmp(argv[i], "--pitch=", 8) == 0) {
            options.pitch_deg = static_cast<f32>(std::atof(argv[i] + 8));
        } else if (std::strncmp(argv[i], "--jump=", 7) == 0) {
            options.jump_frame = std::atoll(argv[i] + 7);
        } else if (std::strncmp(argv[i], "--zone=", 7) == 0) {
            options.zone = argv[i] + 7;
        } else if (std::strcmp(argv[i], "--coil") == 0) {
            options.coil = true;
        } else if (std::strcmp(argv[i], "--place") == 0) {
            options.place = true;
        } else if (std::strcmp(argv[i], "--termloose") == 0) {
            options.term_loose = true;
            options.terminal = true;
        } else if (std::strcmp(argv[i], "--term") == 0) {
            options.terminal = true;
        } else if (std::strncmp(argv[i], "--termkeys=", 11) == 0) {
            options.term_keys = argv[i] + 11;
        } else if (std::strncmp(argv[i], "--termcmd=", 10) == 0) {
            options.term_cmd = argv[i] + 10;
            options.terminal = true;
        } else if (std::strcmp(argv[i], "--freecam") == 0) {
            options.free_cam = true;
        } else if (std::strcmp(argv[i], "--campos") == 0 && i + 3 < argc) {
            options.campos = anom::Vec3{static_cast<f32>(std::atof(argv[i + 1])),
                                        static_cast<f32>(std::atof(argv[i + 2])),
                                        static_cast<f32>(std::atof(argv[i + 3]))};
            options.have_campos = true;
            options.free_cam = true;
            i += 3;
        } else if (std::strcmp(argv[i], "--camlook") == 0 && i + 2 < argc) {
            options.cam_yaw = static_cast<f32>(std::atof(argv[i + 1])) * anom::kDegToRad;
            options.cam_pitch = static_cast<f32>(std::atof(argv[i + 2])) * anom::kDegToRad;
            options.have_camlook = true;
            options.free_cam = true;
            i += 2;
        }
    }
    return options;
}

}

int main(int argc, char** argv)
{
    using namespace anom;

    ghost::engine::initLogging();
    const Options options = parse_options(argc, argv);

    log_info("anomalous %.*s (%.*s)",
             static_cast<int>(version_string().size()), version_string().data(),
             static_cast<int>(build_config().size()), build_config().data());

    Arena perm(gigabytes(1));
    Arena scratch(megabytes(256));
    if (!perm.valid() || !scratch.valid()) {
        return 1;
    }

    Window window;
    if (!window.create("Anomalous", options.width, options.height)) {
        return 1;
    }
    ghost::engine::ImGuiLayer imgui(window.handle());

    FileWatcher watcher;
    RenderDevice device;
    if (!device.init(watcher, scratch)) {
        return 1;
    }

    FontChain fonts;
    TextRenderer text;
    DebugDraw debug;
    if (!fonts.init(perm, scratch) || !text.init(fonts, perm, scratch) || !debug.init(perm)) {
        return 1;
    }

    Game game;
    if (!game.init(device, fonts, perm, scratch, options.zone)) {
        return 1;
    }

    TerrainRenderer terrain_renderer;
    if (!terrain_renderer.init(device, scratch, game.terrain().heightfield(),
                               game.terrain().roadmask(), game.terrain().mask_size())) {
        return 1;
    }

    Environment env;
    env.time_of_day = options.time_of_day;
    env.exposure = options.exposure;
    env.sun_dir = -sun_direction_for_time(env.time_of_day);
    game.set_environment(env);

    if (!options.retro && device.post()) {
        device.post()->settings().mode = ghost::engine::PostProcess::Mode::Native;
    }

    for (const char* name : {"lit", "glass", "rain", "snow", "debug2d", "blit", "screen"}) {
        device.shaders().program(name);
    }

    if (options.install) {
        std::string_view list = options.install;
        while (!list.empty()) {
            const size_t comma = list.find(',');
            const std::string_view name = list.substr(0, comma);
            for (u32 k = 0; k < anom::PART_COUNT; k++) {
                const anom::PartKind kind = static_cast<anom::PartKind>(k);
                if (anom::item_id(anom::item_for_part(kind)) == name) {
                    game.carsys().parts[k].installed = true;
                }
            }
            list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
        }
    }
    for (u32 k = 0; k < anom::kSynthMaterials && options.materials > 0; k++) {
        if (k < game.sim().gameplay().ammo().materials.size()) {
            game.carsys().synth.tank[k] = static_cast<u16>(options.materials);
        }
    }
    if (options.coil) {
        game.carsys().parts[PART_COIL].installed = true;
        game.set_travel_charge(options.charge);
    }
    if (options.photo || options.place) {
        game.interact().hands().kind = anom::ITEM_CAMERA;
    }
    if (options.place) {
        game.player().look(900.0f, 260.0f);
    }
    if (options.photo) {
        game.carsys().coax_target = anom::kCoaxTargetCamera;
    }
    if (options.have_carat) {
        const f32 ground = game.terrain().heightfield().sample(options.carat.x, options.carat.y);
        game.vehicle().teleport(game.phys(), anom::Vec3{options.carat.x, ground + 1.0f, options.carat.y},
                                options.carat.z);
    }
    if (options.hold) {
        game.interact().hands().kind = anom::item_from_id(options.hold);
    }
    if (options.have_playerat) {
        game.player().init(options.playerat, options.playerat_yaw);
    }
    if (options.pitch_deg != 0.0f) {
        game.player().look(0.0f, -options.pitch_deg * anom::kDegToRad / anom::kLookSensitivity);
    }
    game.toggles().free_cam = options.free_cam;
    if (options.dummy >= 0) {
        game.pending().dummy_script = options.dummy;
    }
    if (options.novsync) {
        window.set_vsync(false);
    }
    ghost::engine::steam::init();
    if (options.name) {
        game.net().setName(options.name);
    } else if (ghost::engine::steam::available()) {
        game.net().setName(ghost::engine::steam::personaName());
    }
    if (anom::PlayerSlot* mine = game.sim().slot(game.local_id())) {
        mine->name.assign(game.net().name());
    }
    if (options.host_steam) {
        game.net().hostSteam(game.sim());
    } else if (options.host) {
        game.net().host(game.sim(), ghost::game::net::kDefaultPort);
    } else if (options.join) {
        game.net().join(game.sim(), options.join, ghost::game::net::kDefaultPort, game.net().name());
    }
    game.toggles().chase_cam = options.drive && !options.cockpit;
    if (options.drive) {
        const anom::RigidBody* car = game.phys().body(game.vehicle().body());
        if (car) {
            const anom::Vec3 out = rotate(car->rot,
                                          anom::Vec3{-(car->half_extents.x + 0.8f), 0.0f, 0.0f});
            const anom::Vec3 door = car->pos + out;
            game.player().init(anom::Vec3{door.x, car->pos.y - car->half_extents.y, door.z},
                               std::atan2(-out.x, out.z));
            game.player().look(0.0f, 265.0f);
        }
    }
    if (options.ride) {
        const anom::RigidBody* car = game.phys().body(game.vehicle().body());
        game.carsys().parts[PART_COMPUTER].installed = false;
        if (car) {
            const anom::Vec3 out = rotate(car->rot, anom::Vec3{car->half_extents.x + 0.8f, 0.0f, 0.0f});
            const anom::Vec3 door = car->pos + out;
            game.player().init(anom::Vec3{door.x, car->pos.y - car->half_extents.y, door.z}, std::atan2(-out.x, out.z));
            game.player().look(0.0f, 265.0f);
        }
    }
    if (options.terminal) {
        anom::CarSys& sys = game.carsys();
        sys.parts[PART_COMPUTER].installed = !options.term_loose;
        sys.parts[PART_ANTENNA].installed = true;
        sys.parts[PART_ANTENNA].variant = 1;
        sys.computer_on = true;
        for (anom::Cable& cable : sys.cables) {
            cable.state = anom::CableState::Plugged;
            cable.linked = true;
        }
    }
    if (options.brush) {
        game.editor().toggle(game.world(), game.phys());
        game.editor().set_brush_mode(true);
        game.editor().brush().reset(scratch);
    }
    game.toggles().carsys = options.panels;
    game.toggles().telemetry = options.panels;
    game.toggles().debug_panels = options.panels;
    game.toggles().creatures = options.creatures;
    if (options.have_campos) {
        game.camera().pos = options.campos;
    }
    if (options.have_camlook) {
        game.camera().yaw = options.cam_yaw;
        game.camera().pitch = options.cam_pitch;
    }
    if (options.rain > 0.0f) {
        game.weather().set_mode(WeatherMode::Rain);
        for (i32 i = 0; i < 1800; i++) {
            game.weather().tick(0.1f);
        }
    }
    if (options.snow) {
        game.weather().set_mode(WeatherMode::Snow);
        for (i32 i = 0; i < 1800; i++) {
            game.weather().tick(0.1f);
        }
    }

    const f64 start_time = time_seconds();
    f64 prev_time = start_time;
    f64 fps_timer = 0.0;
    u32 fps_frames = 0;
    i64 frame_index = 0;

    anom::PlayerId gun_owner = 0;
    bool gun_seated = false;
    while (!window.should_close()) {
        scratch.reset();

        const f64 now = time_seconds();
        f64 frame_dt = now - prev_time;
        prev_time = now;
        if (frame_dt > kMaxFrameDt) {
            frame_dt = kMaxFrameDt;
        }

        window.poll();
        ghost::engine::steam::runCallbacks();
        if (options.place) {
            window.input().set_key(static_cast<int>(anom::Key::F), true);
        }
        if (options.drive) {
            if (!game.player().driving()) {
                game.player().look(11.0f, 0.0f);
                if (frame_index % 3 == 0) {
                    window.input().set_key(static_cast<int>(anom::Key::E), true);
                }
            } else if (!game.carsys().engine_on) {
                game.carsys().key_inserted = true;
            } else {
                game.carsys().handbrake_latched = static_cast<i64>(frame_index) < options.burnout_frame;
                window.input().set_key(static_cast<int>(anom::Key::W), true);
            }
            if (options.steer_frame && frame_index >= options.steer_frame
                && game.player().driving()) {
                window.input().set_key(static_cast<int>(anom::Key::D), true);
            }
        }
        if (options.ride && !game.player().driving()) {
            const anom::InteractAction act = game.interact().action();
            if (frame_index % 3 == 0 && (act == anom::InteractAction::OpenDoor || act == anom::InteractAction::EnterCar)) {
                window.input().set_key(static_cast<int>(anom::Key::E), true);
            }
        }
        if (options.walk && !game.player().driving()) {
            window.input().set_key(static_cast<int>(anom::Key::W), true);
        }
        if (options.photo) {
            window.input().set_mouse_button(1, true);
            if (frame_index == options.photo_frame) {
                window.input().set_mouse_button(0, true);
            }
        }
        const Input& input = window.input();

        watcher.poll(now);
        device.shaders().reload_if_changed(now);
        game.poll_hot_reload(scratch, now);
        if (options.term_cmd && frame_index == options.term_cmd_frame) {
            game.focus_terminal(true);
            for (const char* p = options.term_cmd; *p; p++) {
                game.terminal().key_char(*p);
            }
            game.terminal().key(anom::TermKey::Enter);
        }
        if (options.term_keys) {
            const i64 step = frame_index - options.term_cmd_frame - options.term_key_stride;
            if (step >= 0 && step % options.term_key_stride == 0) {
                const i64 index = step / options.term_key_stride;
                if (index < static_cast<i64>(std::strlen(options.term_keys))) {
                    switch (options.term_keys[index]) {
                    case 'U': game.terminal().key(anom::TermKey::Up); break;
                    case 'D': game.terminal().key(anom::TermKey::Down); break;
                    case 'E': game.terminal().key(anom::TermKey::Enter); break;
                    case 'Q': game.terminal().key(anom::TermKey::Quit); break;
                    default: break;
                    }
                }
            }
        }
        game.handle_input(window, input, static_cast<f32>(frame_dt));
        if (options.gun && options.ride && game.player().driving() && !gun_seated) {
            game.pending().holster = true;
            gun_seated = true;
        }
        if (options.gun && !options.ride && (frame_index == 2 || game.local_id() != gun_owner)) {
            game.pending().holster = frame_index >= 2;
            gun_owner = frame_index >= 2 ? game.local_id() : gun_owner;
        }
        if (options.aim && frame_index > 2) {
            game.pending().aim = true;
        }
        if (options.shoot && frame_index > 60) {
            game.pending().trigger = (frame_index % 50) < 25;
        }
        if (options.scene && frame_index == 1) {
            game.pending().scene = anom::scene_index(options.scene);
        }
        if (options.rounds && frame_index == 1) {
            if (anom::PlayerSlot* me = game.sim().slot(game.local_id())) {
                const ghost::game::ElementId element = game.sim().gameplay().ammo().element(options.rounds);
                for (int k = 0; k < ghost::game::kChamberCount; k++) {
                    me->gun.mechanism.setChamber(k, {ghost::game::ChamberState::Live, ghost::game::Round{element}});
                }
            }
        }
        if (options.ghost && frame_index == 1) {
            const auto& types = game.sim().gameplay().ghostData().types;
            for (size_t t = 0; t < types.size(); t++) {
                if (types[t].name == options.ghost) {
                    game.pending().spawn_ghost = static_cast<i32>(t);
                }
            }
        }
        if (options.drive && game.player().driving() && !game.carsys().engine_on) {
            game.pending().crank = true;
        }
        if (options.lights) {
            game.carsys().headlight_switch = true;
        }
        if (options.cowboy) {
            for (anom::PlayerId id = 0; id < anom::kMaxPlayers; id++) {
                if (anom::PlayerSlot* s = game.sim().slot(id)) {
                    s->cowboy = 1;
                }
            }
        }
        if (options.jump_frame >= 0 && frame_index == options.jump_frame) {
            game.request_travel(anom::destination_index("touge"));
        }
        game.advance(static_cast<f32>(frame_dt));

        const FramebufferSize size = window.framebuffer_size();
        const Vec2 viewport{static_cast<f32>(size.width), static_cast<f32>(size.height)};
        game.render(device, terrain_renderer, debug, text, input, viewport,
                    static_cast<f32>(now), static_cast<f32>(frame_dt));

        imgui.beginFrame();
        game.draw_imgui(window);
        if (game.toggles().debug_panels) {
            ImGui::Begin("Stats");
            ImGui::Text("%.2f ms/frame", frame_dt * 1000.0);
            ImGui::Text("%u draws", device.stats().draw_calls);
            ImGui::Text("%u/%u terrain chunks", terrain_renderer.chunks_drawn(), terrain_renderer.chunk_count());
            ImGui::End();
            if (device.post()) {
                device.post()->debugUi();
            }
        }
        imgui.endFrame();

        frame_index++;
        const bool last_frame = options.max_frames >= 0 && frame_index >= options.max_frames;
        if (last_frame && options.screenshot) {
            FrameStats stats;
            if (capture_frame(scratch, size.width, size.height, options.screenshot, &stats)) {
                log_info("frame: mean rgb %.1f %.1f %.1f, nonblack %.1f%%, %u colour buckets",
                         stats.mean_r, stats.mean_g, stats.mean_b,
                         static_cast<f64>(stats.nonblack_fraction) * 100.0,
                         stats.distinct_buckets);
            }
        }

        window.swap_buffers();

        fps_frames++;
        fps_timer += frame_dt;
        if (fps_timer >= 0.5) {
            char title[160];
            std::snprintf(title, sizeof(title),
                          "Anomalous | %.0f fps | %u/%u chunks | %u draws",
                          static_cast<f64>(fps_frames) / fps_timer,
                          terrain_renderer.chunks_drawn(), terrain_renderer.chunk_count(),
                          device.stats().draw_calls);
            window.set_title(title);
            fps_timer = 0.0;
            fps_frames = 0;
        }

        if (last_frame) {
            window.request_close();
        }
    }

    game.audio().shutdown();
    ghost::engine::steam::shutdown();
    terrain_renderer.shutdown();
    device.shutdown();

    const f64 elapsed = time_seconds() - start_time;
    log_info("shutdown after %lld frames, %.2f ms/frame avg",
             static_cast<long long>(frame_index),
             frame_index > 0 ? elapsed * 1000.0 / static_cast<f64>(frame_index) : 0.0);
    return 0;
}
