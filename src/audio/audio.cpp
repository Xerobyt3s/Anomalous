#include "audio/audio.h"

#include "audio/ma.h"
#include "core/arena.h"
#include "core/config.h"
#include "core/log.h"
#include "platform/filesystem.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <new>

namespace anom {

namespace {

constexpr u32 kSfxVoices = 4;
constexpr u32 kEnginePulses = 12;
constexpr u32 kTapeLpfOrder = 4;
constexpr u32 kTapeHpfOrder = 2;

constexpr const char* kSfxPaths[SFX_KIND_COUNT] = {
    "assets/audio/door_open.wav",
    "assets/audio/door_close.wav",
    "assets/audio/hood.wav",
    "assets/audio/ratchet.wav",
    "assets/audio/impact.wav",
    "assets/audio/thump.wav",
    "assets/audio/flap.wav",
    "assets/audio/engine_start.wav",
    "assets/audio/whir.wav",
    "assets/audio/wiper.wav",
};

} // namespace

struct AudioBackend {
    struct Pulse {
        f32 amp;
        f32 decay_mul;
        f32 phase;
        f32 phase_inc;
    };

    struct Synth {
        ma_data_source_base ds;
        std::atomic<f32> rpm;
        std::atomic<f32> load;
        std::atomic<bool> running;
        std::atomic<bool> cranking;
        f32 fire_phase;
        u32 rng_state;
        Pulse pulses[kEnginePulses];
        u32 pulse_next;
        u32 fire_count;
        f32 noise_lp;
        f32 out_lp;
        u32 sample_rate;
    };

    struct Bank {
        ma_sound voices[kSfxVoices];
        u32 next;
        u32 loaded;
    };

    struct Loop {
        ma_sound sound;
        bool loaded;
    };

    struct EngineSet {
        ma_sound loops[kEngineLayerMax];
        f32 base_rpm[kEngineLayerMax];
        f32 vol[kEngineLayerMax];
        u32 count;
        bool loaded;
        f32 master;
    };

    ma_engine engine;
    Bank sfx[SFX_KIND_COUNT];

    Synth synth;
    ma_sound synth_sound;
    bool synth_ok;
    EngineSet set;

    Loop starter;
    Loop skid;
    Loop roll_road;
    Loop roll_grass;
    Loop roll_wet;
    Loop horn;
    Loop rain_light;
    Loop rain_heavy;
    Loop rain_roof;

    f32 horn_vol;
    f32 skid_vol;
    f32 roll_road_vol;
    f32 roll_grass_vol;
    f32 roll_wet_vol;
    f32 rain_light_vol;
    f32 rain_heavy_vol;
    f32 rain_roof_vol;

    Vec3 dash_pos;
    Vec3 car_vel;
    f32 occ;

