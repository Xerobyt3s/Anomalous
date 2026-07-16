#include "audio/audio.h"
#include "core/log.h"
#include "core/arena.h"
#include "core/config.h"
#include "math/vmath.h"
#include "platform/platform.h"

#include <stdio.h>

#define MA_NO_MP3
#define MA_NO_FLAC
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#pragma warning(push)
#pragma warning(disable : 4100 4101 4189 4200 4244 4245 4267 4295 4310 4324 4389 4456 4457 4459 4505 4701 4702 4706 4996)
#include "miniaudio.h"
#pragma warning(pop)

#define SFX_VOICES 4
#define ENGINE_MAX_PULSES 12

typedef struct SfxBank {
    ma_sound voices[SFX_VOICES];
    u32 next;
    b32 loaded;
} SfxBank;

typedef struct EnginePulse {
    f32 amp;
    f32 decay_mul;
    f32 phase;
    f32 phase_inc;
} EnginePulse;

typedef struct EngineSynth {
    ma_data_source_base ds;
    volatile f32 rpm;
    volatile f32 load;
    volatile b32 running;
    volatile b32 cranking;
    f32 fire_phase;
    u32 rng_state;
    EnginePulse pulses[ENGINE_MAX_PULSES];
    u32 pulse_next;
    u32 fire_count;
    f32 noise_lp;
    f32 out_lp;
    u32 sample_rate;
} EngineSynth;

#define ENGINE_SET_MAX 8

typedef struct EngineSet {
    ma_sound loops[ENGINE_SET_MAX];
    f32 base_rpm[ENGINE_SET_MAX];
    f32 vol[ENGINE_SET_MAX];
    u32 count;
    b32 loaded;
    f32 master;
} EngineSet;

static ma_engine s_engine;
static b32 s_ok;
static SfxBank s_sfx[SFX_KIND_COUNT];
static EngineSynth s_synth;
static ma_sound s_engine_sound;
static b32 s_synth_ok;
static EngineSet s_engine_set;
static ma_sound s_starter;
static ma_sound s_skid;
static ma_sound s_roll_road;
static ma_sound s_roll_grass;
static ma_sound s_horn;
static b32 s_loops_loaded;
static f32 s_horn_vol;
static f32 s_skid_vol;
static f32 s_roll_road_vol;
static f32 s_roll_grass_vol;

static f32 synth_rand01(EngineSynth* s)
{
    s->rng_state ^= s->rng_state << 13;
    s->rng_state ^= s->rng_state >> 17;
    s->rng_state ^= s->rng_state << 5;
    return (f32)(s->rng_state >> 8) / 16777216.0f;
}

static ma_result engine_synth_read(ma_data_source* ds, void* out, ma_uint64 frame_count,
                                   ma_uint64* frames_read)
{
    EngineSynth* s = (EngineSynth*)ds;
    f32* dst = (f32*)out;
    f32 rate = (f32)s->sample_rate;
    f32 rpm = s->rpm;
    f32 load = s->load;
    f32 fire_hz = f_max(rpm, 0.0f) / 60.0f * 2.0f;
    f32 base_amp = s->running ? 0.24f + 0.50f * f_clamp01(load)
                 : (s->cranking ? 0.16f : 0.0f);
    f32 rpm_norm = f_clamp01(rpm / 6500.0f);

    for (ma_uint64 i = 0; i < frame_count; i++) {
        s->fire_phase += fire_hz / rate;
        if (s->fire_phase >= 1.0f) {
            s->fire_phase -= 1.0f;
            if (base_amp > 0.0f) {
                EnginePulse* p = &s->pulses[s->pulse_next];
                s->pulse_next = (s->pulse_next + 1) % ENGINE_MAX_PULSES;
                f32 jitter = 0.72f + 0.56f * synth_rand01(s);
                f32 accent = (s->fire_count++ & 3u) == 0 ? 1.35f : 1.0f;
                f32 freq = 82.0f + 55.0f * load + rpm * 0.006f + synth_rand01(s) * 14.0f;
                f32 decay = 42.0f + 195.0f * rpm_norm;
                p->amp = base_amp * jitter * accent;
                p->decay_mul = expf(-decay / rate);
                p->phase = 0.0f;
                p->phase_inc = freq / rate;
            }
        }

        f32 sample = 0.0f;
        for (u32 k = 0; k < ENGINE_MAX_PULSES; k++) {
            EnginePulse* p = &s->pulses[k];
            if (p->amp < 0.002f) {
                continue;
            }
            sample += p->amp * sinf(2.0f * PI32 * p->phase);
            p->amp *= p->decay_mul;
            p->phase += p->phase_inc;
        }

        f32 white = synth_rand01(s) * 2.0f - 1.0f;
        s->noise_lp += 0.22f * (white - s->noise_lp);
        sample += s->noise_lp * (0.015f + 0.12f * load) * rpm_norm * 2.2f;

        sample = sample * 1.7f / (1.0f + f_abs(sample * 1.7f));
        s->out_lp += 0.5f * (sample - s->out_lp);
        dst[i] = s->out_lp;
    }
    if (frames_read) {
        *frames_read = frame_count;
    }
    return MA_SUCCESS;
}

