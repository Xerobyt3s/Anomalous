#include "engine/assets/asset_path.h"
#include "engine/audio/audio_engine.h"
#include "game/audio/sound_synth.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace ghost;
using engine::AudioEngine;
using engine::Listener;
using engine::SoundBank;
using engine::SoundBuffer;

namespace {
const SoundBank& placeholders() {
    static const SoundBank bank = [] {
        SoundBank b;
        game::buildPlaceholderSounds(b);
        return b;
    }();
    return bank;
}

engine::SoundRef constant(float value, std::size_t frames) {
    return std::make_shared<const SoundBuffer>(SoundBuffer{std::vector<float>(frames, value)});
}

float peak(const std::vector<float>& samples) {
    float loudest = 0.0f;
    for (const float s : samples) {
        loudest = std::max(loudest, std::abs(s));
    }
    return loudest;
}

}

TEST_CASE("Every placeholder sound is sane: it has length, is audible, never clips, and holds no bad values") {
    const std::vector<std::string> names = placeholders().names();
    CHECK(names.size() >= 45);
    for (const std::string& name : names) {
        for (const engine::SoundRef& sound : placeholders().variations(name)) {
            INFO(std::string(name));
            CHECK(sound->seconds() > 0.02f);
            CHECK(sound->seconds() < 6.0f);
            bool finite = true;
            double energy = 0.0;
            for (const float s : sound->samples) {
                finite = finite && std::isfinite(s);
                energy += static_cast<double>(s) * static_cast<double>(s);
            }
            CHECK(finite);
            CHECK(peak(sound->samples) <= 1.0f);
            CHECK(peak(sound->samples) > 0.1f);
            CHECK(std::sqrt(energy / static_cast<double>(sound->samples.size())) > 0.002);
        }
    }
}

TEST_CASE("The sounds the game asks for by name all exist") {
    for (const char* name :
         {"revolver.shot", "revolver.discharge", "revolver.dry", "revolver.cock", "revolver.open", "revolver.close",
          "revolver.turn", "revolver.eject", "revolver.load", "revolver.speedload", "revolver.holster", "revolver.draw", "casing.drop", "impact.ground",
          "impact.concrete", "impact.wood", "impact.steel", "impact.ricochet", "fire.loop", "fire.catch", "inferno.loop",
          "inferno.start", "wind.gust", "lightning.crack", "thunder.build", "thunder.strike", "haze.shot", "haze.fade",
          "ghostfire.breath", "fog.burst", "fog.electrify", "steam.explosion", "firelight.pulse", "blink", "tailwind.loop",
          "shroud.loop", "voodoo.hit", "wisp.loop", "wisp.burst", "wisp.dodge", "poltergeist.lift", "poltergeist.throw",
          "ghost.hurt", "ghost.death", "ghost.reveal", "step", "land", "slide.loop", "player.hurt", "heartbeat.loop", "pickup",
          "material.drop", "bench.dose", "bench.grind.loop", "bench.pour", "bench.fizzle", "bench.craft"}) {
        INFO(name);
        CHECK(placeholders().has(name));
    }
    CHECK(placeholders().variations("revolver.shot").size() == 3);
}

TEST_CASE("One-shots end without a click and loops join without a seam") {
    for (const std::string& name : placeholders().names()) {
        const bool loop = name.size() > 5 && name.substr(name.size() - 5) == ".loop";
        for (const engine::SoundRef& sound : placeholders().variations(name)) {
            INFO(std::string(name));
            const std::vector<float>& s = sound->samples;
            if (loop) {
                float largest = 0.0f;
                for (std::size_t i = 1; i < s.size(); ++i) {
                    largest = std::max(largest, std::abs(s[i] - s[i - 1]));
                }
                CHECK(std::abs(s.front() - s.back()) <= largest * 1.5f + 1e-3f);
            } else {
                CHECK(std::abs(s.back()) < 0.02f);
            }
        }
    }
}

