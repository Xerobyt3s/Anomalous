#include "game/audio/sound_synth.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace ghost::game {
namespace synth {
namespace {
constexpr float kRate = static_cast<float>(engine::kAudioSampleRate);
constexpr float kPi = 3.14159265358979f;

std::size_t frames(float seconds) { return static_cast<std::size_t>(std::max(seconds, 0.0f) * kRate); }

struct Random {
    std::uint32_t state;
    explicit Random(std::uint32_t seed) : state(seed * 2654435761u + 0x9E3779B9u) {}
    float next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
    }
    float unit() { return next() * 0.5f + 0.5f; }
};

}

Samples noise(float seconds, std::uint32_t seed) {
    Random random(seed);
    Samples out(frames(seconds));
    for (float& s : out) {
        s = random.next();
    }
    return out;
}

Samples tone(float seconds, float fromHz, float toHz) {
    Samples out(frames(seconds));
    const float n = static_cast<float>(std::max<std::size_t>(out.size(), 1));
    const float ratio = toHz / fromHz;
    double phase = 0.0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const float hz = fromHz * std::pow(ratio, static_cast<float>(i) / n);
        out[i] = static_cast<float>(std::sin(phase));
        phase += 2.0 * static_cast<double>(kPi) * static_cast<double>(hz) / static_cast<double>(kRate);
    }
    return out;
}

Samples filter(const Samples& in, Filter kind, float fromHz, float toHz, float q) {
    Samples out(in.size());
    const float n = static_cast<float>(std::max<std::size_t>(in.size(), 1));
    const float ratio = toHz / fromHz;
    const float k = 1.0f / std::max(q, 0.05f);
    float ic1 = 0.0f;
    float ic2 = 0.0f;
    for (std::size_t i = 0; i < in.size(); ++i) {
        const float hz = std::clamp(fromHz * std::pow(ratio, static_cast<float>(i) / n), 20.0f, 0.45f * kRate);
        const float g = std::tan(kPi * hz / kRate);
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = in[i] - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        switch (kind) {
        case Filter::LowPass:
            out[i] = v2;
            break;
        case Filter::BandPass:
            out[i] = v1 * k;
            break;
        case Filter::HighPass:
            out[i] = in[i] - k * v1 - v2;
            break;
        }
    }
    return out;
}

Samples envelope(Samples in, float attack, float decay) {
    const float attackFrames = std::max(attack * kRate, 1.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        const float t = static_cast<float>(i);
        float level = std::min(t / attackFrames, 1.0f);
        if (decay > 0.0f && t > attackFrames) {
            level *= std::exp(-(t - attackFrames) / (decay * kRate));
        }
        in[i] *= level;
    }
    return in;
}

Samples fadeOut(Samples in, float seconds) {
    const std::size_t count = std::min(frames(seconds), in.size());
    for (std::size_t i = 0; i < count; ++i) {
        in[in.size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(count);
    }
    return in;
}

Samples gain(Samples in, float amount) {
    for (float& s : in) {
        s *= amount;
    }
    return in;
}

Samples drive(Samples in, float amount) {
    for (float& s : in) {
        s = std::tanh(s * amount);
    }
    return in;
}

Samples normalize(Samples in, float peak) {
    float loudest = 0.0f;
    for (const float s : in) {
        loudest = std::max(loudest, std::abs(s));
    }
    if (loudest > 1e-6f) {
        const float scale = peak / loudest;
        for (float& s : in) {
            s *= scale;
        }
    }
    return in;
}

void mixInto(Samples& dst, const Samples& src, float at, float amount) {
    const std::size_t offset = frames(at);
    if (dst.size() < offset + src.size()) {
        dst.resize(offset + src.size(), 0.0f);
    }
    for (std::size_t i = 0; i < src.size(); ++i) {
        dst[offset + i] += src[i] * amount;
    }
}

Samples wobble(float seconds, float rateHz, std::uint32_t seed) {
    Random random(seed);
    Samples out(frames(seconds));
    const float step = kRate / std::max(rateHz, 0.01f);
    float from = random.unit();
    float to = random.unit();
    float t = 0.0f;
    for (float& s : out) {
        const float ease = 0.5f - 0.5f * std::cos(kPi * std::min(t / step, 1.0f));
        s = from + (to - from) * ease;
        t += 1.0f;
        if (t >= step) {
            t -= step;
            from = to;
            to = random.unit();
        }
    }
    return out;
}

Samples multiply(Samples a, const Samples& b) {
    const std::size_t count = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < count; ++i) {
        a[i] *= b[i];
    }
    a.resize(count);
    return a;
}

Samples loopable(const Samples& in, float crossfadeSeconds) {
    const std::size_t fade = std::min(frames(crossfadeSeconds), in.size() / 2);
    const std::size_t length = in.size() - fade;
    Samples out(in.begin(), in.begin() + static_cast<std::ptrdiff_t>(length));
    for (std::size_t i = 0; i < fade; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(fade);

        out[i] = in[i] * std::sqrt(t) + in[length + i] * std::sqrt(1.0f - t);
    }
    return out;
}

Samples click(std::uint32_t seed, float brightHz, float ringHz, float ringDecay, float length) {
    Samples out = envelope(filter(noise(length, seed), Filter::HighPass, brightHz, brightHz), 0.0002f, 0.003f);
    constexpr float kPartials[3] = {1.0f, 2.76f, 5.4f};
    constexpr float kLevels[3] = {0.6f, 0.35f, 0.2f};
    constexpr float kDecays[3] = {1.0f, 0.55f, 0.33f};
    for (int k = 0; k < 3; ++k) {
        const float hz = ringHz * kPartials[k];
        mixInto(out, envelope(tone(length, hz, hz), 0.0003f, ringDecay * kDecays[k]), 0.0f, kLevels[k]);
    }
    return fadeOut(std::move(out), length * 0.3f);
}

Samples friedlander(float positive) {
    const float t0 = std::max(positive, 1e-5f) * kRate;
    Samples out(static_cast<std::size_t>(t0 * 8.0f) + 4);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const float x = static_cast<float>(i) / t0;
        out[i] = (1.0f - x) * std::exp(-x);
    }

    out[0] *= 0.35f;
    out[1] *= 0.8f;
    return out;
}

Samples struck(std::uint32_t seed, const Strike& strike) {
    Random random(seed);
    const float longest = std::max(strike.decayLow, strike.decayHigh);
    const float seconds = std::max(longest * 7.0f, strike.thumpHz > 0.0f ? 0.08f : 0.02f);
    Samples out(frames(seconds), 0.0f);
    const float span = std::log(strike.highHz / strike.lowHz);
    const float cutoff = 1.0f / std::max(strike.contact, 1e-6f);
    for (int k = 0; k < strike.modes; ++k) {
        const float place = (static_cast<float>(k) + random.unit()) / static_cast<float>(strike.modes);
        const float hz = strike.lowHz * std::exp(span * place);
        const float decay = strike.decayLow * std::pow(strike.decayHigh / strike.decayLow, place) * (0.7f + 0.6f * random.unit());
        const float level = (0.35f + 0.65f * random.unit()) / (1.0f + (hz / cutoff) * (hz / cutoff));
        const float step = 2.0f * kPi * hz / kRate;
        const float fall = std::exp(-1.0f / (decay * kRate));
        float amplitude = level;
        const std::size_t count = std::min(out.size(), frames(decay * 7.0f));
        for (std::size_t i = 0; i < count; ++i) {
            out[i] += amplitude * std::sin(step * static_cast<float>(i));
            amplitude *= fall;
        }
    }
    out = gain(std::move(out), 1.0f / std::sqrt(static_cast<float>(std::max(strike.modes, 1))));
    if (strike.tick > 0.0f) {
        mixInto(out, envelope(filter(noise(0.004f, seed + 77), Filter::HighPass, 2500.0f, 2500.0f), 0.00005f, 0.0005f), 0.0f,
                strike.tick);
    }
    if (strike.thumpHz > 0.0f) {
        mixInto(out, envelope(tone(0.08f, strike.thumpHz, strike.thumpHz * 0.7f), 0.0008f, 0.012f), 0.0f, strike.thump);
    }
    return fadeOut(std::move(out), 0.003f);
}