static ma_result engine_synth_seek(ma_data_source* ds, ma_uint64 frame)
{
    (void)ds;
    (void)frame;
    return MA_SUCCESS;
}

static ma_result engine_synth_format(ma_data_source* ds, ma_format* format, ma_uint32* channels,
                                     ma_uint32* sample_rate, ma_channel* channel_map,
                                     size_t channel_map_cap)
{
    EngineSynth* s = (EngineSynth*)ds;
    (void)channel_map;
    (void)channel_map_cap;
    *format = ma_format_f32;
    *channels = 1;
    *sample_rate = s->sample_rate;
    return MA_SUCCESS;
}

static ma_result engine_synth_cursor(ma_data_source* ds, ma_uint64* cursor)
{
    (void)ds;
    *cursor = 0;
    return MA_SUCCESS;
}

static ma_result engine_synth_length(ma_data_source* ds, ma_uint64* length)
{
    (void)ds;
    *length = 0;
    return MA_SUCCESS;
}

static ma_data_source_vtable s_engine_synth_vtable = {
    engine_synth_read,
    engine_synth_seek,
    engine_synth_format,
    engine_synth_cursor,
    engine_synth_length,
    0,
    0,
};

static const char* s_sfx_paths[SFX_KIND_COUNT] = {
    "assets/audio/door_open.wav",
    "assets/audio/door_close.wav",
    "assets/audio/hood.wav",
    "assets/audio/ratchet.wav",
    "assets/audio/impact.wav",
    "assets/audio/thump.wav",
    "assets/audio/flap.wav",
    "assets/audio/engine_start.wav",
};

static b32 load_loop(ma_sound* sound, const char* path)
{
    if (ma_sound_init_from_file(&s_engine, path, MA_SOUND_FLAG_DECODE, 0, 0, sound) != MA_SUCCESS) {
        log_warn("audio: failed to load %s", path);
        return 0;
    }
    ma_sound_set_looping(sound, MA_TRUE);
    ma_sound_set_volume(sound, 0.0f);
    ma_sound_start(sound);
    return 1;
}