TEST_CASE("A sound is louder near than far, silent past its range, and sits on the side it comes from") {
    const Listener listener{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    const auto near = engine::spatialGain(listener, {0.0f, 0.0f, -2.0f}, 2.0f, 60.0f);
    const auto far = engine::spatialGain(listener, {0.0f, 0.0f, -20.0f}, 2.0f, 60.0f);
    const auto gone = engine::spatialGain(listener, {0.0f, 0.0f, -70.0f}, 2.0f, 60.0f);
    CHECK(near.left == doctest::Approx(1.0f).epsilon(0.02));
    CHECK(near.left == doctest::Approx(near.right));
    CHECK(far.left == doctest::Approx(0.1f).epsilon(0.05));
    CHECK(gone.left == 0.0f);
    CHECK(gone.right == 0.0f);

    const auto right = engine::spatialGain(listener, {10.0f, 0.0f, 0.0f}, 2.0f, 60.0f);
    const auto left = engine::spatialGain(listener, {-10.0f, 0.0f, 0.0f}, 2.0f, 60.0f);
    CHECK(right.right > right.left * 3.0f);
    CHECK(left.left > left.right * 3.0f);
    CHECK(right.left > 0.0f);

    CHECK(right.left * right.left + right.right * right.right ==
          doctest::Approx(2.0f * 0.2f * 0.2f).epsilon(0.02));
}

TEST_CASE("The mixer runs with no device: voices play, loop, fade out and finish") {
    AudioEngine audio;
    audio.setMasterVolume(1.0f);
    std::vector<float> out(2 * 480);

    audio.mix(out.data(), 480);
    CHECK(peak(out) == 0.0f);

    const engine::VoiceId shot = audio.play(constant(0.5f, 600), {.volume = 0.5f});
    REQUIRE(shot != 0);
    audio.mix(out.data(), 480);
    CHECK(out[0] == doctest::Approx(0.25f));
    CHECK(out[1] == doctest::Approx(0.25f));
    CHECK(audio.playing(shot));
    audio.mix(out.data(), 480);
    CHECK(out[2 * 100] == doctest::Approx(0.25f));
    CHECK(out[2 * 200] == 0.0f);
    CHECK_FALSE(audio.playing(shot));

    const engine::VoiceId fast = audio.play(constant(0.5f, 600), {.pitch = 2.0f});
    audio.mix(out.data(), 480);
    CHECK_FALSE(audio.playing(fast));

    const engine::VoiceId loop = audio.play(constant(0.5f, 100), {.loop = true});
    for (int i = 0; i < 10; ++i) {
        audio.mix(out.data(), 480);
    }
    CHECK(audio.playing(loop));
    CHECK(out[2 * 479] == doctest::Approx(0.5f));
    audio.stop(loop, 0.005f);
    audio.mix(out.data(), 480);
    CHECK(out[0] > out[2 * 100]);
    CHECK(out[2 * 100] > 0.0f);
    CHECK(out[2 * 479] == 0.0f);
    CHECK_FALSE(audio.playing(loop));
    CHECK(audio.voiceCount() == 0);
}

TEST_CASE("A delayed sound starts late, group volume scales it, and loud mixes are held under the ceiling") {
    AudioEngine audio;
    audio.setMasterVolume(1.0f);
    std::vector<float> out(2 * 480);

    audio.play(constant(0.5f, 1000), {.delay = 0.005f});
    audio.mix(out.data(), 480);
    CHECK(out[2 * 200] == 0.0f);
    CHECK(out[2 * 300] == doctest::Approx(0.5f));

    AudioEngine grouped;
    grouped.setMasterVolume(1.0f);
    grouped.setGroupVolume(2, 0.5f);
    grouped.play(constant(0.4f, 1000), {.group = 2});
    grouped.mix(out.data(), 480);
    CHECK(out[0] == doctest::Approx(0.2f));

    AudioEngine loud;
    loud.setMasterVolume(1.0f);
    for (int i = 0; i < 8; ++i) {
        loud.play(constant(0.9f, 1000));
    }
    loud.mix(out.data(), 480);
    CHECK(peak(out) <= 1.0f);
    CHECK(peak(out) > 0.9f);
}

TEST_CASE("A positional voice follows the listener: it moves from one ear to the other as they turn") {
    AudioEngine audio;
    audio.setMasterVolume(1.0f);
    std::vector<float> out(2 * 480);
    audio.setListener({{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    audio.play(constant(0.5f, 48000), {.position = glm::vec3(8.0f, 0.0f, 0.0f)});
    audio.mix(out.data(), 480);
    CHECK(out[1] > out[0] * 3.0f);

    audio.setListener({{0.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}});
    audio.mix(out.data(), 480);
    CHECK(out[1] > out[0]);
    CHECK(out[2 * 479] > out[2 * 479 + 1] * 2.5f);
}

TEST_CASE("The bank picks among a sound's variations and reports what it lacks") {
    SoundBank bank;
    CHECK_FALSE(bank.has("thing"));
    std::minstd_rand rng(1);
    CHECK(bank.pick("thing", rng) == nullptr);
    bank.add("thing", SoundBuffer{{0.1f}});
    bank.add("thing", SoundBuffer{{0.2f}});
    CHECK(bank.variations("thing").size() == 2);
    bool first = false;
    bool second = false;
    for (int i = 0; i < 40; ++i) {
        const float value = bank.pick("thing", rng)->samples[0];
        first = first || value == 0.1f;
        second = second || value == 0.2f;
    }
    CHECK(first);
    CHECK(second);
    CHECK(bank.loadOverrides("no/such/directory") == 0);
}

namespace {
double bandShare(const std::vector<float>& samples, std::size_t size, float fromHz, float toHz) {
    const std::vector<float> s = game::synth::spectrum(samples, 0, size);
    double total = 0.0;
    double band = 0.0;
    for (std::size_t k = 1; k < s.size(); ++k) {
        const double power = static_cast<double>(s[k]) * static_cast<double>(s[k]);
        const float hz = static_cast<float>(k) * static_cast<float>(engine::kAudioSampleRate) / static_cast<float>(size);
        total += power;
        band += hz >= fromHz && hz < toHz ? power : 0.0;
    }
    return band / total;
}

double rms(const std::vector<float>& samples, float fromSeconds, float toSeconds) {
    const auto from = static_cast<std::size_t>(fromSeconds * static_cast<float>(engine::kAudioSampleRate));
    const auto to = std::min(samples.size(), static_cast<std::size_t>(toSeconds * static_cast<float>(engine::kAudioSampleRate)));
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        sum += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
    }
    return to > from ? std::sqrt(sum / static_cast<double>(to - from)) : 0.0;
}

}

TEST_CASE("A gunshot is a blast, not a hiss: an instant rise, weight and crack together, and a tail that differs per ear") {
    for (const engine::SoundRef& shot : placeholders().variations("revolver.shot")) {
        REQUIRE(shot->stereo());
        REQUIRE(shot->right.size() == shot->samples.size());
        const std::vector<float>& s = shot->samples;

        const float top = peak(s);
        std::size_t rising = 0;
        while (std::abs(s[rising]) < top / 3.0f) {
            ++rising;
        }
        std::size_t peakAt = rising;
        while (std::abs(s[peakAt]) < top) {
            ++peakAt;
        }
        CHECK(peakAt - rising < 48);

        CHECK(bandShare(s, 16384, 20.0f, 150.0f) > 0.05);
        CHECK(bandShare(s, 16384, 1000.0f, 6000.0f) > 0.02);
        CHECK(bandShare(s, 16384, 6000.0f, 20000.0f) > 0.0005);

        CHECK(shot->seconds() > 1.5f);
        CHECK(rms(s, 1.0f, 1.2f) > 0.0005);
        CHECK(rms(s, 1.0f, 1.2f) < 0.1 * rms(s, 0.0f, 0.1f));

        double dot = 0.0;
        double left = 0.0;
        double right = 0.0;
        for (std::size_t i = 24000; i < 48000; ++i) {
            dot += static_cast<double>(s[i]) * static_cast<double>(shot->right[i]);
            left += static_cast<double>(s[i]) * static_cast<double>(s[i]);
            right += static_cast<double>(shot->right[i]) * static_cast<double>(shot->right[i]);
        }
        CHECK(std::abs(dot) / std::sqrt(left * right) < 0.7);
    }
}

TEST_CASE("The revolver's clicks are knocks, not beeps: broadband, and over in a few hundredths of a second") {
    for (const char* name : {"revolver.cock", "revolver.dry", "revolver.close", "revolver.turn"}) {
        for (const engine::SoundRef& sound : placeholders().variations(name)) {
            INFO(std::string(name));
            const std::vector<float>& s = sound->samples;

            const std::vector<float> all = game::synth::spectrum(s, 0, 8192);
            double total = 0.0;
            double top = 0.0;
            for (std::size_t k = 1; k < all.size(); ++k) {
                const double power = static_cast<double>(all[k]) * static_cast<double>(all[k]);
                total += power;
                top = std::max(top, power);
            }
            CHECK(top / total < 0.15);
            CHECK(bandShare(s, 8192, 2000.0f, 20000.0f) > 0.1);

            CHECK(rms(s, 0.06f, sound->seconds()) < 0.35 * rms(s, 0.0f, 0.06f));
        }
    }
}

TEST_CASE("A stereo sound plays an ear each in the hands, and as one signal when placed in the world") {
    auto sound = std::make_shared<const SoundBuffer>(SoundBuffer{std::vector<float>(1000, 0.4f), std::vector<float>(1000, 0.2f)});
    std::vector<float> out(2 * 100);

    AudioEngine held;
    held.setMasterVolume(1.0f);
    held.play(sound);
    held.mix(out.data(), 100);
    CHECK(out[0] == doctest::Approx(0.4f));
    CHECK(out[1] == doctest::Approx(0.2f));

    AudioEngine placed;
    placed.setMasterVolume(1.0f);
    placed.setListener({{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    placed.play(sound, {.position = glm::vec3(0.0f, 0.0f, -1.0f), .minDistance = 2.0f});
    placed.mix(out.data(), 100);
    CHECK(out[0] == doctest::Approx(0.3f));
    CHECK(out[1] == doctest::Approx(0.3f));
}

TEST_CASE("The recorded sounds that ship with the game are read through the asset system and replace their placeholders") {
    const auto bytes = engine::readAsset(engine::assetPath("audio/revolver.shot.1.flac"));
    REQUIRE(bytes.has_value());
    const auto shot = engine::decodeSound(*bytes);
    REQUIRE(shot.has_value());
    CHECK(shot->stereo());
    CHECK(shot->seconds() > 0.5f);
    CHECK_FALSE(engine::decodeSound("not a sound file").has_value());

    engine::SoundBank bank;
    game::buildPlaceholderSounds(bank);
    CHECK(bank.loadAssets(engine::assetPath("audio")) >= 6);
    CHECK(bank.variations("revolver.cock").size() == 2);
    CHECK(bank.variations("revolver.shot").size() == 2);
    CHECK(bank.variations("step").size() == 6);
    for (const engine::SoundRef& step : bank.variations("step")) {
        CHECK_FALSE(step->stereo());
        float peak = 0.0f;
        for (const float s : step->samples) {
            peak = std::max(peak, std::abs(s));
        }
        CHECK(peak > 0.06f);
        CHECK(peak < 0.16f);
    }
}