namespace {
Samples reverbChannel(const Samples& in, float tail, float rt60, float size, float damping, std::uint32_t seed) {
    Random random(seed);
    constexpr int kLines = 8;
    constexpr float kLineMs[kLines] = {31.7f, 37.1f, 41.3f, 47.9f, 53.3f, 61.1f, 67.3f, 73.9f};
    constexpr float kSmearMs[3] = {5.1f, 7.3f, 11.7f};

    Samples x = in;
    x.resize(in.size() + frames(tail), 0.0f);
    for (const float ms : kSmearMs) {
        const std::size_t delay = std::max<std::size_t>(frames(ms * 0.001f * std::min(size, 1.5f) * (0.9f + 0.2f * random.unit())), 1);
        std::vector<float> line(delay, 0.0f);
        std::size_t at = 0;
        for (float& s : x) {
            const float delayed = line[at];
            const float fed = s + 0.6f * delayed;
            line[at] = fed;
            s = delayed - 0.6f * fed;
            at = (at + 1) % delay;
        }
    }

    std::vector<std::vector<float>> lines;
    float gains[kLines];
    float lows[kLines] = {};
    std::size_t at[kLines] = {};
    for (int k = 0; k < kLines; ++k) {
        const std::size_t delay = std::max<std::size_t>(frames(kLineMs[k] * 0.001f * size * (0.93f + 0.14f * random.unit())), 2);
        lines.emplace_back(delay, 0.0f);
        gains[k] = std::pow(10.0f, -3.0f * static_cast<float>(delay) / (std::max(rt60, 0.01f) * kRate));
    }
    Samples out(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        float read[kLines];
        float sum = 0.0f;
        for (int k = 0; k < kLines; ++k) {
            read[k] = lines[static_cast<std::size_t>(k)][at[k]];
            sum += read[k];
        }
        out[i] = (read[0] - read[1] + read[2] - read[3] + read[4] - read[5] + read[6] - read[7]) * 0.35f;
        for (int k = 0; k < kLines; ++k) {
            const float mixed = read[k] - sum * (2.0f / static_cast<float>(kLines));
            lows[k] += (mixed - lows[k]) * (1.0f - damping);
            std::vector<float>& line = lines[static_cast<std::size_t>(k)];
            line[at[k]] = x[i] + lows[k] * gains[k];
            at[k] = (at[k] + 1) % line.size();
        }
    }
    return out;
}

}

Stereo reverb(const Samples& in, float tail, float rt60, float size, float damping, std::uint32_t seed) {
    return {reverbChannel(in, tail, rt60, size, damping, seed * 2 + 1), reverbChannel(in, tail, rt60, size, damping, seed * 2 + 2)};
}

void compress(Stereo& sound, float threshold, float ratio, float release) {
    const std::size_t count = std::min(sound.left.size(), sound.right.size());
    const float attack = std::exp(-1.0f / (0.0003f * kRate));
    const float letGo = std::exp(-1.0f / (std::max(release, 0.001f) * kRate));
    float level = 0.0f;
    for (std::size_t i = 0; i < count; ++i) {
        const float in = std::max(std::abs(sound.left[i]), std::abs(sound.right[i]));
        const float coefficient = in > level ? attack : letGo;
        level = in + (level - in) * coefficient;
        if (level > threshold) {
            const float reduce = std::pow(level / threshold, 1.0f / ratio - 1.0f);
            sound.left[i] *= reduce;
            sound.right[i] *= reduce;
        }
    }
}

