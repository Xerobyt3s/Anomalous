#pragma once

#include "app/player_bodies.h"
#include "app/play_view.h"
#include "app/player_view.h"
#include "audio/audio.h"
#include "carsys/carsys_render.h"
#include "core/arena.h"
#include "core/types.h"
#include "editor/editor.h"
#include "engine/core/game_loop.h"
#include "game/fx/lightning.h"
#include "math/vmath.h"
#include "net/session.h"
#include "platform/input_context.h"
#include "render/camera.h"
#include "render/device.h"
#include "render/entity_meshes.h"
#include "render/tree.h"
#include "sim/sim.h"
#include "terminal/term_render.h"
#include "ui/ui.h"

#include <optional>
#include <string_view>
#include <vector>

namespace anom {
class Arena;
class DebugDraw;
class FontChain;
class Input;
class RenderDevice;
class TerrainRenderer;
class TextRenderer;
class Window;

inline constexpr u32 kTelemetrySamples = 128;
inline constexpr f64 kMaxFrameDt = 0.25;

inline constexpr f32 kThrowChargeMax = 0.9f;

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
    bool debug_panels = false;
    bool slow_mo = false;
    bool free_cam = false;
    bool chase_cam = false;
};

class Game {
public:
    bool init(RenderDevice& device, FontChain& fonts, Arena& perm, Arena& scratch, std::string_view zone_dir);

    void handle_input(Window& window, const Input& input, f32 frame_dt);
    void advance(f32 frame_dt);
    void render(RenderDevice& device, TerrainRenderer& terrain_renderer, DebugDraw& debug, TextRenderer& text,
                const Input& input, Vec2 viewport, f32 time, f32 frame_dt);
    void poll_hot_reload(Arena& scratch, f64 now);
    void draw_imgui(Window& window);
    NetSession& net() { return net_; }
    bool menu_open() const { return menu_open_; }

    void reset_car() { sim_.reset_car(); }
    void reset_player();

    Sim& sim() { return sim_; }
    PlayerCommand& pending() { return pending_; }
    Camera& camera() { return camera_; }
    const Camera& camera() const { return camera_; }
    Player& player() { return sim_.player(local_); }
    PlayerId local_id() const { return local_; }
    Vehicle& vehicle() { return sim_.vehicle(); }
    CarSys& carsys() { return sim_.carsys(); }
    Interact& interact() { return sim_.interact(local_); }
    World& world() { return sim_.world(); }
    PhysWorld& phys() { return sim_.phys(); }
    Terrain& terrain() { return sim_.terrain(); }
    Weather& weather() { return sim_.weather(); }
    Editor& editor() { return editor_; }
    Audio& audio() { return audio_; }
    Ui& ui() { return ui_; }
    Terminal& terminal() { return sim_.terminal(); }
    void set_environment(const Environment& env);
    bool terminal_focused() const { return sim_.terminal_user() == local_; }
    bool third_person() const { return toggles_.free_cam || (toggles_.chase_cam && sim_.player(local_).driving()); }
    void focus_terminal(bool on) { sim_.set_terminal_user(on ? local_ : kNoPlayer); }
    void set_travel_charge(f32 charge) { sim_.set_travel_charge(charge); }
    void request_travel(i32 destination) { sim_.arm_travel(destination); }
    GameToggles& toggles() { return toggles_; }
    const ZoneSpawn& spawn() const { return sim_.spawn(); }
    u64 tick_count() const { return sim_.tick_count(); }
    f32 interpolation() const { return alpha_; }

private:
    void build_input_context(const Input& input);
    PlayFrame play_frame() const;
    bool gun_out() const;
    f32 look_zoom() const;
    void drive_input(const Input& input);
    void foot_input(const Input& input, f32 frame_dt);
    void terminal_input(const Input& input);
    void clear_pending_edges();
    void consume_events();
    void update_camera(f32 frame_dt);
    void track_camera_velocity(f32 frame_dt);
    void update_chroma(f32 frame_dt);
    void update_audio(f32 frame_dt);
    void update_screen_fx(f32 frame_dt);
    void report_island_rejections();
    void travel_warp(f32& warp, f32& flash) const;
    void update_camera_item(const Input& input, f32 frame_dt);
    void capture_exposure(RenderDevice& device);
    bool build_video_camera(Camera& out) const;
    void render_video_feed(RenderDevice& device, TerrainRenderer& terrain_renderer, f32 frame_dt, f32 time);
    void draw_viewfinder();
    void draw_place_preview(RenderDevice& device, DebugDraw& debug);
    void draw_loose_terminal(RenderDevice& device, u32 screen_texture);
    u32 terminal_screen_texture() const;
    Vec3 hands_item_pos() const;
    void draw_play_hud();
    void draw_debug_panels();
    void draw_coil_arcs(RenderDevice& device, DebugDraw& debug);
    void draw_debug_overlays(DebugDraw& debug);
    void bind_entity_meshes(RenderDevice& device);
    void draw_entities(RenderDevice& device);
    void collect_trees();
    void update_grass_press(TerrainRenderer& terrain_renderer);
    void sync_island_gpu(RenderDevice& device, TerrainRenderer& terrain_renderer);
    void draw_debris(RenderDevice& device, f32 time);
    void draw_grass_patches(RenderDevice& device, TerrainRenderer& terrain_renderer, f32 time);
    void draw_vehicle(RenderDevice& device);
    void draw_viewmodel(RenderDevice& device);

