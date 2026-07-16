#pragma once

#include "core/types.h"

typedef enum SfxKind {
    SFX_DOOR_OPEN,
    SFX_DOOR_CLOSE,
    SFX_HOOD,
    SFX_RATCHET,
    SFX_IMPACT,
    SFX_THUMP,
    SFX_ENGINE_START,
    SFX_KIND_COUNT,
} SfxKind;

b32  audio_init(void);
void audio_shutdown(void);
void audio_play(SfxKind kind, f32 volume, f32 pitch);
void audio_engine_set(f32 rpm, f32 load, b32 running, b32 cranking, f32 dt);
void audio_rolling_set(f32 speed, f32 road_amount, b32 grounded, f32 dt);
void audio_skid_set(f32 intensity, f32 dt);
