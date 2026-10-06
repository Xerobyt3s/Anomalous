#include "test.h"

#include "audio/audio.h"
#include "audio/tapes.h"
#include "core/arena.h"
#include "engine/audio/audio_engine.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace anom;

namespace {

constexpr f32 kDt = 1.0f / 60.0f;

f32 weight_sum_sq(std::span<const f32> w)
{
    f32 total = 0.0f;
    for (f32 v : w) {
        total += v * v;
    }
    return total;
}

} // namespace

TEST(audio, skid_intensity_counts_longitudinal_slide)
{
    CHECK(skid_intensity(0.0f, 0.0f) == 0.0f);
    CHECK(skid_intensity(1.0f, 1.5f) == 0.0f);
    CHECK_NEAR(skid_intensity(7.5f, 0.0f), 1.0f, 1e-5);
    CHECK_NEAR(skid_intensity(0.0f, 9.0f), 1.0f, 1e-5);
    CHECK(skid_intensity(0.0f, 5.0f) > 0.3f);
    CHECK(skid_intensity(0.0f, 5.0f) < 0.6f);
    CHECK_NEAR(skid_intensity(-4.5f, 0.0f), skid_intensity(4.5f, 0.0f), 1e-6);
}

TEST(audio, engine_lowpass_opens_with_load)
{
    CHECK_NEAR(engine_load_lowpass(0.0f), 1500.0f, 1e-3);
    CHECK(engine_load_lowpass(0.5f) > 1500.0f);
    CHECK(engine_load_lowpass(0.5f) < 9000.0f);
    CHECK(engine_load_lowpass(1.0f) == 0.0f);
}

TEST(audio, the_cabin_lowpass_is_off_outside_the_car)
{
    CHECK(cabin_lowpass(1.0f) == 0.0f);
    CHECK_NEAR(cabin_lowpass(0.55f), 1300.0f, 1e-3);
    CHECK(cabin_lowpass(0.77f) > 1300.0f);
    CHECK(cabin_lowpass(0.77f) < 6000.0f);
}

TEST(audio, wind_is_silent_at_walking_pace_and_grows_with_speed)
{
    CHECK(wind_volume(2.0f, false) == 0.0f);
    CHECK(wind_volume(8.0f, false) == 0.0f);
    const f32 mid = wind_volume(25.0f, false);
    const f32 fast = wind_volume(40.0f, false);
    CHECK(mid > 0.0f);
    CHECK(fast > mid);
    CHECK_NEAR(wind_volume(40.0f, true), fast * 0.5f, 1e-6);
    CHECK(wind_lowpass(0.0f) < wind_lowpass(40.0f));
}

TEST(audio, a_noise_loop_has_a_flat_rms)
{
    std::vector<f32> samples(9600);
    fill_noise_loop(samples, 7u);
    f32 sum_sq = 0.0f;
    f32 peak = 0.0f;
    for (f32 s : samples) {
        sum_sq += s * s;
        peak = std::fmax(peak, std::fabs(s));
    }
    const f32 rms = std::sqrt(sum_sq / static_cast<f32>(samples.size()));
    CHECK(rms > 0.1f);
    CHECK(rms < 0.3f);
    CHECK(peak <= 1.0f);
    CHECK(samples.front() == 0.0f);
    CHECK(std::fabs(samples.back()) < 1e-3f);
    std::vector<f32> again(9600);
    fill_noise_loop(again, 7u);
    CHECK(again[4000] == samples[4000]);
}

TEST(audio, crossfade_pins_to_the_lowest_layer_below_its_rpm)
{
    const f32 base[] = {800.0f, 2000.0f, 4500.0f};
    f32 w[3] = {};
    engine_crossfade(base, 400.0f, w);
    CHECK_NEAR(w[0], 1.0f, 1e-6);
    CHECK_NEAR(w[1], 0.0f, 1e-6);
    CHECK_NEAR(w[2], 0.0f, 1e-6);
}

TEST(audio, crossfade_pins_to_the_highest_layer_above_its_rpm)
{
    const f32 base[] = {800.0f, 2000.0f, 4500.0f};
    f32 w[3] = {};
    engine_crossfade(base, 9000.0f, w);
    CHECK_NEAR(w[2], 1.0f, 1e-6);
    CHECK_NEAR(w[0], 0.0f, 1e-6);
}

