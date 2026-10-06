#include "audio/audio.h"

#include "core/arena.h"
#include "core/config.h"
#include "core/log.h"
#include "engine/assets/asset_path.h"
#include "engine/audio/audio_engine.h"
#include "game/audio/game_audio.h"
#include "math/glm_bridge.h"
#include "platform/filesystem.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

namespace anom {

namespace {

namespace engine = ghost::engine;

constexpr f32 kTapeLow = 210.0f;
constexpr f32 kSpeedOfSound = 343.0f;
constexpr f32 kDopplerMin = 0.5f;
constexpr f32 kDopplerMax = 2.0f;
constexpr int kCarGroup = static_cast<int>(ghost::game::SoundGroup::World);

constexpr const char* kSfxFiles[SFX_KIND_COUNT] = {
    "door_open.wav", "door_close.wav", "hood.wav",         "ratchet.wav", "impact.wav",
    "thump.wav",     "flap.wav",       "engine_start.wav", "whir.wav",    "wiper.wav",
};

engine::SoundRef load_sound(const std::string& file)
{
    const std::optional<std::string> bytes = engine::readAsset(engine::assetPath("audio/" + file));
    std::optional<engine::SoundBuffer> buffer = bytes ? engine::decodeSound(*bytes) : std::nullopt;
    if (!buffer) {
        log_warn("audio: failed to load %s", file.c_str());
        return nullptr;
    }
    return std::make_shared<const engine::SoundBuffer>(std::move(*buffer));
}

}

struct AudioBackend {
    struct Loop {
        engine::SoundRef sound;
        engine::VoiceId voice = 0;
        f32 volume = 0.0f;
        f32 pitch = 1.0f;
        bool positional = true;
        Vec3 pos{};
    };

    engine::AudioEngine* engine = nullptr;
    engine::SoundRef sfx[SFX_KIND_COUNT];

    Loop layers[kEngineLayerMax];
    f32 base_rpm[kEngineLayerMax] = {};
    f32 layer_vol[kEngineLayerMax] = {};
    u32 layer_count = 0;
    f32 master = 0.0f;
    f32 rpm_pitch = 1.0f;

    Loop starter;
    Loop skid;
    Loop roll_road;
    Loop roll_grass;
    Loop roll_wet;
    Loop horn;
    Loop rain_light;
    Loop rain_heavy;
    Loop rain_roof;
    Loop tape;

    f32 horn_vol = 0.0f;
    f32 skid_vol = 0.0f;
    f32 roll_road_vol = 0.0f;
    f32 roll_grass_vol = 0.0f;
    f32 roll_wet_vol = 0.0f;
    f32 rain_light_vol = 0.0f;
    f32 rain_heavy_vol = 0.0f;
    f32 rain_roof_vol = 0.0f;

    Vec3 listener{};
    Vec3 listener_vel{};
    Vec3 dash_pos{};
    Vec3 car_vel{};
    f32 occ = 1.0f;

    bool tape_active = false;
    f32 tape_speed_sm = 1.0f;
    f32 tape_wobble_t = 0.0f;

    f32 doppler(Vec3 source, Vec3 source_vel) const
    {
        const Vec3 to = source - listener;
        const f32 distance = length(to);
        if (distance < 1e-3f) {
            return 1.0f;
        }
        const Vec3 dir = to / distance;
        const f32 f = (kSpeedOfSound + dot(listener_vel, dir)) / f_max(kSpeedOfSound + dot(source_vel, dir), 1.0f);
        return f_clamp(f, kDopplerMin, kDopplerMax);
    }

    void start(Loop& loop, engine::SoundRef sound, bool positional)
    {
        loop.sound = std::move(sound);
        loop.positional = positional;
        if (!loop.sound) {
            return;
        }
        engine::PlayParams params;
        params.volume = 0.0f;
        params.loop = true;
        params.group = kCarGroup;
        params.minDistance = 3.0f;
        params.maxDistance = 120.0f;
        if (positional) {
            params.position = glm::vec3(0.0f);
        }
        loop.voice = engine->play(loop.sound, params);
    }

    void push(Loop& loop, f32 volume, f32 pitch_scale = 1.0f)
    {
        loop.volume = volume;
        if (!loop.voice) {
            return;
        }
        const f32 shift = loop.positional ? doppler(loop.pos, car_vel) : 1.0f;
        const std::optional<glm::vec3> at = loop.positional ? std::optional<glm::vec3>(to_glm(loop.pos)) : std::nullopt;
        engine->set(loop.voice, volume, loop.pitch * pitch_scale * shift, at);
    }

