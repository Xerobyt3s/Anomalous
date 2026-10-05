#pragma once

#include "engine/audio/sound_bank.h"

#include <cstdint>
#include <vector>

namespace ghost::game {
namespace synth {
using Samples = std::vector<float>;

enum class Filter { LowPass, HighPass, BandPass };

Samples noise(float seconds, std::uint32_t seed);

Samples tone(float seconds, float fromHz, float toHz);

Samples filter(const Samples& in, Filter kind, float fromHz, float toHz, float q = 0.7f);

Samples envelope(Samples in, float attack, float decay);

Samples fadeOut(Samples in, float seconds);
Samples gain(Samples in, float amount);

Samples drive(Samples in, float amount);

Samples normalize(Samples in, float peak);

void mixInto(Samples& dst, const Samples& src, float at = 0.0f, float amount = 1.0f);

Samples wobble(float seconds, float rateHz, std::uint32_t seed);
Samples multiply(Samples a, const Samples& b);

Samples loopable(const Samples& in, float crossfadeSeconds);

Samples click(std::uint32_t seed, float brightHz, float ringHz, float ringDecay, float length = 0.12f);

Samples friedlander(float positive);

struct Strike {
    float lowHz = 1500.0f;
    float highHz = 9000.0f;
    int modes = 28;
    float decayLow = 0.010f;
    float decayHigh = 0.002f;
    float contact = 0.00008f;
    float tick = 0.5f;
    float thumpHz = 0.0f;
    float thump = 0.6f;
};
Samples struck(std::uint32_t seed, const Strike& strike);

struct Stereo {
    Samples left;
    Samples right;
};

Stereo reverb(const Samples& in, float tail, float rt60, float size, float damping, std::uint32_t seed);

void compress(Stereo& sound, float threshold, float ratio, float release);

std::vector<float> spectrum(const Samples& in, std::size_t offset, std::size_t size);

}

void buildPlaceholderSounds(engine::SoundBank& bank);

}