    Sim sim_;
    CarSysRenderer car_render_;
    TreeRenderer tree_render_;
    std::optional<ghost::game::Lightning> lightning_;
    TermRenderer term_render_;
    Editor editor_;
    Ui ui_;
    InputContext context_;
    Camera camera_;
    PlayerView view_;
    PlayerBodies bodies_;
    std::optional<PlayView> play_;
    std::vector<OtherGun> other_guns_;
    std::vector<HeldItem> held_items_;
    std::vector<PropView> props_;
    std::vector<TintedCylinder> tank_fill_;
    std::vector<SeeThrough> see_through_;
    Mat4 hud_view_proj_ = mat4_identity();
    Audio audio_;
    bool sprint_on_ = false;
    bool crouch_on_ = false;
    EntityMeshes meshes_;

    GameToggles toggles_;
    Telemetry telem_rpm_;
    Telemetry telem_slip_;
    Telemetry telem_speed_;
    Telemetry telem_load_;

    ghost::engine::FixedStepClock clock_;
    PlayerCommand pending_;
    PlayerId local_ = 0;
    NetSession net_;
    std::vector<SlotCommand> slot_cmds_;
    bool menu_open_ = false;
    i32 pending_scene_ = -1;
    char join_address_[64] = "127.0.0.1";
    char name_edit_[24] = "Player";
    f32 alpha_ = 0.0f;
    f64 next_cfg_poll_ = 0.0;
    Arena* perm_ = nullptr;
    Arena* scratch_ = nullptr;

    u32 arc_rng_ = 0x5EED1234u;
    Vec3 cam_vel_;
    Vec3 prev_cam_pos_;
    bool prev_cam_valid_ = false;
    Environment env_;
    ScreenFx screen_fx_;
    f32 term_anim_ = 0.0f;
    f32 chroma_ = 0.0f;

    f32 throw_charge_ = 0.0f;
    bool viewfinder_ = false;
    bool capture_pending_ = false;
    f32 capture_flash_ = 0.0f;
    f32 capture_msg_until_ = 0.0f;
    FixedString<48> capture_msg_;
    f32 video_timer_ = 0.0f;
    bool place_active_ = false;
    bool place_valid_ = false;
    Vec3 place_pos_;
    f32 place_yaw_ = 0.0f;

    bool terrain_dirty_ = false;
    u32 islands_uploaded_ = 0;
    u32 islands_reported_ = 0;
    bool islands_patches_dirty_ = true;
    std::vector<const GpuMesh*> island_meshes_;
    const GpuMesh* debris_meshes_[IslandField::kRockVariants + IslandField::kTurfVariants] = {};
};

}
