#pragma once

#include "core/types.h"
#include "math/vmath.h"

typedef enum SfxKind {
    SFX_DOOR_OPEN,
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
} SfxKind;

b32  audio_init(void);
void audio_shutdown(void);
void audio_listener_set(Vec3 pos, Vec3 forward, Vec3 vel);
void audio_car_set(Vec3 engine_pos, Vec3 center_pos, Vec3 dash_pos, Vec3 vel);
void audio_occlusion_set(f32 factor, f32 dt);
void audio_play(SfxKind kind, f32 volume, f32 pitch);
void audio_play_at(SfxKind kind, f32 volume, f32 pitch, Vec3 pos);
void audio_engine_set(f32 rpm, f32 load, b32 running, b32 cranking, f32 dt);
void audio_rolling_set(f32 speed, f32 road_amount, b32 grounded, f32 wetness, f32 dt);
void audio_rain_set(f32 exterior, f32 roof, f32 dt);
void audio_skid_set(f32 intensity, f32 dt);
void audio_horn_set(b32 on, f32 dt);

b32  audio_tape_play(const char* path);
void audio_tape_stop(void);
b32  audio_tape_playing(void);
void audio_tape_set(f32 speed, f32 condition, f32 volume, f32 dt);
