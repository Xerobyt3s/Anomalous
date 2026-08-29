#pragma once

#include "audio/audio.h"
#include "audio/tapes.h"
#include "carsys/carsys.h"
#include "carsys/carsys_render.h"
#include "core/types.h"
#include "editor/editor.h"
#include "math/vmath.h"
#include "physics/world.h"
#include "player/interact.h"
#include "platform/input_context.h"
#include "player/player.h"
#include "render/camera.h"
#include "ui/ui.h"
#include "vehicle/vehicle.h"
#include "world/entity.h"
#include "world/terrain.h"
#include "world/weather.h"
#include "world/zone.h"

#include <string_view>

namespace anom {

class Arena;
class DebugDraw;
class Input;
class RenderDevice;
class TerrainRenderer;
class TextRenderer;
class Window;

inline constexpr f32 kFixedDt = 1.0f / 120.0f;
inline constexpr f64 kMaxFrameDt = 0.25;
inline constexpr u32 kTelemetrySamples = 128;

struct Telemetry {
    f32 samples[kTelemetrySamples] = {};
    u32 head = 0;

    void push(f32 value)
    {
        samples[head] = value;
        head = (head + 1) % kTelemetrySamples;
    }
};

struct GameToggles {
    bool telemetry = false;
    bool phys_debug = false;
    bool collision = false;
    bool carsys = false;
    bool tuning = false;
    bool slow_mo = false;
    bool free_cam = false;
    bool chase_cam = false;
};

class Game {
public:
    bool init(RenderDevice& device, Arena& perm, Arena& scratch, std::string_view zone_dir);

    void handle_input(Window& window, const Input& input, f32 frame_dt);
    void advance(f32 frame_dt);
    void render(RenderDevice& device, TerrainRenderer& terrain_renderer, DebugDraw& debug,
                TextRenderer& text, const Input& input, Vec2 viewport, f32 time, f32 frame_dt);
    void poll_hot_reload(Arena& scratch, f64 now);

    void tick(f32 dt, const PlayerCommand& cmd);
    void reset_car();
    void reset_player();

    Camera& camera() { return camera_; }
    const Camera& camera() const { return camera_; }
    Player& player() { return player_; }
    Vehicle& vehicle() { return vehicle_; }
    CarSys& carsys() { return carsys_; }
    Interact& interact() { return interact_; }
    World& world() { return world_; }
    PhysWorld& phys() { return phys_; }
    Terrain& terrain() { return terrain_; }
    Weather& weather() { return weather_; }
    Editor& editor() { return editor_; }
    Audio& audio() { return audio_; }
    Ui& ui() { return ui_; }
    GameToggles& toggles() { return toggles_; }
    const ZoneSpawn& spawn() const { return spawn_; }
    u64 tick_count() const { return tick_count_; }
    f32 interpolation() const { return alpha_; }

private:
    void build_input_context(const Input& input);
    void drive_input(const Input& input, PlayerCommand& cmd, f32 frame_dt);
    void foot_input(const Input& input, PlayerCommand& cmd, f32 frame_dt);
    void sync_pickup_transforms();
    void apply_weather_grip();
    void update_camera(f32 frame_dt);
    void guard_against_falling();
    void update_audio(f32 frame_dt);
    void draw_hud(DebugDraw& debug, TextRenderer& text, Vec2 viewport, f32 frame_dt);
    void draw_debug_overlays(DebugDraw& debug);
    void bind_entity_meshes(RenderDevice& device);
    void draw_vehicle(RenderDevice& device);

    Terrain terrain_;
    World world_;
    PhysWorld phys_;
    Vehicle vehicle_;
    Player player_;
    CarSys carsys_;
    Interact interact_;
    InteractBoxes boxes_;
    Weather weather_;
    TapeLibrary tapes_;
    Audio audio_;
    CarSysRenderer car_render_;
    Editor editor_;
    Ui ui_;
    InputContext context_;
    Camera camera_;

    ZoneSpawn spawn_;
    ZonePickups pickups_;
    FixedString<128> zone_dir_;

    GameToggles toggles_;
    Telemetry telem_rpm_;
    Telemetry telem_slip_;
    Telemetry telem_speed_;
    Telemetry telem_load_;

    Vec3 tower_pos_;
    Quat tower_rot_ = quat_identity();

    f32 throw_charge_ = 0.0f;
    bool place_active_ = false;
    bool place_valid_ = false;
    Vec3 place_pos_;
    f32 place_yaw_ = 0.0f;

    PlayerCommand pending_cmd_;
    bool pending_jump_ = false;
    bool pending_interact_ = false;
    f64 accumulator_ = 0.0;
    f32 alpha_ = 0.0f;
    u64 tick_count_ = 0;
    f64 next_cfg_poll_ = 0.0;
    Arena* perm_ = nullptr;
    Arena* scratch_ = nullptr;
};

} // namespace anom