b32 audio_init(void)
{
    if (ma_engine_init(0, &s_engine) != MA_SUCCESS) {
        log_warn("audio: device init failed, running silent");
        s_ok = 0;
        return 0;
    }
    s_ok = 1;

    for (u32 k = 0; k < SFX_KIND_COUNT; k++) {
        s_sfx[k].loaded = 1;
        for (u32 v = 0; v < SFX_VOICES; v++) {
            if (ma_sound_init_from_file(&s_engine, s_sfx_paths[k], MA_SOUND_FLAG_DECODE, 0, 0,
                                        &s_sfx[k].voices[v]) != MA_SUCCESS) {
                s_sfx[k].loaded = 0;
                log_warn("audio: failed to load %s", s_sfx_paths[k]);
                break;
            }
        }
    }

    s_loops_loaded = load_loop(&s_starter, "assets/audio/starter.wav")
                   && load_loop(&s_skid, "assets/audio/skid.wav")
                   && load_loop(&s_roll_road, "assets/audio/roll_road.wav")
                   && load_loop(&s_roll_grass, "assets/audio/roll_grass.wav")
                   && load_loop(&s_horn, "assets/audio/horn.wav");

    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    FileData set_file = platform_read_entire_file(&g_frame_arena, "assets/audio/engine_set.cfg");
    if (set_file.data) {
        Config cfg;
        if (config_parse(&cfg, &g_frame_arena, (const char*)set_file.data)) {
            u32 count = (u32)config_get_i32(&cfg, "engine_set.count", 0);
            count = count > ENGINE_SET_MAX ? ENGINE_SET_MAX : count;
            u32 loaded = 0;
            for (u32 i = 0; i < count; i++) {
                char key[64];
                char path[192];
                snprintf(key, sizeof(key), "engine_set.loop%u_file", i);
                snprintf(path, sizeof(path), "assets/audio/%s", config_get_str(&cfg, key, ""));
                snprintf(key, sizeof(key), "engine_set.loop%u_rpm", i);
                f32 rpm = config_get_f32(&cfg, key, 0.0f);
                if (rpm <= 0.0f) {
                    continue;
                }
                if (ma_sound_init_from_file(&s_engine, path, MA_SOUND_FLAG_DECODE, 0, 0,
                                            &s_engine_set.loops[loaded]) != MA_SUCCESS) {
                    log_warn("audio: failed to load %s", path);
                    continue;
                }
                ma_sound_set_looping(&s_engine_set.loops[loaded], MA_TRUE);
                ma_sound_set_volume(&s_engine_set.loops[loaded], 0.0f);
                ma_sound_start(&s_engine_set.loops[loaded]);
                s_engine_set.base_rpm[loaded] = rpm;
                loaded++;
            }
            s_engine_set.count = loaded;
            s_engine_set.loaded = loaded >= 2;
        }
    }
    arena_temp_end(temp);

    if (!s_engine_set.loaded) {
        ma_data_source_config ds_config = ma_data_source_config_init();
        ds_config.vtable = &s_engine_synth_vtable;
        s_synth.sample_rate = ma_engine_get_sample_rate(&s_engine);
        s_synth.rng_state = 0x2545F491u;
        s_synth_ok = ma_data_source_init(&ds_config, &s_synth.ds) == MA_SUCCESS
                  && ma_sound_init_from_data_source(&s_engine, &s_synth, 0, 0,
                                                    &s_engine_sound) == MA_SUCCESS;
        if (s_synth_ok) {
            ma_sound_set_volume(&s_engine_sound, 0.85f);
            ma_sound_start(&s_engine_sound);
        } else {
            log_warn("audio: engine synth init failed");
        }
    }

    log_info("audio: initialized (%s, engine %s)",
             s_loops_loaded ? "loops loaded" : "missing loops",
             s_engine_set.loaded ? "sample set" : (s_synth_ok ? "synth fallback" : "off"));
    return 1;
}

void audio_shutdown(void)
{
    if (!s_ok) {
        return;
    }
    if (s_engine_set.loaded) {
        for (u32 i = 0; i < s_engine_set.count; i++) {
            ma_sound_uninit(&s_engine_set.loops[i]);
        }
    }
    if (s_synth_ok) {
        ma_sound_uninit(&s_engine_sound);
        ma_data_source_uninit(&s_synth.ds);
    }
    if (s_loops_loaded) {
        ma_sound_uninit(&s_starter);
        ma_sound_uninit(&s_skid);
        ma_sound_uninit(&s_roll_road);
        ma_sound_uninit(&s_roll_grass);
        ma_sound_uninit(&s_horn);
    }
    for (u32 k = 0; k < SFX_KIND_COUNT; k++) {
        if (!s_sfx[k].loaded) {
            continue;
        }
        for (u32 v = 0; v < SFX_VOICES; v++) {
            ma_sound_uninit(&s_sfx[k].voices[v]);
        }
    }
    ma_engine_uninit(&s_engine);
    s_ok = 0;
}

void audio_play(SfxKind kind, f32 volume, f32 pitch)
{
    if (!s_ok || !s_sfx[kind].loaded) {
        return;
    }
    SfxBank* bank = &s_sfx[kind];
    ma_sound* voice = &bank->voices[bank->next];
    bank->next = (bank->next + 1) % SFX_VOICES;
    ma_sound_stop(voice);
    ma_sound_seek_to_pcm_frame(voice, 0);
    ma_sound_set_volume(voice, volume);
    ma_sound_set_pitch(voice, pitch);
    ma_sound_start(voice);
}