std::vector<float> spectrum(const Samples& in, std::size_t offset, std::size_t size) {
    std::vector<float> re(size, 0.0f);
    std::vector<float> im(size, 0.0f);
    for (std::size_t i = 0; i < size && offset + i < in.size(); ++i) {
        const float window = 0.5f - 0.5f * std::cos(2.0f * kPi * static_cast<float>(i) / static_cast<float>(size));
        re[i] = in[offset + i] * window;
    }

    for (std::size_t i = 1, j = 0; i < size; ++i) {
        std::size_t bit = size >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (std::size_t length = 2; length <= size; length <<= 1) {
        const float angle = -2.0f * kPi / static_cast<float>(length);
        for (std::size_t start = 0; start < size; start += length) {
            for (std::size_t k = 0; k < length / 2; ++k) {
                const float c = std::cos(angle * static_cast<float>(k));
                const float s = std::sin(angle * static_cast<float>(k));
                const std::size_t a = start + k;
                const std::size_t b = a + length / 2;
                const float tr = re[b] * c - im[b] * s;
                const float ti = re[b] * s + im[b] * c;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
    std::vector<float> out(size / 2);
    for (std::size_t k = 0; k < out.size(); ++k) {
        out[k] = std::sqrt(re[k] * re[k] + im[k] * im[k]);
    }
    return out;
}

}

namespace {
using namespace synth;
using F = synth::Filter;

Samples ping(float seconds, float hz, float decay) { return envelope(tone(seconds, hz, hz), 0.0005f, decay); }

Samples lfo(float seconds, float hz, float lo, float hi) {
    Samples out = tone(seconds, hz, hz);
    for (float& s : out) {
        s = lo + (hi - lo) * (0.5f + 0.5f * s);
    }
    return out;
}

Samples crackle(float seconds, float perSecond, std::uint32_t seed) {
    Samples out = noise(seconds, seed);
    const float threshold = 1.0f - 2.0f * perSecond / static_cast<float>(engine::kAudioSampleRate);
    for (float& s : out) {
        s = s > threshold ? 1.0f : 0.0f;
    }
    return filter(out, F::BandPass, 2800.0f, 2800.0f, 1.2f);
}

engine::SoundBuffer done(Samples samples, float peak, float fade = 0.02f) {
    return engine::SoundBuffer{fadeOut(normalize(std::move(samples), peak), fade)};
}

void scaleToPeak(Stereo& sound, float peak) {
    float loudest = 0.0f;
    for (const Samples* channel : {&sound.left, &sound.right}) {
        for (const float s : *channel) {
            loudest = std::max(loudest, std::abs(s));
        }
    }
    if (loudest > 1e-6f) {
        sound.left = gain(std::move(sound.left), peak / loudest);
        sound.right = gain(std::move(sound.right), peak / loudest);
    }
}

engine::SoundBuffer done(Stereo sound, float peak, float fade) {
    const std::size_t length = std::max(sound.left.size(), sound.right.size());
    sound.left.resize(length, 0.0f);
    sound.right.resize(length, 0.0f);
    scaleToPeak(sound, peak);
    return engine::SoundBuffer{fadeOut(std::move(sound.left), fade), fadeOut(std::move(sound.right), fade)};
}

engine::SoundBuffer inHand(const Samples& dry, float peak, std::uint32_t seed, float wet = 0.1f) {
    Stereo space = reverb(dry, 0.2f, 0.15f, 0.3f, 0.6f, seed);
    Stereo out{dry, dry};
    mixInto(out.left, space.left, 0.0f, wet);
    mixInto(out.right, space.right, 0.0f, wet);
    return done(std::move(out), peak, 0.05f);
}

constexpr Strike kHammer{900.0f, 8000.0f, 28, 0.010f, 0.002f, 0.0001f, 0.5f, 240.0f, 0.15f};
constexpr Strike kSear{1800.0f, 10000.0f, 30, 0.009f, 0.002f, 0.00006f, 0.6f, 320.0f, 0.15f};
constexpr Strike kCylinderLock{900.0f, 6000.0f, 24, 0.014f, 0.003f, 0.0001f, 0.4f, 180.0f, 0.25f};
constexpr Strike kTick{3000.0f, 12000.0f, 16, 0.003f, 0.001f, 0.00004f, 0.6f, 0.0f, 0.0f};
constexpr Strike kDetent{3000.0f, 12000.0f, 16, 0.003f, 0.001f, 0.00004f, 0.6f, 600.0f, 0.08f};
constexpr Strike kLatch{2000.0f, 11000.0f, 20, 0.006f, 0.0015f, 0.00005f, 0.5f, 0.0f, 0.0f};
constexpr Strike kFrame{350.0f, 6000.0f, 36, 0.025f, 0.003f, 0.00015f, 0.5f, 150.0f, 0.45f};
constexpr Strike kCraneStop{500.0f, 5000.0f, 22, 0.018f, 0.003f, 0.00015f, 0.4f, 200.0f, 0.3f};
constexpr Strike kSeat{1200.0f, 8000.0f, 20, 0.006f, 0.0015f, 0.0001f, 0.4f, 400.0f, 0.2f};

Samples brass(std::uint32_t seed, float baseHz, float decay) {
    constexpr float kRatios[6] = {1.0f, 1.47f, 2.09f, 2.56f, 3.39f, 4.12f};
    constexpr float kLevels[6] = {1.0f, 0.7f, 0.5f, 0.4f, 0.25f, 0.15f};
    constexpr float kDecays[6] = {1.0f, 0.8f, 0.6f, 0.5f, 0.4f, 0.3f};
    Samples out = envelope(filter(noise(0.003f, seed), F::HighPass, 3000.0f, 3000.0f), 0.00005f, 0.0004f);
    std::uint32_t state = seed * 747796405u + 2891336453u;
    for (int k = 0; k < 6; ++k) {
        state = state * 1664525u + 1013904223u;
        const float chance = 0.5f + 0.5f * static_cast<float>(state >> 8) / 16777216.0f;
        const float hz = baseHz * kRatios[k];
        if (hz > 20000.0f) {
            continue;
        }
        mixInto(out, envelope(tone(decay * kDecays[k] * 7.0f, hz, hz), 0.0002f, decay * kDecays[k]), 0.0f, kLevels[k] * chance);
    }
    return out;
}

Samples scrape(std::uint32_t seed, float seconds, float fromHz, float toHz, float rattleHz) {
    Samples s = multiply(filter(noise(seconds, seed), F::BandPass, fromHz, toHz, 1.8f), wobble(seconds, rattleHz, seed + 1));
    return fadeOut(envelope(std::move(s), seconds * 0.2f, seconds * 0.5f), seconds * 0.3f);
}

Stereo gunshot(std::uint32_t variation) {
    std::uint32_t state = (variation + 1) * 2654435761u;
    auto unit = [&state] {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 16777216.0f;
    };
    const std::uint32_t seed = 900 + variation * 16;
    const float fire = 0.0015f;

    Samples dry;
    mixInto(dry, struck(seed, kHammer), 0.0f, 0.08f);

    const Samples blast = friedlander(0.00025f * (0.9f + 0.2f * unit()));
    mixInto(dry, blast, fire, 1.0f);
    mixInto(dry, filter(blast, F::LowPass, 12000.0f, 12000.0f), fire + 0.0016f + 0.0005f * unit(), 0.5f);
    for (int k = 0; k < 8; ++k) {
        const float u = unit();
        const float delay = 0.004f + 0.036f * u * std::sqrt(u);
        const float cutoff = 2500.0f + 9000.0f * std::exp(-delay / 0.03f);
        const float level = 0.5f * std::exp(-delay / 0.025f) * (unit() < 0.3f ? -1.0f : 1.0f);
        mixInto(dry, filter(blast, F::LowPass, cutoff, cutoff), fire + delay, level);
    }

    mixInto(dry, envelope(filter(noise(0.05f, seed + 1), F::HighPass, 3500.0f, 3500.0f), 0.0001f, 0.007f), fire, 0.55f);
    mixInto(dry, envelope(filter(noise(0.2f, seed + 2), F::BandPass, 4500.0f, 1800.0f, 0.7f), 0.0003f, 0.03f), fire, 0.4f);

    mixInto(dry, drive(envelope(filter(noise(0.3f, seed + 3), F::BandPass, 1600.0f, 700.0f, 0.9f), 0.0003f, 0.04f), 2.0f), fire,
            0.8f);

    mixInto(dry, envelope(filter(noise(0.25f, seed + 4), F::BandPass, 380.0f, 200.0f, 1.2f), 0.0005f, 0.03f), fire, 0.55f);
    mixInto(dry, envelope(tone(0.25f, 110.0f, 45.0f), 0.0015f, 0.04f), fire, 0.3f);
    dry = drive(std::move(dry), 1.6f);
    mixInto(dry, blast, fire, 0.4f);

    Stereo out;
    for (Samples* channel : {&out.left, &out.right}) {
        Samples heard = dry;
        constexpr float kSlapAt[3] = {0.11f, 0.19f, 0.28f};
        constexpr float kSlapCutoff[3] = {4500.0f, 3000.0f, 2000.0f};
        constexpr float kSlapLevel[3] = {0.3f, 0.2f, 0.13f};
        for (int k = 0; k < 3; ++k) {
            mixInto(heard, filter(dry, F::LowPass, kSlapCutoff[k], kSlapCutoff[k]), kSlapAt[k] + 0.03f * unit(), kSlapLevel[k]);
        }
        const bool left = channel == &out.left;
        const Stereo space = reverb(heard, 1.6f, 1.5f, 2.2f, 0.3f, seed + (left ? 5u : 6u));

        const Stereo near = reverb(dry, 0.4f, 0.4f, 0.55f, 0.25f, seed + (left ? 7u : 8u));
        *channel = std::move(heard);
        mixInto(*channel, filter(left ? space.left : space.right, F::HighPass, 160.0f, 160.0f), 0.0f, 0.5f);
        mixInto(*channel, left ? near.left : near.right, 0.0f, 0.4f);
    }
    out.left.resize(std::max(out.left.size(), out.right.size()), 0.0f);
    out.right.resize(out.left.size(), 0.0f);
    compress(out, 0.6f, 3.0f, 0.12f);
    return out;
}

}

void buildPlaceholderSounds(engine::SoundBank& bank) {
    {
        Samples s = struck(11, kSear);
        mixInto(s, struck(12, kCylinderLock), 0.012f, 0.8f);
        mixInto(s, struck(13, kTick), 0.035f, 0.12f);
        bank.add("revolver.cock", inHand(s, 0.55f, 14));
    }
    {
        Samples s = struck(21, kHammer);
        mixInto(s, struck(22, kTick), 0.002f, 0.3f);
        bank.add("revolver.dry", inHand(s, 0.5f, 23));
    }
    for (std::uint32_t v = 0; v < 3; ++v) {
        bank.add("revolver.shot", done(gunshot(v), 0.95f, 0.3f));
    }
    {
        Samples s = gain(struck(31, kHammer), 0.8f);
        mixInto(s, envelope(filter(noise(0.02f, 32), F::HighPass, 3000.0f, 3000.0f), 0.0001f, 0.002f), 0.0015f, 0.6f);
        mixInto(s, envelope(filter(noise(0.12f, 33), F::BandPass, 2200.0f, 700.0f, 2.0f), 0.0005f, 0.025f), 0.0015f, 0.5f);
        mixInto(s, envelope(tone(0.15f, 120.0f, 60.0f), 0.001f, 0.03f), 0.0015f, 0.12f);
        bank.add("revolver.discharge", inHand(s, 0.6f, 34));
    }
    {
        Samples s = gain(struck(41, kLatch), 0.5f);
        mixInto(s, scrape(42, 0.1f, 500.0f, 900.0f, 60.0f), 0.01f, 0.07f);
        mixInto(s, struck(43, kCraneStop), 0.09f, 0.8f);
        mixInto(s, struck(44, kTick), 0.112f, 0.15f);
        mixInto(s, struck(45, kTick), 0.137f, 0.08f);
        bank.add("revolver.open", inHand(s, 0.5f, 46));
    }
    {
        Samples s = struck(51, kFrame);
        mixInto(s, struck(52, kLatch), 0.014f, 0.6f);
        mixInto(s, struck(53, kTick), 0.04f, 0.15f);
        mixInto(s, struck(54, kTick), 0.062f, 0.07f);
        bank.add("revolver.close", inHand(s, 0.65f, 55));
    }
    for (std::uint32_t v = 0; v < 3; ++v) {
        bank.add("revolver.turn", inHand(struck(60 + v, kDetent), 0.3f, 64 + v, 0.06f));
    }
    {
        Samples s = gain(scrape(71, 0.09f, 1800.0f, 3200.0f, 90.0f), 0.25f);
        mixInto(s, struck(72, kCraneStop), 0.07f, 0.6f);
        for (std::uint32_t k = 0; k < 4; ++k) {
            mixInto(s, brass(73 + k, 3500.0f + 260.0f * static_cast<float>(k), 0.02f), 0.05f + 0.03f * static_cast<float>(k), 0.2f);
        }
        bank.add("revolver.eject", inHand(s, 0.5f, 78));
    }
    for (std::uint32_t v = 0; v < 3; ++v) {
        Samples s = gain(scrape(80 + v, 0.055f, 2500.0f, 1500.0f, 120.0f), 0.2f);
        mixInto(s, struck(84 + v, kSeat), 0.055f, 1.0f);
        mixInto(s, brass(88 + v, 3600.0f + 150.0f * static_cast<float>(v), 0.008f), 0.055f, 0.3f);
        bank.add("revolver.load", inHand(s, 0.4f, 92 + v));
    }
    {
        Samples s = gain(scrape(100, 0.05f, 2500.0f, 1500.0f, 120.0f), 0.2f);
        const float seats[6] = {0.030f, 0.034f, 0.041f, 0.045f, 0.052f, 0.060f};
        for (std::uint32_t k = 0; k < 6; ++k) {
            mixInto(s, struck(101 + k, kSeat), seats[k], 0.5f);
            mixInto(s, brass(108 + k, 3500.0f + 90.0f * static_cast<float>(k), 0.008f), seats[k], 0.12f);
        }
        mixInto(s, struck(115, kLatch), 0.22f, 0.5f);
        mixInto(s, struck(116, kTick), 0.245f, 0.2f);
        bank.add("revolver.speedload", inHand(s, 0.55f, 117));
    }
    {
        Samples s = gain(scrape(160, 0.22f, 900.0f, 450.0f, 40.0f), 0.35f);
        mixInto(s, gain(struck(161, kTick), 0.5f), 0.2f, 0.4f);
        mixInto(s, envelope(tone(0.12f, 140.0f, 90.0f), 0.002f, 0.04f), 0.205f, 0.25f);
        bank.add("revolver.holster", inHand(s, 0.45f, 162));
    }
    {
        Samples s = gain(scrape(165, 0.16f, 500.0f, 1100.0f, 40.0f), 0.35f);
        mixInto(s, struck(166, kLatch), 0.17f, 0.35f);
        bank.add("revolver.draw", inHand(s, 0.45f, 167));
    }
    for (std::uint32_t v = 0; v < 3; ++v) {
        const float hz = 3400.0f + 420.0f * static_cast<float>(v);
        Samples s = brass(130 + v, hz, 0.045f);
        const float bounceAt[3] = {0.11f, 0.18f, 0.225f};
        const float bounceLevel[3] = {0.55f, 0.3f, 0.15f};
        for (std::uint32_t k = 0; k < 3; ++k) {
            mixInto(s, brass(134 + v * 4 + k, hz * (0.97f + 0.03f * static_cast<float>(k)), 0.035f), bounceAt[k], bounceLevel[k]);
        }
        bank.add("casing.drop", done(std::move(s), 0.3f));
    }

    for (std::uint32_t v = 0; v < 2; ++v) {
        Samples s = envelope(tone(0.2f, 120.0f + 15.0f * static_cast<float>(v), 60.0f), 0.0005f, 0.04f);
        mixInto(s, envelope(filter(noise(0.25f, 200 + v), F::LowPass, 900.0f, 900.0f), 0.0f, 0.05f));
        mixInto(s, envelope(filter(noise(0.3f, 202 + v), F::HighPass, 2000.0f, 2000.0f), 0.0f, 0.08f), 0.0f, 0.2f);
        bank.add("impact.ground", done(std::move(s), 0.7f));
    }
    for (std::uint32_t v = 0; v < 2; ++v) {
        Samples s = envelope(filter(noise(0.05f, 210 + v), F::HighPass, 1200.0f, 1200.0f), 0.0f, 0.012f);
        mixInto(s, envelope(filter(noise(0.3f, 212 + v), F::BandPass, 700.0f + 80.0f * static_cast<float>(v), 600.0f, 1.0f),
                            0.0f, 0.06f));

        mixInto(s, envelope(crackle(0.5f, 60.0f, 214 + v), 0.02f, 0.15f), 0.0f, 0.5f);
        bank.add("impact.concrete", done(std::move(s), 0.7f));
    }
    for (std::uint32_t v = 0; v < 2; ++v) {
        const float hz = 320.0f + 40.0f * static_cast<float>(v);
        Samples s = envelope(tone(0.15f, hz, hz * 0.62f), 0.0005f, 0.03f);
        mixInto(s, envelope(tone(0.1f, hz * 2.1f, hz * 1.6f), 0.0005f, 0.015f), 0.0f, 0.5f);
        mixInto(s, envelope(filter(noise(0.1f, 220 + v), F::BandPass, 1500.0f, 1500.0f, 2.0f), 0.0f, 0.02f), 0.0f, 0.8f);
        bank.add("impact.wood", done(std::move(s), 0.7f));
    }
    for (std::uint32_t v = 0; v < 2; ++v) {
        const float base = 1100.0f + 130.0f * static_cast<float>(v);
        Samples s = envelope(filter(noise(0.03f, 230 + v), F::HighPass, 3000.0f, 3000.0f), 0.0f, 0.008f);
        mixInto(s, ping(0.7f, base, 0.25f), 0.0f, 0.6f);
        mixInto(s, ping(0.5f, base * 2.3f, 0.15f), 0.0f, 0.45f);
        mixInto(s, ping(0.4f, base * 3.61f, 0.1f), 0.0f, 0.3f);
        mixInto(s, ping(0.3f, base * 5.27f, 0.06f), 0.0f, 0.2f);
        bank.add("impact.steel", done(std::move(s), 0.7f, 0.1f));
    }
    for (std::uint32_t v = 0; v < 2; ++v) {
        const float from = 3800.0f - 500.0f * static_cast<float>(v);
        Samples s = multiply(envelope(tone(0.6f, from, from * 0.37f), 0.005f, 0.2f), lfo(0.6f, 38.0f, 0.6f, 1.0f));
        mixInto(s, envelope(filter(noise(0.6f, 240 + v), F::BandPass, from, from * 0.37f, 8.0f), 0.005f, 0.2f), 0.0f, 0.6f);
        bank.add("impact.ricochet", done(std::move(s), 0.45f, 0.1f));
    }

    {
        Samples bed = multiply(filter(noise(4.3f, 300), F::LowPass, 600.0f, 600.0f), wobble(4.3f, 6.0f, 301));
        mixInto(bed, filter(noise(4.3f, 302), F::LowPass, 250.0f, 250.0f), 0.0f, 0.8f);
        mixInto(bed, crackle(4.3f, 9.0f, 303), 0.0f, 2.5f);
        bank.add("fire.loop", engine::SoundBuffer{normalize(loopable(bed, 0.3f), 0.5f)});
    }
    {
        Samples s = envelope(filter(noise(0.6f, 310), F::LowPass, 200.0f, 2500.0f), 0.05f, 0.15f);
        mixInto(s, envelope(tone(0.4f, 90.0f, 50.0f), 0.01f, 0.12f), 0.0f, 0.6f);
        bank.add("fire.catch", done(std::move(s), 0.6f, 0.1f));
    }
    {
        Samples bed = multiply(filter(noise(4.4f, 320), F::LowPass, 350.0f, 350.0f), wobble(4.4f, 3.0f, 321));
        mixInto(bed, multiply(filter(noise(4.4f, 322), F::BandPass, 900.0f, 900.0f, 0.8f), wobble(4.4f, 8.0f, 323)), 0.0f, 0.5f);
        mixInto(bed, filter(noise(4.4f, 324), F::LowPass, 90.0f, 90.0f), 0.0f, 1.5f);
        mixInto(bed, crackle(4.4f, 14.0f, 325), 0.0f, 1.5f);
        bank.add("inferno.loop", engine::SoundBuffer{normalize(drive(loopable(bed, 0.4f), 1.5f), 0.7f)});
    }
    {
        Samples s = envelope(filter(noise(1.2f, 330), F::LowPass, 150.0f, 3000.0f), 0.15f, 0.35f);
        mixInto(s, envelope(tone(0.9f, 70.0f, 35.0f), 0.02f, 0.3f), 0.0f, 0.8f);
        bank.add("inferno.start", done(drive(std::move(s), 1.8f), 0.85f, 0.2f));
    }
    for (std::uint32_t v = 0; v < 2; ++v) {
        Samples s = envelope(filter(noise(0.9f, 340 + v), F::BandPass, 300.0f, 1400.0f + 200.0f * static_cast<float>(v), 1.2f),
                             0.06f, 0.18f);
        mixInto(s, envelope(filter(noise(0.9f, 342 + v), F::BandPass, 2500.0f, 1800.0f, 6.0f), 0.08f, 0.15f), 0.0f, 0.15f);
        bank.add("wind.gust", done(std::move(s), 0.6f, 0.15f));
    }
    for (std::uint32_t v = 0; v < 2; ++v) {
        Samples s = envelope(filter(noise(0.05f, 350 + v), F::HighPass, 2000.0f, 2000.0f), 0.0f, 0.008f);
        mixInto(s, envelope(filter(noise(0.5f, 352 + v), F::BandPass, 1200.0f, 300.0f, 0.8f), 0.0f, 0.12f));
        mixInto(s, envelope(drive(tone(0.3f, 110.0f + 12.0f * static_cast<float>(v), 100.0f), 6.0f), 0.0f, 0.05f), 0.0f, 0.3f);
        mixInto(s, envelope(filter(noise(0.9f, 354 + v), F::LowPass, 400.0f, 200.0f), 0.02f, 0.3f), 0.0f, 0.3f);
        bank.add("lightning.crack", done(drive(std::move(s), 3.0f), 0.9f, 0.2f));
    }
    {
        Samples s = multiply(filter(noise(1.2f, 360), F::BandPass, 200.0f, 2500.0f, 2.0f), wobble(1.2f, 30.0f, 361));
        mixInto(s, tone(1.2f, 60.0f, 180.0f), 0.0f, 0.3f);
        bank.add("thunder.build", done(envelope(std::move(s), 1.2f, 0.0f), 0.5f));
    }
    {
        Samples s = envelope(filter(noise(0.06f, 370), F::HighPass, 1500.0f, 1500.0f), 0.0f, 0.01f);
        mixInto(s, envelope(filter(noise(2.0f, 371), F::LowPass, 2500.0f, 120.0f), 0.002f, 0.5f));
        mixInto(s, envelope(multiply(filter(noise(3.0f, 372), F::LowPass, 120.0f, 120.0f), wobble(3.0f, 5.0f, 373)), 0.1f, 1.0f),
                0.0f, 2.0f);
        mixInto(s, envelope(tone(1.0f, 70.0f, 28.0f), 0.0f, 0.4f));
        bank.add("thunder.strike", done(drive(std::move(s), 3.0f), 0.98f, 0.5f));
    }
    {
        Samples s = envelope(filter(noise(0.7f, 380), F::BandPass, 3500.0f, 1500.0f, 3.0f), 0.03f, 0.18f);
        bank.add("haze.shot", done(std::move(s), 0.3f, 0.1f));
    }
    {
        Samples s = ping(0.3f, 2800.0f, 0.08f);
        mixInto(s, envelope(filter(noise(0.1f, 385), F::BandPass, 5000.0f, 5000.0f, 2.0f), 0.0f, 0.03f), 0.0f, 0.6f);
        bank.add("haze.fade", done(std::move(s), 0.2f));
    }
    {
        Samples s = multiply(filter(noise(0.9f, 390), F::BandPass, 500.0f, 1500.0f, 0.7f), lfo(0.9f, 23.0f, 0.65f, 1.0f));
        mixInto(s, filter(noise(0.9f, 391), F::HighPass, 3000.0f, 3000.0f), 0.0f, 0.12f);
        mixInto(s, tone(0.9f, 80.0f, 60.0f), 0.0f, 0.3f);
        bank.add("ghostfire.breath", done(envelope(std::move(s), 0.05f, 0.0f), 0.7f, 0.4f));
    }
    {
        Samples s = envelope(filter(noise(1.0f, 400), F::LowPass, 2500.0f, 400.0f), 0.02f, 0.25f);
        bank.add("fog.burst", done(std::move(s), 0.45f, 0.2f));
    }
    {
        Samples spit = wobble(1.2f, 60.0f, 411);
        spit = multiply(spit, spit);
        Samples s = multiply(filter(noise(1.2f, 410), F::HighPass, 1500.0f, 1500.0f), spit);
        mixInto(s, multiply(drive(tone(1.2f, 100.0f, 100.0f), 5.0f), wobble(1.2f, 45.0f, 412)), 0.0f, 0.35f);
        bank.add("fog.electrify", done(envelope(std::move(s), 0.01f, 0.5f), 0.7f, 0.2f));
    }
    {
        Samples s = envelope(tone(0.8f, 90.0f, 30.0f), 0.0f, 0.25f);
        mixInto(s, envelope(filter(noise(1.0f, 420), F::LowPass, 3000.0f, 200.0f), 0.002f, 0.2f));
        mixInto(s, envelope(filter(noise(2.0f, 421), F::HighPass, 3500.0f, 2500.0f), 0.05f, 0.6f), 0.0f, 0.5f);
        bank.add("steam.explosion", done(drive(std::move(s), 2.5f), 0.95f, 0.4f));
    }
    {
        Samples s = envelope(filter(noise(1.6f, 430), F::BandPass, 300.0f, 2000.0f, 1.0f), 0.15f, 0.5f);
        mixInto(s, envelope(crackle(1.6f, 40.0f, 431), 0.1f, 0.5f), 0.0f, 1.5f);
        mixInto(s, envelope(tone(1.0f, 110.0f, 55.0f), 0.01f, 0.4f), 0.0f, 0.5f);
        bank.add("firelight.pulse", done(std::move(s), 0.6f, 0.3f));
    }
    {
        Samples s = envelope(tone(0.09f, 300.0f, 3000.0f), 0.06f, 0.0f);
        mixInto(s, envelope(noise(0.03f, 440), 0.0f, 0.004f), 0.09f);
        bank.add("blink", done(std::move(s), 0.7f, 0.1f));
    }
    {
        Samples bed = multiply(filter(noise(3.3f, 450), F::BandPass, 700.0f, 700.0f, 1.5f), wobble(3.3f, 1.5f, 451));
        mixInto(bed, filter(noise(3.3f, 452), F::BandPass, 1600.0f, 1600.0f, 3.0f), 0.0f, 0.15f);
        bank.add("tailwind.loop", engine::SoundBuffer{normalize(loopable(bed, 0.3f), 0.35f)});
    }
    {
        Samples s = multiply(tone(3.0f, 82.0f, 82.0f), lfo(3.0f, 1.0f / 3.0f, 0.5f, 1.0f));
        mixInto(s, multiply(tone(3.0f, 370.0f / 3.0f, 370.0f / 3.0f), lfo(3.0f, 2.0f / 3.0f, 0.2f, 0.7f)), 0.0f, 0.6f);
        mixInto(s, loopable(filter(noise(3.3f, 460), F::LowPass, 250.0f, 250.0f), 0.3f), 0.0f, 1.2f);
        s.resize(static_cast<std::size_t>(3 * engine::kAudioSampleRate));
        bank.add("shroud.loop", engine::SoundBuffer{normalize(std::move(s), 0.35f)});
    }
    {
        Samples s = envelope(tone(0.4f, 110.0f, 45.0f), 0.0f, 0.12f);
        mixInto(s, envelope(filter(noise(0.2f, 470), F::LowPass, 800.0f, 800.0f), 0.0f, 0.04f));
        mixInto(s, envelope(tone(0.7f, 233.0f, 229.0f), 0.005f, 0.2f), 0.0f, 0.35f);
        mixInto(s, envelope(tone(0.7f, 247.0f, 243.0f), 0.005f, 0.2f), 0.0f, 0.35f);
        bank.add("voodoo.hit", done(std::move(s), 0.8f, 0.1f));
    }

    {
        Samples s = multiply(tone(3.0f, 220.0f, 220.0f), lfo(3.0f, 1.0f / 3.0f, 0.4f, 1.0f));
        mixInto(s, multiply(tone(3.0f, 331.0f, 331.0f), lfo(3.0f, 2.0f / 3.0f, 0.2f, 0.8f)), 0.0f, 0.6f);
        mixInto(s, multiply(tone(3.0f, 442.0f, 442.0f), lfo(3.0f, 1.0f, 0.1f, 0.6f)), 0.0f, 0.4f);
        mixInto(s, loopable(multiply(filter(noise(3.3f, 500), F::BandPass, 1800.0f, 1800.0f, 4.0f), wobble(3.3f, 2.0f, 501)), 0.3f),
                0.0f, 0.5f);
        s.resize(static_cast<std::size_t>(3 * engine::kAudioSampleRate));
        bank.add("wisp.loop", engine::SoundBuffer{normalize(std::move(s), 0.4f)});
    }
    {
        Samples s = envelope(filter(noise(0.1f, 510), F::HighPass, 1500.0f, 1500.0f), 0.0f, 0.02f);
        mixInto(s, envelope(tone(0.4f, 1800.0f, 300.0f), 0.0f, 0.08f));
        mixInto(s, envelope(tone(0.5f, 120.0f, 50.0f), 0.0f, 0.1f));
        mixInto(s, envelope(filter(noise(0.9f, 511), F::BandPass, 3000.0f, 2000.0f, 2.0f), 0.0f, 0.3f), 0.0f, 0.3f);
        bank.add("wisp.burst", done(drive(std::move(s), 2.0f), 0.9f, 0.2f));
    }
    {
        Samples s = envelope(filter(noise(0.35f, 520), F::BandPass, 800.0f, 3000.0f, 2.0f), 0.03f, 0.06f);
        bank.add("wisp.dodge", done(std::move(s), 0.35f, 0.05f));
    }
    {
        Samples s = envelope(tone(1.1f, 70.0f, 140.0f), 0.5f, 0.0f);
        mixInto(s, envelope(filter(noise(1.1f, 530), F::BandPass, 300.0f, 900.0f, 5.0f), 0.6f, 0.0f), 0.0f, 0.7f);
        mixInto(s, multiply(tone(1.1f, 143.0f, 290.0f), lfo(1.1f, 9.0f, 0.0f, 0.4f)), 0.0f, 0.3f);
        bank.add("poltergeist.lift", done(std::move(s), 0.5f, 0.12f));
    }
    {
        Samples s = envelope(filter(noise(0.6f, 540), F::BandPass, 400.0f, 1800.0f, 1.5f), 0.05f, 0.12f);
        mixInto(s, envelope(tone(0.3f, 130.0f, 60.0f), 0.0f, 0.08f), 0.0f, 0.5f);
        bank.add("poltergeist.throw", done(std::move(s), 0.6f, 0.1f));
    }
    for (std::uint32_t v = 0; v < 2; ++v) {
        const float hz = 1250.0f + 160.0f * static_cast<float>(v);
        Samples s = envelope(tone(0.5f, hz, hz * 0.88f), 0.003f, 0.15f);
        mixInto(s, envelope(tone(0.5f, hz * 1.01f, hz * 0.885f), 0.003f, 0.15f));
        mixInto(s, envelope(tone(0.4f, hz * 1.5f, hz * 1.3f), 0.003f, 0.08f), 0.0f, 0.4f);
        mixInto(s, envelope(filter(noise(0.4f, 550 + v), F::BandPass, 2000.0f, 1500.0f, 2.0f), 0.01f, 0.1f), 0.0f, 0.5f);
        bank.add("ghost.hurt", done(std::move(s), 0.5f, 0.1f));
    }
    {
        Samples s = envelope(tone(1.4f, 900.0f, 180.0f), 0.02f, 0.5f);
        mixInto(s, envelope(tone(1.4f, 1350.0f, 270.0f), 0.02f, 0.5f), 0.0f, 0.5f);
        mixInto(s, envelope(filter(noise(1.4f, 560), F::BandPass, 1500.0f, 300.0f, 3.0f), 0.05f, 0.5f), 0.0f, 0.6f);
        bank.add("ghost.death", done(std::move(s), 0.6f, 0.3f));
    }
    {
        Samples s = ping(1.0f, 1320.0f, 0.4f);
        mixInto(s, ping(1.0f, 1980.0f, 0.3f), 0.03f, 0.6f);
        bank.add("ghost.reveal", done(std::move(s), 0.3f, 0.2f));
    }
    {
        Samples s = envelope(tone(0.25f, 150.0f, 45.0f), 0.001f, 0.09f);
        mixInto(s, envelope(filter(noise(0.08f, 180), F::LowPass, 1800.0f, 400.0f), 0.0005f, 0.03f), 0.0f, 0.8f);
        mixInto(s, envelope(filter(noise(0.5f, 181), F::BandPass, 900.0f, 250.0f, 1.5f), 0.02f, 0.25f), 0.01f, 0.35f);
        bank.add("wind.blast", done(std::move(s), 0.75f, 0.15f));
    }
    {
        Samples s = ping(0.7f, 2350.0f, 0.22f);
        mixInto(s, ping(0.7f, 3520.0f, 0.14f), 0.0f, 0.5f);
        mixInto(s, ping(0.5f, 5270.0f, 0.08f), 0.0f, 0.25f);
        bank.add("revolver.last", inHand(s, 0.4f, 171, 0.2f));
    }
    {
        Samples s = envelope(filter(noise(0.03f, 596), F::BandPass, 3200.0f, 2400.0f, 3.0f), 0.0f, 0.012f);
        bank.add("hit.mark", done(std::move(s), 0.4f));
        Samples k = envelope(filter(noise(0.05f, 597), F::BandPass, 1800.0f, 900.0f, 3.0f), 0.0f, 0.02f);
        mixInto(k, ping(0.35f, 660.0f, 0.12f), 0.0f, 0.5f);
        bank.add("hit.kill", done(std::move(k), 0.5f, 0.1f));
    }
    {
        Samples s = multiply(filter(noise(0.35f, 570), F::HighPass, 2500.0f, 4000.0f), lfo(0.35f, 60.0f, 0.2f, 1.0f));
        bank.add("ball.crackle", done(envelope(std::move(s), 0.25f, 0.05f), 0.35f, 0.03f));
    }
    for (std::uint32_t v = 0; v < 2; ++v) {
        Samples s = envelope(filter(noise(0.25f, 572 + v), F::HighPass, 1200.0f, 900.0f), 0.0f, 0.06f);
        mixInto(s, envelope(multiply(tone(0.25f, 120.0f + 15.0f * static_cast<float>(v), 90.0f), lfo(0.25f, 50.0f, 0.3f, 1.0f)), 0.0f, 0.12f), 0.0f, 0.6f);
        bank.add("ball.arc", done(drive(std::move(s), 3.0f), 0.7f, 0.08f));
    }
    {
        Samples s = envelope(filter(noise(0.6f, 575), F::HighPass, 900.0f, 300.0f), 0.0f, 0.18f);
        mixInto(s, envelope(filter(noise(0.6f, 576), F::LowPass, 400.0f, 120.0f), 0.01f, 0.35f), 0.0f, 0.8f);
        bank.add("ball.hop", done(drive(std::move(s), 2.5f), 0.8f, 0.2f));
    }
    {
        Samples s = envelope(multiply(filter(noise(0.8f, 580), F::BandPass, 600.0f, 1500.0f, 1.5f), lfo(0.8f, 23.0f, 0.1f, 1.0f)), 0.02f, 0.4f);
        mixInto(s, envelope(tone(0.8f, 60.0f, 110.0f), 0.1f, 0.4f), 0.0f, 0.6f);
        bank.add("mimic.reveal", done(drive(std::move(s), 1.8f), 0.7f, 0.2f));
    }
    {
        Samples s = envelope(filter(noise(0.35f, 585), F::BandPass, 500.0f, 2600.0f, 2.0f), 0.12f, 0.08f);
        bank.add("mimic.lash", done(std::move(s), 0.6f, 0.05f));
    }
    {
        Samples s = envelope(multiply(filter(noise(0.6f, 587), F::BandPass, 1400.0f, 500.0f, 1.5f), lfo(0.6f, 17.0f, 0.2f, 1.0f)), 0.05f, 0.3f);
        bank.add("mimic.conceal", done(std::move(s), 0.5f, 0.1f));
    }
    for (std::uint32_t v = 0; v < 2; ++v) {
        Samples s = envelope(multiply(filter(noise(0.5f, 700 + v), F::HighPass, 3500.0f, 2500.0f), lfo(0.5f, 34.0f + 9.0f * static_cast<float>(v), 0.15f, 1.0f)), 0.03f, 0.3f);
        mixInto(s, ping(0.3f, 2400.0f + 300.0f * static_cast<float>(v), 0.1f), 0.02f, 0.25f);
        bank.add("kraka.scatter", done(std::move(s), 0.45f, 0.08f));
    }
    {
        Samples s = envelope(filter(noise(0.7f, 704), F::BandPass, 700.0f, 3200.0f, 2.0f), 0.3f, 0.2f);
        mixInto(s, envelope(multiply(tone(0.5f, 520.0f, 880.0f), lfo(0.5f, 70.0f, 0.2f, 1.0f)), 0.1f, 0.25f), 0.1f, 0.5f);
        bank.add("kraka.dive", done(drive(std::move(s), 2.0f), 0.7f, 0.1f));
    }
    {
        Samples s = envelope(multiply(filter(noise(0.7f, 706), F::BandPass, 1500.0f, 5000.0f, 2.0f), lfo(0.7f, 22.0f, 0.0f, 1.0f)), 0.5f, 0.05f);
        mixInto(s, envelope(tone(0.7f, 300.0f, 900.0f), 0.5f, 0.05f), 0.0f, 0.3f);
        bank.add("kraka.ball", done(std::move(s), 0.6f, 0.05f));
    }
    {
        Samples s = envelope(filter(noise(0.6f, 708), F::HighPass, 2500.0f, 1200.0f), 0.0f, 0.2f);
        mixInto(s, envelope(tone(0.3f, 140.0f, 50.0f), 0.0f, 0.1f), 0.0f, 0.9f);
        mixInto(s, ping(0.6f, 3100.0f, 0.2f), 0.01f, 0.3f);
        mixInto(s, ping(0.6f, 4300.0f, 0.15f), 0.03f, 0.25f);
        bank.add("kraka.burst", done(drive(std::move(s), 2.5f), 0.9f, 0.2f));
    }
    {
        Samples s = ping(0.9f, 1560.0f, 0.4f);
        mixInto(s, ping(0.9f, 2340.0f, 0.3f), 0.08f, 0.6f);
        mixInto(s, envelope(filter(noise(0.8f, 710), F::HighPass, 4000.0f, 3000.0f), 0.3f, 0.3f), 0.0f, 0.3f);
        bank.add("kraka.merge", done(std::move(s), 0.4f, 0.2f));
    }
    {
        Samples s = envelope(multiply(filter(noise(0.7f, 720), F::LowPass, 700.0f, 300.0f), lfo(0.7f, 13.0f, 0.2f, 1.0f)), 0.1f, 0.3f);
        mixInto(s, envelope(filter(noise(0.3f, 721), F::BandPass, 1800.0f, 900.0f, 2.0f), 0.02f, 0.1f), 0.15f, 0.3f);
        bank.add("necromite.emerge", done(std::move(s), 0.55f, 0.1f));
    }
    {
        Samples s = envelope(filter(noise(0.12f, 722), F::BandPass, 1200.0f, 500.0f, 2.0f), 0.0f, 0.05f);
        mixInto(s, envelope(multiply(filter(noise(0.5f, 723), F::LowPass, 900.0f, 400.0f), lfo(0.5f, 31.0f, 0.1f, 1.0f)), 0.03f, 0.25f), 0.06f, 0.7f);
        mixInto(s, envelope(tone(0.2f, 90.0f, 50.0f), 0.0f, 0.08f), 0.0f, 0.6f);
        bank.add("necromite.enter", done(drive(std::move(s), 2.0f), 0.7f, 0.1f));
    }
    {
        Samples s = envelope(multiply(tone(1.3f, 82.0f, 64.0f), lfo(1.3f, 27.0f, 0.3f, 1.0f)), 0.25f, 0.5f);
        mixInto(s, envelope(multiply(tone(1.3f, 124.0f, 97.0f), lfo(1.3f, 19.0f, 0.2f, 1.0f)), 0.3f, 0.5f), 0.0f, 0.5f);
        mixInto(s, envelope(filter(noise(1.3f, 724), F::BandPass, 500.0f, 350.0f, 2.0f), 0.3f, 0.5f), 0.0f, 0.5f);
        bank.add("zombie.groan", done(drive(std::move(s), 2.2f), 0.7f, 0.3f));
    }
    for (std::uint32_t v = 0; v < 3; ++v) {
        Samples s = envelope(filter(noise(0.06f, 590 + v), F::BandPass, 900.0f + 200.0f * static_cast<float>(v), 600.0f, 2.0f), 0.0f, 0.02f);
        bank.add("mimic.step", done(std::move(s), 0.25f));
    }

    for (std::uint32_t v = 0; v < 4; ++v) {
        const float hz = 95.0f + 9.0f * static_cast<float>(v);
        Samples s = envelope(filter(noise(0.15f, 600 + v), F::LowPass, 500.0f + 60.0f * static_cast<float>(v), 300.0f), 0.002f, 0.03f);
        mixInto(s, envelope(tone(0.12f, hz, hz * 0.63f), 0.0f, 0.03f), 0.0f, 0.6f);
        mixInto(s, envelope(filter(noise(0.1f, 610 + v), F::BandPass, 2500.0f, 2500.0f, 1.0f), 0.005f, 0.025f), 0.0f, 0.12f);
        bank.add("step", done(std::move(s), 0.11f));
    }
    {
        Samples s = envelope(filter(noise(0.25f, 620), F::LowPass, 450.0f, 200.0f), 0.002f, 0.05f);
        mixInto(s, envelope(tone(0.2f, 80.0f, 45.0f), 0.0f, 0.05f), 0.0f, 0.8f);
        bank.add("land", done(std::move(s), 0.5f));
    }
    {
        Samples s = envelope(tone(0.35f, 140.0f, 50.0f), 0.0f, 0.1f);
        mixInto(s, envelope(filter(noise(0.2f, 630), F::LowPass, 1200.0f, 600.0f), 0.0f, 0.05f));
        bank.add("player.hurt", done(drive(std::move(s), 2.0f), 0.8f, 0.05f));
    }
    {
        Samples s(static_cast<std::size_t>(engine::kAudioSampleRate), 0.0f);
        const Samples beat = envelope(tone(0.25f, 60.0f, 38.0f), 0.005f, 0.05f);
        mixInto(s, beat, 0.0f);
        mixInto(s, beat, 0.28f, 0.7f);
        s.resize(static_cast<std::size_t>(engine::kAudioSampleRate));
        bank.add("heartbeat.loop", engine::SoundBuffer{normalize(std::move(s), 0.7f)});
    }
    {
        Samples s = ping(0.5f, 880.0f, 0.15f);
        mixInto(s, ping(0.5f, 1320.0f, 0.15f), 0.06f, 0.8f);
        mixInto(s, ping(0.5f, 1760.0f, 0.18f), 0.12f, 0.7f);
        bank.add("pickup", done(std::move(s), 0.35f, 0.1f));
    }
    {
        Samples s = ping(0.5f, 1760.0f, 0.12f);
        mixInto(s, ping(0.5f, 1318.0f, 0.18f), 0.08f, 0.8f);
        mixInto(s, envelope(filter(noise(0.4f, 640), F::BandPass, 3000.0f, 1500.0f, 2.0f), 0.02f, 0.1f), 0.0f, 0.3f);
        bank.add("material.drop", done(std::move(s), 0.3f, 0.1f));
    }
    {
        Samples bed = multiply(filter(noise(2.3f, 650), F::BandPass, 900.0f, 900.0f, 0.8f), wobble(2.3f, 14.0f, 651));
        mixInto(bed, filter(noise(2.3f, 652), F::LowPass, 300.0f, 300.0f), 0.0f, 0.7f);
        mixInto(bed, multiply(filter(noise(2.3f, 653), F::HighPass, 3500.0f, 3500.0f), wobble(2.3f, 30.0f, 654)), 0.0f, 0.15f);
        bank.add("slide.loop", engine::SoundBuffer{normalize(loopable(bed, 0.3f), 0.4f)});
    }

    {
        Samples s = multiply(filter(noise(0.25f, 700), F::HighPass, 3000.0f, 3000.0f), wobble(0.25f, 25.0f, 701));
        bank.add("bench.dose", done(envelope(std::move(s), 0.01f, 0.06f), 0.3f));
    }
    {
        Samples bed = multiply(filter(noise(2.3f, 710), F::BandPass, 1200.0f, 1200.0f, 1.0f), wobble(2.3f, 7.0f, 711));
        mixInto(bed, filter(noise(2.3f, 712), F::LowPass, 200.0f, 200.0f), 0.0f, 0.6f);
        bank.add("bench.grind.loop", engine::SoundBuffer{normalize(loopable(bed, 0.3f), 0.35f)});
    }
    {
        Samples s = envelope(filter(noise(0.7f, 720), F::HighPass, 4000.0f, 2500.0f), 0.05f, 0.2f);
        bank.add("bench.pour", done(std::move(s), 0.3f, 0.1f));
    }
    {
        Samples spit = wobble(0.5f, 50.0f, 731);
        spit = multiply(multiply(spit, spit), spit);
        Samples s = envelope(multiply(filter(noise(0.5f, 730), F::HighPass, 4000.0f, 4000.0f), spit), 0.0f, 0.12f);
        mixInto(s, envelope(tone(0.08f, 500.0f, 150.0f), 0.0f, 0.02f), 0.0f, 0.3f);
        bank.add("bench.fizzle", done(std::move(s), 0.45f, 0.05f));
    }
    {
        Samples s = click(740, 4000.0f, 2100.0f, 0.2f, 0.5f);
        bank.add("bench.craft", done(std::move(s), 0.4f, 0.1f));
    }
}

}