TEST(audio, crossfade_is_constant_power_between_layers)
{
    const f32 base[] = {800.0f, 2000.0f, 4500.0f};
    for (f32 rpm = 800.0f; rpm <= 4500.0f; rpm += 37.0f) {
        f32 w[3] = {};
        engine_crossfade(base, rpm, w);
        CHECK_NEAR(weight_sum_sq(w), 1.0f, 1e-4);
    }
}

TEST(audio, crossfade_midpoint_splits_evenly)
{
    const f32 base[] = {1000.0f, 2000.0f};
    f32 w[2] = {};
    engine_crossfade(base, 1500.0f, w);
    CHECK_NEAR(w[0], 0.70710678f, 1e-4);
    CHECK_NEAR(w[1], 0.70710678f, 1e-4);
}

TEST(audio, crossfade_survives_a_degenerate_layer_pair)
{
    const f32 base[] = {1200.0f, 1200.0f, 3000.0f};
    f32 w[3] = {};
    engine_crossfade(base, 1200.0f, w);
    CHECK_NEAR(weight_sum_sq(w), 1.0f, 1e-4);
    CHECK(w[0] == w[0]);
    CHECK(w[1] == w[1]);
    CHECK(w[2] == w[2]);
}

TEST(audio, crossfade_ignores_undersized_output)
{
    const f32 base[] = {800.0f, 2000.0f, 4500.0f};
    f32 w[2] = {0.5f, 0.5f};
    engine_crossfade(base, 1000.0f, w);
    CHECK_NEAR(w[0], 0.0f, 1e-6);
    CHECK_NEAR(w[1], 0.0f, 1e-6);
}

TEST(audio, crossfade_handles_an_empty_layer_set)
{
    f32 w[2] = {0.25f, 0.75f};
    engine_crossfade({}, 3000.0f, w);
    CHECK_NEAR(w[0], 0.0f, 1e-6);
    CHECK_NEAR(w[1], 0.0f, 1e-6);
}

TEST(audio, an_uninitialised_system_is_inert)
{
    Audio audio;
    CHECK(!audio.ok());
    CHECK(!audio.tape_playing());
    CHECK(audio.engine_layer_count() == 0);
    CHECK_NEAR(audio.occlusion(), 1.0f, 1e-6);
    CHECK(!audio.sfx_available(SFX_HOOD));
    CHECK(!audio.tape_play("assets/audio/starter.wav"));

    audio.set_listener(Vec3{}, Vec3{0.0f, 0.0f, -1.0f}, Vec3{0.0f, 1.0f, 0.0f}, Vec3{});
    audio.set_car(Vec3{}, Vec3{}, Vec3{}, Vec3{});
    audio.set_occlusion(0.2f, kDt);
    audio.play(SFX_HOOD, 1.0f, 1.0f);
    audio.play_at(SFX_THUMP, 1.0f, 1.0f, Vec3{});
    audio.set_engine(3000.0f, 0.5f, true, false, kDt);
    audio.set_rolling(20.0f, 1.0f, true, 0.0f, kDt);
    audio.set_rain(0.5f, 0.2f, kDt);
    audio.set_skid(1.0f, kDt);
    audio.set_horn(true, kDt);
    audio.set_tape(1.0f, 1.0f, 1.0f, kDt);
    audio.tape_stop();
    audio.shutdown();

    CHECK(!audio.ok());
    CHECK_NEAR(audio.occlusion(), 1.0f, 1e-6);
}

TEST(audio, the_car_loads_its_engine_layers_on_the_shared_engine)
{
    ghost::engine::AudioEngine engine;
    Audio audio;
    CHECK(audio.init(engine));

    CHECK(audio.ok());
    CHECK(audio.sfx_available(SFX_HOOD));
    CHECK(audio.engine_layer_count() >= 2);

    for (i32 i = 0; i < 240; i++) {
        audio.set_engine(2500.0f, 0.6f, true, false, kDt);
    }
    f32 loudest = 0.0f;
    for (u32 i = 0; i < audio.engine_layer_count(); i++) {
        loudest = f_max(loudest, audio.engine_layer_volume(i));
    }
    CHECK(loudest > 0.05f);

    for (i32 i = 0; i < 600; i++) {
        audio.set_engine(0.0f, 0.0f, false, false, kDt);
    }
    for (u32 i = 0; i < audio.engine_layer_count(); i++) {
        CHECK(audio.engine_layer_volume(i) < 0.01f);
    }

    audio.shutdown();
    CHECK(!audio.ok());
    audio.shutdown();
}