    void release(Loop& loop)
    {
        if (loop.voice) {
            engine->stop(loop.voice);
            loop.voice = 0;
        }
    }
};

namespace {

void load_engine_set(AudioBackend& be)
{
    Arena arena(megabytes(1));
    const fs::FileData file = fs::read_entire_file(arena, engine::assetPath("audio/engine_set.cfg").string());
    if (!file.valid()) {
        return;
    }
    Config cfg;
    if (!cfg.parse(arena, file.text())) {
        return;
    }
    const i32 declared = cfg.get_i32("engine_set.count", 0);
    const u32 count = declared <= 0 ? 0u : std::min(static_cast<u32>(declared), kEngineLayerMax);

    struct Layer {
        f32 rpm;
        std::string file;
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
        layers[slot] = {rpm, std::string(name)};
        layer_count++;
    }
    for (u32 i = 0; i < layer_count; i++) {
        engine::SoundRef sound = load_sound(layers[i].file);
        if (!sound) {
            continue;
        }
        be.base_rpm[be.layer_count] = layers[i].rpm;
        be.start(be.layers[be.layer_count], std::move(sound), true);
        be.layer_count++;
    }
}

}

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

Audio::Audio() = default;

Audio::~Audio()
{
    shutdown();
}

bool Audio::init(ghost::engine::AudioEngine& audio_engine)
{
    if (backend_) {
        return true;
    }
    backend_ = std::make_unique<AudioBackend>();
    AudioBackend& be = *backend_;
    be.engine = &audio_engine;
    for (u32 k = 0; k < SFX_KIND_COUNT; k++) {
        be.sfx[k] = load_sound(kSfxFiles[k]);
    }
    be.start(be.starter, load_sound("starter.wav"), true);
    be.start(be.skid, load_sound("skid.wav"), true);
    be.start(be.roll_road, load_sound("roll_road.wav"), true);
    be.start(be.roll_grass, load_sound("roll_grass.wav"), true);
    be.start(be.horn, load_sound("horn.wav"), true);
    be.start(be.roll_wet, load_sound("roll_road_wet.wav"), true);
    be.start(be.rain_light, load_sound("rain_light.wav"), false);
    be.start(be.rain_heavy, load_sound("rain_heavy.wav"), false);
    be.start(be.rain_roof, load_sound("rain_roof.wav"), false);
    load_engine_set(be);
    log_info("audio: car audio on the shared engine (%u engine layers)", be.layer_count);
    return true;
}

void Audio::shutdown()
{
    if (!backend_) {
        return;
    }
    AudioBackend& be = *backend_;
    for (u32 i = 0; i < be.layer_count; i++) {
        be.release(be.layers[i]);
    }
    AudioBackend::Loop* loops[] = {&be.starter,   &be.skid,       &be.roll_road,  &be.roll_grass, &be.roll_wet,
                                   &be.horn,      &be.rain_light, &be.rain_heavy, &be.rain_roof,  &be.tape};
    for (AudioBackend::Loop* loop : loops) {
        be.release(*loop);
    }
    backend_.reset();
}

void Audio::set_listener(Vec3 pos, Vec3 forward, Vec3 up, Vec3 vel)
{
    (void)forward;
    (void)up;
    if (!backend_) {
        return;
    }
    backend_->listener = pos;
    backend_->listener_vel = vel;
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
    for (u32 i = 0; i < be.layer_count; i++) {
        be.layers[i].pos = engine_pos;
    }
    be.starter.pos = engine_pos;
    be.horn.pos = engine_pos;
    be.skid.pos = center_pos;
    be.roll_road.pos = center_pos;
    be.roll_grass.pos = center_pos;
    be.roll_wet.pos = center_pos;
    be.tape.pos = dash_pos;
}

bool Audio::sfx_available(SfxKind kind) const
{
    return backend_ && backend_->sfx[kind] != nullptr;
}

void Audio::play(SfxKind kind, f32 volume, f32 pitch)
{
    if (!sfx_available(kind)) {
        return;
    }
    engine::PlayParams params;
    params.volume = volume;
    params.pitch = f_max(pitch, 0.02f);
    params.group = kCarGroup;
    backend_->engine->play(backend_->sfx[kind], params);
}

void Audio::play_at(SfxKind kind, f32 volume, f32 pitch, Vec3 pos)
{
    if (!sfx_available(kind)) {
        return;
    }
    engine::PlayParams params;
    params.volume = volume * backend_->occ;
    params.pitch = f_max(pitch, 0.02f);
    params.position = to_glm(pos);
    params.minDistance = 2.0f;
    params.maxDistance = 90.0f;
    params.group = kCarGroup;
    backend_->engine->play(backend_->sfx[kind], params);
}

void Audio::set_engine(f32 rpm, f32 load, bool running, bool cranking, f32 dt)
{
    if (!backend_) {
        return;
    }
    AudioBackend& be = *backend_;
    const f32 master_target = running ? 0.30f + 0.45f * f_clamp01(load) : (cranking ? 0.10f : 0.0f);
    be.master = f_approach_exp(be.master, master_target, 10.0f, dt);
    f32 target[kEngineLayerMax] = {};
    const f32 rpm_eff = f_max(rpm, 100.0f);
    if (be.master > 0.001f) {
        engine_crossfade(std::span<const f32>(be.base_rpm, be.layer_count), rpm_eff, target);
    }
    for (u32 i = 0; i < be.layer_count; i++) {
        be.layer_vol[i] = f_approach_exp(be.layer_vol[i], target[i] * be.master, 18.0f, dt);
        if (be.layer_vol[i] > 0.002f) {
            be.layers[i].pitch = f_clamp(rpm_eff / be.base_rpm[i], 0.45f, 2.3f);
        }
        be.push(be.layers[i], be.layer_vol[i] * be.occ);
    }
    be.push(be.starter, cranking ? 0.5f * be.occ : 0.0f);
}

u32 Audio::engine_layer_count() const
{
    return backend_ ? backend_->layer_count : 0;
}

f32 Audio::engine_layer_volume(u32 index) const
{
    return backend_ && index < backend_->layer_count ? backend_->layer_vol[index] : 0.0f;
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
    const f32 pitch = 0.8f + f_clamp01(speed / 35.0f) * 0.5f;
    be.roll_road.pitch = pitch;
    be.roll_wet.pitch = pitch;
    be.roll_grass.pitch = pitch;
    be.push(be.roll_road, be.roll_road_vol * be.occ);
    be.push(be.roll_wet, be.roll_wet_vol * be.occ);
    be.push(be.roll_grass, be.roll_grass_vol * be.occ);
}

void Audio::set_rain(f32 exterior, f32 roof, f32 dt)
{
    if (!backend_) {
        return;
    }
    AudioBackend& be = *backend_;
    const f32 ext = f_clamp01(exterior);
    const f32 light_target = f_clamp01(ext * 2.2f) * 0.34f * (1.0f - f_clamp01((ext - 0.5f) * 1.4f));
    const f32 heavy_target = f_clamp01((ext - 0.30f) / 0.55f) * 0.48f;
    const f32 roof_target = f_clamp01(roof) * 0.50f;
    be.rain_light_vol = f_approach_exp(be.rain_light_vol, light_target, 2.5f, dt);
    be.rain_heavy_vol = f_approach_exp(be.rain_heavy_vol, heavy_target, 2.5f, dt);
    be.rain_roof_vol = f_approach_exp(be.rain_roof_vol, roof_target, 4.0f, dt);
    be.push(be.rain_light, be.rain_light_vol);
    be.push(be.rain_heavy, be.rain_heavy_vol);
    be.push(be.rain_roof, be.rain_roof_vol);
}

void Audio::set_horn(bool on, f32 dt)
{
    if (!backend_) {
        return;
    }
    AudioBackend& be = *backend_;
    be.horn_vol = f_approach_exp(be.horn_vol, on ? 0.5f : 0.0f, 40.0f, dt);
    be.push(be.horn, be.horn_vol * be.occ);
}

void Audio::set_skid(f32 intensity, f32 dt)
{
    if (!backend_) {
        return;
    }
    AudioBackend& be = *backend_;
    be.skid_vol = f_approach_exp(be.skid_vol, f_clamp01(intensity) * 0.55f, 14.0f, dt);
    be.skid.pitch = 0.9f + 0.25f * f_clamp01(intensity);
    be.push(be.skid, be.skid_vol * be.occ);
}

bool Audio::tape_play(std::string_view path)
{
    if (!backend_) {
        return false;
    }
    AudioBackend& be = *backend_;
    be.release(be.tape);
    be.tape_active = false;
    std::optional<engine::SoundBuffer> buffer = engine::loadSoundFile(std::filesystem::path(std::u8string(path.begin(), path.end())));
    if (!buffer) {
        log_warn("audio: failed to load tape %.*s", static_cast<int>(path.size()), path.data());
        return false;
    }
    be.tape.sound = std::make_shared<const engine::SoundBuffer>(std::move(*buffer));
    be.tape.positional = true;
    be.tape.pos = be.dash_pos;
    engine::PlayParams params;
    params.volume = 0.0f;
    params.loop = true;
    params.group = kCarGroup;
    params.position = to_glm(be.dash_pos);
    params.minDistance = 1.5f;
    params.maxDistance = 40.0f;
    params.highpass = kTapeLow;
    params.lowpass = 4500.0f;
    be.tape.voice = be.engine->play(be.tape.sound, params);
    be.tape_active = be.tape.voice != 0;
    be.tape_speed_sm = 0.4f;
    return be.tape_active;
}

void Audio::tape_stop()
{
    if (backend_) {
        backend_->release(backend_->tape);
        backend_->tape_active = false;
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
    const f32 flutter = std::sin(be.tape_wobble_t * kTau * 6.3f + std::sin(be.tape_wobble_t * 17.0f)) * 0.007f * wear;
    be.tape_speed_sm = f_approach_exp(be.tape_speed_sm, f_clamp(speed, 0.0f, 1.2f), 5.0f, dt);
    be.tape.pitch = f_clamp(be.tape_speed_sm * (1.0f + wow + flutter), 0.05f, 1.6f);
    const f32 am = 1.0f - wear * 0.35f * (0.5f + 0.5f * std::sin(be.tape_wobble_t * kTau * 1.4f));
    const f32 drag = 0.30f + 0.70f * f_clamp01((be.tape_speed_sm - 0.30f) / 0.70f);
    be.push(be.tape, f_clamp(volume * am * drag * be.occ, 0.0f, 1.0f));
    be.engine->filter(be.tape.voice, 1900.0f + 2600.0f * f_clamp01(condition), kTapeLow);
}

}