    ma_sound tape;
    ma_hpf_node tape_hpf;
    ma_lpf_node tape_lpf;
    bool tape_active;
    bool tape_chain;
    f32 tape_speed_sm;
    f32 tape_wobble_t;
    f32 tape_cutoff;
};

namespace {

f32 synth_rand01(AudioBackend::Synth* s)
{
    s->rng_state ^= s->rng_state << 13;
    s->rng_state ^= s->rng_state >> 17;
    s->rng_state ^= s->rng_state << 5;
    return static_cast<f32>(s->rng_state >> 8) / 16777216.0f;
}

ma_result synth_read(ma_data_source* ds, void* out, ma_uint64 frame_count, ma_uint64* frames_read)
{
    auto* s = reinterpret_cast<AudioBackend::Synth*>(ds);
    f32* dst = static_cast<f32*>(out);
    const f32 rate = static_cast<f32>(s->sample_rate);
    const f32 rpm = s->rpm.load(std::memory_order_relaxed);
    const f32 load = s->load.load(std::memory_order_relaxed);
    const bool running = s->running.load(std::memory_order_relaxed);
    const bool cranking = s->cranking.load(std::memory_order_relaxed);
    const f32 fire_hz = f_max(rpm, 0.0f) / 60.0f * 2.0f;
    const f32 base_amp = running ? 0.24f + 0.50f * load : (cranking ? 0.16f : 0.0f);
    const f32 rpm_norm = f_clamp01(rpm / 6500.0f);

    for (ma_uint64 i = 0; i < frame_count; i++) {
        s->fire_phase += fire_hz / rate;
        if (s->fire_phase >= 1.0f) {
            s->fire_phase -= 1.0f;
            if (base_amp > 0.0f) {
                AudioBackend::Pulse* p = &s->pulses[s->pulse_next];
                s->pulse_next = (s->pulse_next + 1) % kEnginePulses;
                const f32 jitter = 0.72f + 0.56f * synth_rand01(s);
                const f32 accent = (s->fire_count++ & 3u) == 0 ? 1.35f : 1.0f;
                const f32 freq = 82.0f + 55.0f * load + rpm * 0.006f + synth_rand01(s) * 14.0f;
                const f32 decay = 42.0f + 195.0f * rpm_norm;
                p->amp = base_amp * jitter * accent;
                p->decay_mul = std::exp(-decay / rate);
                p->phase = 0.0f;
                p->phase_inc = freq / rate;
            }
        }

        f32 sample = 0.0f;
        for (u32 k = 0; k < kEnginePulses; k++) {
            AudioBackend::Pulse* p = &s->pulses[k];
            if (p->amp < 0.002f) {
                continue;
            }
            sample += p->amp * std::sin(kTau * p->phase);
            p->amp *= p->decay_mul;
            p->phase += p->phase_inc;
        }

        const f32 white = synth_rand01(s) * 2.0f - 1.0f;
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

ma_result synth_seek(ma_data_source* ds, ma_uint64 frame)
{
    (void)ds;
    (void)frame;
    return MA_SUCCESS;
}

ma_result synth_format(ma_data_source* ds, ma_format* format, ma_uint32* channels,
                       ma_uint32* sample_rate, ma_channel* channel_map, size_t channel_map_cap)
{
    (void)channel_map;
    (void)channel_map_cap;
    auto* s = reinterpret_cast<AudioBackend::Synth*>(ds);
    *format = ma_format_f32;
    *channels = 1;
    *sample_rate = s->sample_rate;
    return MA_SUCCESS;
}

ma_result synth_cursor(ma_data_source* ds, ma_uint64* cursor)
{
    (void)ds;
    *cursor = 0;
    return MA_SUCCESS;
}

ma_result synth_length(ma_data_source* ds, ma_uint64* length)
{
    (void)ds;
    *length = 0;
    return MA_SUCCESS;
}

ma_data_source_vtable g_synth_vtable = {
    synth_read, synth_seek, synth_format, synth_cursor, synth_length, nullptr, 0,
};

void sound_spatial(ma_sound* sound, f32 min_dist, f32 max_dist)
{
    ma_sound_set_spatialization_enabled(sound, MA_TRUE);
    ma_sound_set_attenuation_model(sound, ma_attenuation_model_inverse);
    ma_sound_set_min_distance(sound, min_dist);
    ma_sound_set_max_distance(sound, max_dist);
    ma_sound_set_rolloff(sound, 1.0f);
}

void sound_place(ma_sound* sound, Vec3 pos, Vec3 vel)
{
    ma_sound_set_position(sound, pos.x, pos.y, pos.z);
    ma_sound_set_velocity(sound, vel.x, vel.y, vel.z);
}

bool load_loop(ma_engine& engine, AudioBackend::Loop& loop, const char* path, f32 min_dist,
               f32 max_dist)
{
    if (ma_sound_init_from_file(&engine, path, MA_SOUND_FLAG_DECODE, nullptr, nullptr,
                                &loop.sound)
        != MA_SUCCESS) {
        log_warn("audio: failed to load %s", path);
        return false;
    }
    ma_sound_set_looping(&loop.sound, MA_TRUE);
    ma_sound_set_volume(&loop.sound, 0.0f);
    sound_spatial(&loop.sound, min_dist, max_dist);
    ma_sound_start(&loop.sound);
    loop.loaded = true;
    return true;
}

void unload_loop(AudioBackend::Loop& loop)
{
    if (loop.loaded) {
        ma_sound_uninit(&loop.sound);
        loop.loaded = false;
    }
}

void set_loop(AudioBackend::Loop& loop, f32 volume)
{
    if (loop.loaded) {
        ma_sound_set_volume(&loop.sound, volume);
    }
}

void place_loop(AudioBackend::Loop& loop, Vec3 pos, Vec3 vel)
{
    if (loop.loaded) {
        sound_place(&loop.sound, pos, vel);
    }
}

void pitch_loop(AudioBackend::Loop& loop, f32 pitch)
{
    if (loop.loaded) {
        ma_sound_set_pitch(&loop.sound, pitch);
    }
}

void tape_release(AudioBackend& be)
{
    if (be.tape_active) {
        ma_sound_uninit(&be.tape);
        be.tape_active = false;
    }
}

void load_engine_set(AudioBackend& be, Arena& arena)
{
    ArenaScope scope(arena);
    const fs::FileData file = fs::read_entire_file(arena, "assets/audio/engine_set.cfg");
    if (!file.valid()) {
        return;
    }
    Config cfg;
    if (!cfg.parse(arena, file.text())) {
        return;
    }

    const i32 declared = cfg.get_i32("engine_set.count", 0);
    u32 count = declared <= 0 ? 0u : static_cast<u32>(declared);
    count = count > kEngineLayerMax ? kEngineLayerMax : count;

    struct Layer {
        f32 rpm;
        char path[192];
    };
    Layer layers[kEngineLayerMax];
    u32 layer_count = 0;

    for (u32 i = 0; i < count; i++) {
        char key[64];
        std::snprintf(key, sizeof(key), "engine_set.loop%u_rpm", i);
        const f32 rpm = cfg.get_f32(key, 0.0f);
        if (rpm <= 0.0f) {
            continue;
        }
        std::snprintf(key, sizeof(key), "engine_set.loop%u_file", i);
        const std::string_view name = cfg.get_str(key, "");
        if (name.empty()) {
            continue;
        }

        u32 slot = layer_count;
        while (slot > 0 && layers[slot - 1].rpm > rpm) {
            slot--;
        }
        if (slot > 0 && f_abs(layers[slot - 1].rpm - rpm) < 1.0f) {
            log_warn("audio: engine_set loop%u duplicates %.0f rpm", i, static_cast<f64>(rpm));
            continue;
        }
        for (u32 m = layer_count; m > slot; m--) {
            layers[m] = layers[m - 1];
        }
        layers[slot].rpm = rpm;
        std::snprintf(layers[slot].path, sizeof(layers[slot].path), "assets/audio/%.*s",
                      static_cast<int>(name.size()), name.data());
        layer_count++;
    }

    for (u32 i = 0; i < layer_count; i++) {
        const u32 slot = be.set.count;
        if (ma_sound_init_from_file(&be.engine, layers[i].path, MA_SOUND_FLAG_DECODE, nullptr,
                                    nullptr, &be.set.loops[slot])
            != MA_SUCCESS) {
            log_warn("audio: failed to load %s", layers[i].path);
            continue;
        }
        be.set.base_rpm[slot] = layers[i].rpm;
        be.set.count++;

        ma_sound_set_looping(&be.set.loops[slot], MA_TRUE);
        ma_sound_set_volume(&be.set.loops[slot], 0.0f);
        sound_spatial(&be.set.loops[slot], 3.0f, 160.0f);
        ma_sound_start(&be.set.loops[slot]);
    }
    be.set.loaded = be.set.count >= 2;
}

ma_sound* pick_voice(AudioBackend::Bank& bank, f32 volume, f32 pitch)
{
    ma_sound* voice = &bank.voices[bank.next];
    bank.next = (bank.next + 1) % bank.loaded;
    ma_sound_stop(voice);
    ma_sound_seek_to_pcm_frame(voice, 0);
    ma_sound_set_volume(voice, volume);
    ma_sound_set_pitch(voice, f_max(pitch, 0.02f));
    return voice;
}

} // namespace

void engine_crossfade(std::span<const f32> base_rpm, f32 rpm, std::span<f32> out)
{
    for (f32& w : out) {
        w = 0.0f;
    }
    const u32 count = static_cast<u32>(base_rpm.size());
    if (count == 0 || out.size() < base_rpm.size()) {
        return;
    }
    if (rpm <= base_rpm[0]) {
        out[0] = 1.0f;
        return;
    }
    if (rpm >= base_rpm[count - 1]) {
        out[count - 1] = 1.0f;
        return;
    }
    for (u32 i = 0; i + 1 < count; i++) {
        const f32 lo = base_rpm[i];
        const f32 hi = base_rpm[i + 1];
        if (rpm < lo || rpm > hi) {
            continue;
        }
        const f32 span = hi - lo;
        const f32 w = span > 1e-3f ? (rpm - lo) / span : 0.0f;
        out[i] = std::cos(w * kPi * 0.5f);
        out[i + 1] = std::sin(w * kPi * 0.5f);
        return;
    }
}

bool Audio::init(Arena& arena)
{
    if (backend_) {
        return true;
    }

    void* mem = arena.push_bytes(sizeof(AudioBackend), alignof(AudioBackend));
    if (!mem) {
        log_warn("audio: no arena space, running silent");
        return false;
    }
    auto* be = new (mem) AudioBackend{};
    be->occ = 1.0f;
    be->tape_speed_sm = 1.0f;

    if (ma_engine_init(nullptr, &be->engine) != MA_SUCCESS) {
        log_warn("audio: device init failed, running silent");
        return false;
    }
    backend_ = be;

    for (u32 k = 0; k < SFX_KIND_COUNT; k++) {
        AudioBackend::Bank& bank = be->sfx[k];
        for (u32 v = 0; v < kSfxVoices; v++) {
            if (ma_sound_init_from_file(&be->engine, kSfxPaths[k], MA_SOUND_FLAG_DECODE, nullptr,
                                        nullptr, &bank.voices[v])
                != MA_SUCCESS) {
                log_warn("audio: failed to load %s", kSfxPaths[k]);
                break;
            }
            sound_spatial(&bank.voices[v], 2.0f, 90.0f);
            bank.loaded++;
        }
    }

    load_loop(be->engine, be->starter, "assets/audio/starter.wav", 3.0f, 120.0f);
    load_loop(be->engine, be->skid, "assets/audio/skid.wav", 3.0f, 120.0f);
    load_loop(be->engine, be->roll_road, "assets/audio/roll_road.wav", 3.0f, 120.0f);
    load_loop(be->engine, be->roll_grass, "assets/audio/roll_grass.wav", 3.0f, 120.0f);
    load_loop(be->engine, be->horn, "assets/audio/horn.wav", 3.0f, 120.0f);
    load_loop(be->engine, be->roll_wet, "assets/audio/roll_road_wet.wav", 3.0f, 120.0f);

    AudioBackend::Loop* rain[] = {&be->rain_light, &be->rain_heavy, &be->rain_roof};
    const char* rain_paths[] = {"assets/audio/rain_light.wav", "assets/audio/rain_heavy.wav",
                                "assets/audio/rain_roof.wav"};
    for (u32 i = 0; i < array_count(rain); i++) {
        if (load_loop(be->engine, *rain[i], rain_paths[i], 3.0f, 120.0f)) {
            ma_sound_set_spatialization_enabled(&rain[i]->sound, MA_FALSE);
        }
    }

    load_engine_set(*be, arena);

    if (!be->set.loaded) {
        ma_data_source_config ds_config = ma_data_source_config_init();
        ds_config.vtable = &g_synth_vtable;
        be->synth.sample_rate = ma_engine_get_sample_rate(&be->engine);
        be->synth.rng_state = 0x2545F491u;
        be->synth_ok = ma_data_source_init(&ds_config, &be->synth.ds) == MA_SUCCESS
                       && ma_sound_init_from_data_source(&be->engine, &be->synth.ds, 0, nullptr,
                                                         &be->synth_sound)
                              == MA_SUCCESS;
        if (be->synth_ok) {
            ma_sound_set_volume(&be->synth_sound, 0.85f);
            sound_spatial(&be->synth_sound, 3.0f, 160.0f);
            ma_sound_start(&be->synth_sound);
        } else {
            log_warn("audio: engine synth init failed");
        }
    }

    ma_node_graph* graph = ma_engine_get_node_graph(&be->engine);
    const ma_uint32 channels = ma_engine_get_channels(&be->engine);
    const ma_uint32 rate = ma_engine_get_sample_rate(&be->engine);
    be->tape_cutoff = 4200.0f;
    ma_hpf_node_config hc = ma_hpf_node_config_init(channels, rate, 210.0, kTapeHpfOrder);
    ma_lpf_node_config lc = ma_lpf_node_config_init(channels, rate,
                                                    static_cast<f64>(be->tape_cutoff),
                                                    kTapeLpfOrder);
    be->tape_chain = ma_hpf_node_init(graph, &hc, nullptr, &be->tape_hpf) == MA_SUCCESS
                     && ma_lpf_node_init(graph, &lc, nullptr, &be->tape_lpf) == MA_SUCCESS;
    if (be->tape_chain) {
        ma_node_attach_output_bus(&be->tape_hpf, 0, &be->tape_lpf, 0);
        ma_node_attach_output_bus(&be->tape_lpf, 0, ma_engine_get_endpoint(&be->engine), 0);
    }

    log_info("audio: initialized (engine %s, %u layers)",
             be->set.loaded ? "sample set" : (be->synth_ok ? "synth" : "off"), be->set.count);
    return true;
}

void Audio::shutdown()
{
    if (!backend_) {
        return;
    }
    AudioBackend* be = backend_;
    backend_ = nullptr;

    tape_release(*be);
    if (be->tape_chain) {
        ma_lpf_node_uninit(&be->tape_lpf, nullptr);
        ma_hpf_node_uninit(&be->tape_hpf, nullptr);
        be->tape_chain = false;
    }
    for (u32 i = 0; i < be->set.count; i++) {
        ma_sound_uninit(&be->set.loops[i]);
    }
    be->set.count = 0;
    be->set.loaded = false;
    if (be->synth_ok) {
        ma_sound_uninit(&be->synth_sound);
        ma_data_source_uninit(&be->synth.ds);
        be->synth_ok = false;
    }
    unload_loop(be->starter);
    unload_loop(be->skid);
    unload_loop(be->roll_road);
    unload_loop(be->roll_grass);
    unload_loop(be->horn);
    unload_loop(be->roll_wet);
    unload_loop(be->rain_light);
    unload_loop(be->rain_heavy);
    unload_loop(be->rain_roof);
    for (u32 k = 0; k < SFX_KIND_COUNT; k++) {
        for (u32 v = 0; v < be->sfx[k].loaded; v++) {
            ma_sound_uninit(&be->sfx[k].voices[v]);
        }
        be->sfx[k].loaded = 0;
    }
    ma_engine_uninit(&be->engine);
}

void Audio::set_listener(Vec3 pos, Vec3 forward, Vec3 up, Vec3 vel)
{
    if (!backend_) {
        return;
    }
    ma_engine& engine = backend_->engine;
    ma_engine_listener_set_position(&engine, 0, pos.x, pos.y, pos.z);
    ma_engine_listener_set_direction(&engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&engine, 0, up.x, up.y, up.z);
    ma_engine_listener_set_velocity(&engine, 0, vel.x, vel.y, vel.z);
}

void Audio::set_occlusion(f32 factor, f32 dt)
{
    if (!backend_) {
        return;
    }
    backend_->occ = f_approach_exp(backend_->occ, f_clamp01(factor), 9.0f, dt);
}

f32 Audio::occlusion() const
{
    return backend_ ? backend_->occ : 1.0f;
}

void Audio::set_car(Vec3 engine_pos, Vec3 center_pos, Vec3 dash_pos, Vec3 vel)
{
    if (!backend_) {
        return;
    }
    AudioBackend& be = *backend_;
    be.dash_pos = dash_pos;
    be.car_vel = vel;

    for (u32 i = 0; i < be.set.count; i++) {
        sound_place(&be.set.loops[i], engine_pos, vel);
    }
    if (be.synth_ok) {
        sound_place(&be.synth_sound, engine_pos, vel);
    }
    place_loop(be.starter, engine_pos, vel);
    place_loop(be.horn, engine_pos, vel);
    place_loop(be.skid, center_pos, vel);
    place_loop(be.roll_road, center_pos, vel);
    place_loop(be.roll_grass, center_pos, vel);
    place_loop(be.roll_wet, center_pos, vel);
    if (be.tape_active) {
        sound_place(&be.tape, dash_pos, vel);
    }
}

bool Audio::sfx_available(SfxKind kind) const
{
    return backend_ && backend_->sfx[kind].loaded > 0;
}

void Audio::play(SfxKind kind, f32 volume, f32 pitch)
{
    if (!sfx_available(kind)) {
        return;
    }
    ma_sound* voice = pick_voice(backend_->sfx[kind], volume, pitch);
    ma_sound_set_spatialization_enabled(voice, MA_FALSE);
    ma_sound_start(voice);
}

void Audio::play_at(SfxKind kind, f32 volume, f32 pitch, Vec3 pos)
{
    if (!sfx_available(kind)) {
        return;
    }
    ma_sound* voice = pick_voice(backend_->sfx[kind], volume * backend_->occ, pitch);
    ma_sound_set_spatialization_enabled(voice, MA_TRUE);
    sound_place(voice, pos, Vec3{});
    ma_sound_start(voice);
}

void Audio::set_engine(f32 rpm, f32 load, bool running, bool cranking, f32 dt)
{
    if (!backend_) {
        return;
    }
    AudioBackend& be = *backend_;

    if (be.set.loaded) {
        AudioBackend::EngineSet& set = be.set;
        const f32 master_target = running ? 0.30f + 0.45f * f_clamp01(load)
                                          : (cranking ? 0.10f : 0.0f);
        set.master = f_approach_exp(set.master, master_target, 10.0f, dt);

        f32 target[kEngineLayerMax] = {};
        const f32 rpm_eff = f_max(rpm, 100.0f);
        if (set.master > 0.001f) {
            engine_crossfade(std::span<const f32>(set.base_rpm, set.count), rpm_eff, target);
        }
        for (u32 i = 0; i < set.count; i++) {
            set.vol[i] = f_approach_exp(set.vol[i], target[i] * set.master, 18.0f, dt);
            ma_sound_set_volume(&set.loops[i], set.vol[i] * be.occ);
            if (set.vol[i] > 0.002f) {
                ma_sound_set_pitch(&set.loops[i],
                                   f_clamp(rpm_eff / set.base_rpm[i], 0.45f, 2.3f));
            }
        }
    } else if (be.synth_ok) {
        be.synth.rpm.store(rpm, std::memory_order_relaxed);
        be.synth.load.store(f_clamp01(load), std::memory_order_relaxed);
        be.synth.running.store(running, std::memory_order_relaxed);
        be.synth.cranking.store(cranking, std::memory_order_relaxed);
        ma_sound_set_volume(&be.synth_sound, 0.85f * be.occ);
    }
    set_loop(be.starter, cranking ? 0.5f * be.occ : 0.0f);
}

u32 Audio::engine_layer_count() const
{
    return backend_ ? backend_->set.count : 0;
}

f32 Audio::engine_layer_volume(u32 index) const
{
    return backend_ && index < backend_->set.count ? backend_->set.vol[index] : 0.0f;
}

void Audio::set_rolling(f32 speed, f32 road_amount, bool grounded, f32 wetness, f32 dt)
{
    if (!backend_) {
        return;
    }
    AudioBackend& be = *backend_;
    const f32 base = grounded ? f_clamp01(speed / 40.0f) * 0.18f : 0.0f;
    const f32 wet = f_clamp01(wetness);
    const f32 road_target = base * road_amount * (1.0f - wet * 0.75f);
    const f32 wet_target = base * road_amount * wet * 1.5f;
    const f32 grass_target = base * (1.0f - road_amount) * (1.15f + wet * 0.35f);
    be.roll_road_vol = f_approach_exp(be.roll_road_vol, road_target, 8.0f, dt);
    be.roll_wet_vol = f_approach_exp(be.roll_wet_vol, wet_target, 8.0f, dt);
    be.roll_grass_vol = f_approach_exp(be.roll_grass_vol, grass_target, 8.0f, dt);
    set_loop(be.roll_road, be.roll_road_vol * be.occ);
    set_loop(be.roll_wet, be.roll_wet_vol * be.occ);
    set_loop(be.roll_grass, be.roll_grass_vol * be.occ);

    const f32 pitch = 0.8f + f_clamp01(speed / 35.0f) * 0.5f;
    pitch_loop(be.roll_road, pitch);
    pitch_loop(be.roll_wet, pitch);
    pitch_loop(be.roll_grass, pitch);
}

void Audio::set_rain(f32 exterior, f32 roof, f32 dt)
{
    if (!backend_) {
        return;
    }
    AudioBackend& be = *backend_;
    const f32 ext = f_clamp01(exterior);
    const f32 light_target = f_clamp01(ext * 2.2f) * 0.34f
                             * (1.0f - f_clamp01((ext - 0.5f) * 1.4f));
    const f32 heavy_target = f_clamp01((ext - 0.30f) / 0.55f) * 0.48f;
    const f32 roof_target = f_clamp01(roof) * 0.50f;
    be.rain_light_vol = f_approach_exp(be.rain_light_vol, light_target, 2.5f, dt);
    be.rain_heavy_vol = f_approach_exp(be.rain_heavy_vol, heavy_target, 2.5f, dt);
    be.rain_roof_vol = f_approach_exp(be.rain_roof_vol, roof_target, 4.0f, dt);
    set_loop(be.rain_light, be.rain_light_vol);
    set_loop(be.rain_heavy, be.rain_heavy_vol);
    set_loop(be.rain_roof, be.rain_roof_vol);
}

void Audio::set_horn(bool on, f32 dt)
{
    if (!backend_) {
        return;
    }
    AudioBackend& be = *backend_;
    be.horn_vol = f_approach_exp(be.horn_vol, on ? 0.5f : 0.0f, 40.0f, dt);
    set_loop(be.horn, be.horn_vol * be.occ);
}

void Audio::set_skid(f32 intensity, f32 dt)
{
    if (!backend_) {
        return;
    }
    AudioBackend& be = *backend_;
    be.skid_vol = f_approach_exp(be.skid_vol, f_clamp01(intensity) * 0.55f, 14.0f, dt);
    set_loop(be.skid, be.skid_vol * be.occ);
    pitch_loop(be.skid, 0.9f + 0.25f * f_clamp01(intensity));
}

bool Audio::tape_play(std::string_view path)
{
    if (!backend_) {
        return false;
    }
    AudioBackend& be = *backend_;
    tape_release(be);

    wchar_t wide[640];
    if (fs::utf8_to_wide(path, wide) == 0
        || ma_sound_init_from_file_w(&be.engine, wide, MA_SOUND_FLAG_STREAM, nullptr, nullptr,
                                     &be.tape)
               != MA_SUCCESS) {
        log_warn("audio: failed to stream %.*s", static_cast<int>(path.size()), path.data());
        return false;
    }
    ma_sound_set_looping(&be.tape, MA_TRUE);
    sound_spatial(&be.tape, 1.5f, 40.0f);
    sound_place(&be.tape, be.dash_pos, be.car_vel);
    if (be.tape_chain) {
        ma_node_attach_output_bus(&be.tape, 0, &be.tape_hpf, 0);
    }
    ma_sound_set_volume(&be.tape, 0.0f);
    ma_sound_start(&be.tape);
    be.tape_active = true;
    be.tape_speed_sm = 0.4f;
    return true;
}

void Audio::tape_stop()
{
    if (backend_) {
        tape_release(*backend_);
    }
}

bool Audio::tape_playing() const
{
    return backend_ && backend_->tape_active;
}

void Audio::set_tape(f32 speed, f32 condition, f32 volume, f32 dt)
{
    if (!backend_ || !backend_->tape_active) {
        return;
    }
    AudioBackend& be = *backend_;
    be.tape_wobble_t += dt;
    if (be.tape_wobble_t > 1000.0f) {
        be.tape_wobble_t -= 1000.0f;
    }

    const f32 wear = 1.0f - f_clamp01(condition);
    const f32 wow = std::sin(be.tape_wobble_t * kTau * 0.6f) * (0.003f + 0.032f * wear);
    const f32 flutter = std::sin(be.tape_wobble_t * kTau * 6.3f
                                 + std::sin(be.tape_wobble_t * 17.0f))
                        * 0.007f * wear;
    be.tape_speed_sm = f_approach_exp(be.tape_speed_sm, f_clamp(speed, 0.0f, 1.2f), 5.0f, dt);
    ma_sound_set_pitch(&be.tape, f_clamp(be.tape_speed_sm * (1.0f + wow + flutter), 0.05f, 1.6f));

    const f32 am = 1.0f - wear * 0.35f * (0.5f + 0.5f * std::sin(be.tape_wobble_t * kTau * 1.4f));
    const f32 drag = 0.30f + 0.70f * f_clamp01((be.tape_speed_sm - 0.30f) / 0.70f);
    ma_sound_set_volume(&be.tape, f_clamp(volume * am * drag * be.occ, 0.0f, 1.0f));

    if (be.tape_chain) {
        const f32 cutoff = 1900.0f + 2600.0f * f_clamp01(condition);
        if (f_abs(cutoff - be.tape_cutoff) > 140.0f) {
            be.tape_cutoff = cutoff;
            ma_lpf_node_config lc = ma_lpf_node_config_init(ma_engine_get_channels(&be.engine),
                                                           ma_engine_get_sample_rate(&be.engine),
                                                           static_cast<f64>(cutoff),
                                                           kTapeLpfOrder);
            ma_lpf_node_reinit(&lc.lpf, &be.tape_lpf);
        }
    }
}

} // namespace anom
