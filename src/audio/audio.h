#pragma once

#include "core/types.h"
#include "math/vmath.h"

#include <memory>
#include <span>
#include <string_view>

namespace ghost::engine {
class AudioEngine;
}

namespace anom {

struct AudioBackend;

inline constexpr u32 kEngineLayerMax = 8;

enum SfxKind : u32 {
    SFX_DOOR_OPEN = 0,
    SFX_DOOR_CLOSE,
    SFX_HOOD,
    SFX_RATCHET,
    SFX_IMPACT,
    SFX_THUMP,
    SFX_FLAP,
    SFX_ENGINE_START,
    SFX_WHIR,
    SFX_WIPER,
    SFX_KIND_COUNT,
};

void engine_crossfade(std::span<const f32> base_rpm, f32 rpm, std::span<f32> out);
f32 skid_intensity(f32 slide_lat, f32 slide_long);
f32 engine_load_lowpass(f32 load);
f32 cabin_lowpass(f32 occlusion);
f32 wind_volume(f32 speed, bool seated);
f32 wind_lowpass(f32 speed);
void fill_noise_loop(std::span<f32> out, u32 seed);

class Audio {
public:
    Audio();
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    bool init(ghost::engine::AudioEngine& engine);
    void shutdown();

    bool ok() const { return backend_ != nullptr; }

    void set_listener(Vec3 pos, Vec3 forward, Vec3 up, Vec3 vel);
    void set_car(Vec3 engine_pos, Vec3 center_pos, Vec3 dash_pos, Vec3 vel);
    void set_occlusion(f32 factor, f32 dt);
    f32 occlusion() const;

    void play(SfxKind kind, f32 volume, f32 pitch);
    void play_at(SfxKind kind, f32 volume, f32 pitch, Vec3 pos);

    void set_engine(f32 rpm, f32 load, bool running, bool cranking, f32 dt, bool shifting = false);
    void set_rolling(f32 speed, f32 road_amount, bool grounded, f32 wetness, f32 dt);
    void set_rain(f32 exterior, f32 roof, f32 dt);
    void set_skid(f32 intensity, f32 dt);
    void set_skid_surface(f32 squeal, f32 grass, f32 dt);
    void set_horn(bool on, f32 dt);
    void set_wind(f32 speed, bool seated, f32 dt);

    bool tape_play(std::string_view path);
    void tape_stop();
    bool tape_playing() const;
    void set_tape(f32 speed, f32 condition, f32 volume, f32 dt);

    u32 engine_layer_count() const;
    f32 engine_layer_volume(u32 index) const;
    bool sfx_available(SfxKind kind) const;

private:
    std::unique_ptr<AudioBackend> backend_;
};

}
