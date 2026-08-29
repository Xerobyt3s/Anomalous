#include "test.h"

#include "audio/audio.h"
#include "audio/tapes.h"
#include "core/arena.h"

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

    audio.set_listener(Vec3{}, Vec3{0.0f, 0.0f, -1.0f}, Vec3{});
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

TEST(audio, a_device_backed_system_loads_its_engine_layers)
{
    Arena arena(megabytes(4));
    Audio audio;
    if (!audio.init(arena)) {
        return;
    }

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
    Arena arena(megabytes(4));
    Audio audio;
    if (!audio.init(arena)) {
        return;
    }

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
    Arena arena(megabytes(4));
    Audio audio;
    if (!audio.init(arena)) {
        return;
    }

    CHECK(!audio.tape_play("assets/audio/definitely_missing.wav"));
    CHECK(!audio.tape_playing());
    audio.set_tape(1.0f, 1.0f, 1.0f, kDt);
    audio.tape_stop();
    CHECK(!audio.tape_playing());
    audio.shutdown();
}

TEST(audio, a_streamed_tape_plays_and_stops)
{
    TapeLibrary tapes;
    tapes.init();
    if (tapes.count() == 0) {
        return;
    }

    Arena arena(megabytes(4));
    Audio audio;
    if (!audio.init(arena)) {
        return;
    }

    CHECK(audio.tape_play(tapes.path(0)));
    CHECK(audio.tape_playing());
    for (i32 i = 0; i < 60; i++) {
        audio.set_tape(1.0f, 0.4f, 0.8f, kDt);
    }
    audio.tape_stop();
    CHECK(!audio.tape_playing());
    audio.shutdown();
}