void audio_engine_set(f32 rpm, f32 load, b32 running, b32 cranking, f32 dt)
{
    if (!s_ok) {
        return;
    }
    if (s_engine_set.loaded) {
        EngineSet* set = &s_engine_set;
        f32 master_target = running ? 0.30f + 0.45f * f_clamp01(load)
                          : (cranking ? 0.10f : 0.0f);
        set->master = f_approach_exp(set->master, master_target, 10.0f, dt);

        f32 target[ENGINE_SET_MAX] = {0};
        f32 rpm_eff = f_max(rpm, 100.0f);
        if (set->master > 0.001f) {
            if (rpm_eff <= set->base_rpm[0]) {
                target[0] = 1.0f;
            } else if (rpm_eff >= set->base_rpm[set->count - 1]) {
                target[set->count - 1] = 1.0f;
            } else {
                for (u32 i = 0; i + 1 < set->count; i++) {
                    if (rpm_eff >= set->base_rpm[i] && rpm_eff <= set->base_rpm[i + 1]) {
                        f32 w = (rpm_eff - set->base_rpm[i])
                              / (set->base_rpm[i + 1] - set->base_rpm[i]);
                        target[i] = cosf(w * PI32 * 0.5f);
                        target[i + 1] = sinf(w * PI32 * 0.5f);
                        break;
                    }
                }
            }
        }
        for (u32 i = 0; i < set->count; i++) {
            set->vol[i] = f_approach_exp(set->vol[i], target[i] * set->master, 18.0f, dt);
            ma_sound_set_volume(&set->loops[i], set->vol[i]);
            if (set->vol[i] > 0.002f) {
                ma_sound_set_pitch(&set->loops[i],
                                   f_clamp(rpm_eff / set->base_rpm[i], 0.45f, 2.3f));
            }
        }
    } else if (s_synth_ok) {
        s_synth.rpm = rpm;
        s_synth.load = f_clamp01(load);
        s_synth.running = running;
        s_synth.cranking = cranking;
    }
    if (s_loops_loaded) {
        ma_sound_set_volume(&s_starter, cranking ? 0.5f : 0.0f);
    }
}

void audio_rolling_set(f32 speed, f32 road_amount, b32 grounded, f32 dt)
{
    if (!s_ok || !s_loops_loaded) {
        return;
    }
    f32 base = grounded ? f_clamp01(speed / 40.0f) * 0.18f : 0.0f;
    f32 road_target = base * road_amount;
    f32 grass_target = base * (1.0f - road_amount) * 1.15f;
    s_roll_road_vol = f_approach_exp(s_roll_road_vol, road_target, 8.0f, dt);
    s_roll_grass_vol = f_approach_exp(s_roll_grass_vol, grass_target, 8.0f, dt);
    ma_sound_set_volume(&s_roll_road, s_roll_road_vol);
    ma_sound_set_volume(&s_roll_grass, s_roll_grass_vol);
    f32 pitch = 0.8f + f_clamp01(speed / 35.0f) * 0.5f;
    ma_sound_set_pitch(&s_roll_road, pitch);
    ma_sound_set_pitch(&s_roll_grass, pitch);
}

void audio_horn_set(b32 on, f32 dt)
{
    if (!s_ok || !s_loops_loaded) {
        return;
    }
    s_horn_vol = f_approach_exp(s_horn_vol, on ? 0.5f : 0.0f, 40.0f, dt);
    ma_sound_set_volume(&s_horn, s_horn_vol);
}

void audio_skid_set(f32 intensity, f32 dt)
{
    if (!s_ok || !s_loops_loaded) {
        return;
    }
    s_skid_vol = f_approach_exp(s_skid_vol, f_clamp01(intensity) * 0.55f, 14.0f, dt);
    ma_sound_set_volume(&s_skid, s_skid_vol);
    ma_sound_set_pitch(&s_skid, 0.9f + 0.25f * f_clamp01(intensity));
}