TEST(audio, occlusion_settles_toward_its_target)
{
    ghost::engine::AudioEngine engine;
    Audio audio;
    CHECK(audio.init(engine));

    for (i32 i = 0; i < 240; i++) {
        audio.set_occlusion(0.25f, kDt);
    }
    CHECK_NEAR(audio.occlusion(), 0.25f, 1e-3);

    for (i32 i = 0; i < 240; i++) {
        audio.set_occlusion(1.0f, kDt);
    }
    CHECK_NEAR(audio.occlusion(), 1.0f, 1e-3);
    audio.shutdown();
}

TEST(audio, a_missing_tape_leaves_the_deck_empty)
{
    ghost::engine::AudioEngine engine;
    Audio audio;
    CHECK(audio.init(engine));

    CHECK(!audio.tape_play("assets/audio/definitely_missing.wav"));
    CHECK(!audio.tape_playing());
    audio.set_tape(1.0f, 1.0f, 1.0f, kDt);
    audio.tape_stop();
    CHECK(!audio.tape_playing());
    audio.shutdown();
}

TEST(audio, the_car_mixes_into_the_shared_engine)
{
    ghost::engine::AudioEngine engine;
    Audio audio;
    CHECK(audio.init(engine));
    CHECK(engine.voiceCount() >= audio.engine_layer_count() + 9);
    audio.set_listener(Vec3{}, Vec3{0.0f, 0.0f, -1.0f}, Vec3{0.0f, 1.0f, 0.0f}, Vec3{});
    audio.set_car(Vec3{2.0f, 0.0f, 0.0f}, Vec3{2.0f, 0.0f, 0.0f}, Vec3{2.0f, 0.0f, 0.0f}, Vec3{});
    for (i32 i = 0; i < 240; i++) {
        audio.set_engine(2500.0f, 0.6f, true, false, kDt);
    }
    std::vector<f32> out(2 * 4800);
    engine.mix(out.data(), 4800);
    f32 peak = 0.0f;
    for (f32 v : out) {
        peak = f_max(peak, f_abs(v));
    }
    CHECK(peak > 0.01f);
    audio.shutdown();
    engine.mix(out.data(), 4800);
    engine.mix(out.data(), 4800);
    CHECK(engine.voiceCount() == 0);
}

TEST(audio, a_lowpass_voice_quiets_high_frequencies)
{
    auto tone = std::make_shared<ghost::engine::SoundBuffer>();
    for (i32 i = 0; i < 4800; i++) {
        tone->samples.push_back(std::sin(static_cast<f32>(i) * kTau * 8000.0f / 48000.0f));
    }
    const auto loudness = [&](f32 lowpass) {
        ghost::engine::AudioEngine engine;
        ghost::engine::PlayParams params;
        params.lowpass = lowpass;
        engine.play(tone, params);
        std::vector<f32> out(2 * 4800);
        engine.mix(out.data(), 4800);
        f32 sum = 0.0f;
        for (size_t i = 2400; i < out.size(); i++) {
            sum += out[i] * out[i];
        }
        return sum;
    };
    CHECK(loudness(500.0f) < loudness(0.0f) * 0.05f);
}

TEST(audio, a_streamed_tape_plays_and_stops)
{
    TapeLibrary tapes;
    tapes.init();
    if (tapes.count() == 0) {
        return;
    }

    ghost::engine::AudioEngine engine;
    Audio audio;
    CHECK(audio.init(engine));

    CHECK(audio.tape_play(tapes.path(0)));
    CHECK(audio.tape_playing());
    for (i32 i = 0; i < 60; i++) {
        audio.set_tape(1.0f, 0.4f, 0.8f, kDt);
    }
    audio.tape_stop();
    CHECK(!audio.tape_playing());
    audio.shutdown();
}
