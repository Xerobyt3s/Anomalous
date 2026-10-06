#include "engine/audio/audio_engine.h"

#include "engine/debug/log.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <miniaudio.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>

namespace ghost::engine {
namespace {
float onePole(float hz) {
    return hz <= 0.0f ? 1.0f : 1.0f - std::exp(-6.28318531f * hz / static_cast<float>(kAudioSampleRate));
}

std::optional<SoundBuffer> readAll(ma_decoder& decoder) {
    SoundBuffer sound;
    constexpr std::size_t kChunkFrames = 2048;
    std::array<float, kChunkFrames * 2> chunk{};
    bool same = true;
    for (;;) {
        ma_uint64 read = 0;
        const ma_result result = ma_decoder_read_pcm_frames(&decoder, chunk.data(), kChunkFrames, &read);
        for (std::size_t i = 0; i < static_cast<std::size_t>(read); ++i) {
            sound.samples.push_back(chunk[i * 2]);
            sound.right.push_back(chunk[i * 2 + 1]);
            same = same && chunk[i * 2] == chunk[i * 2 + 1];
        }
        if (result != MA_SUCCESS || read < kChunkFrames) {
            break;
        }
    }
    ma_decoder_uninit(&decoder);
    if (sound.samples.empty()) {
        return std::nullopt;
    }
    if (same) {
        sound.right.clear();
    }
    return sound;
}

}

std::optional<SoundBuffer> loadSoundFile(const std::filesystem::path& path) {
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, kAudioSampleRate);
    ma_decoder decoder;
    if (ma_decoder_init_file_w(path.wstring().c_str(), &config, &decoder) != MA_SUCCESS) {
        return std::nullopt;
    }
    return readAll(decoder);
}

std::optional<SoundBuffer> decodeSound(std::string_view bytes) {
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, kAudioSampleRate);
    ma_decoder decoder;
    if (bytes.empty() || ma_decoder_init_memory(bytes.data(), bytes.size(), &config, &decoder) != MA_SUCCESS) {
        return std::nullopt;
    }
    return readAll(decoder);
}

StereoGain spatialGain(const Listener& listener, const glm::vec3& source, float minDistance, float maxDistance) {
    const glm::vec3 to = source - listener.position;
    const float distance = glm::length(to);
    if (distance >= maxDistance) {
        return {0.0f, 0.0f};
    }

    float gain = minDistance / std::max(distance, minDistance);
    const float edge = std::clamp((maxDistance - distance) / (0.3f * maxDistance), 0.0f, 1.0f);
    gain *= edge;

    const float side = distance > 1e-3f ? glm::dot(to / distance, listener.right) : 0.0f;
    const float pan = side * std::clamp(distance / std::max(minDistance * 0.5f, 1e-3f), 0.0f, 1.0f) * 0.85f;
    const float angle = (pan + 1.0f) * 0.25f * 3.14159265f;
    return {gain * std::cos(angle) * 1.41421356f, gain * std::sin(angle) * 1.41421356f};
}

struct AudioEngine::Device {
    ma_device device;
    bool open = false;
};

AudioEngine::AudioEngine() : m_device(std::make_unique<Device>()) {}

AudioEngine::~AudioEngine() {
    if (m_device->open) {
        ma_device_uninit(&m_device->device);
    }
}

bool AudioEngine::start() {
    if (m_running) {
        return true;
    }
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = kAudioSampleRate;
    config.pUserData = this;
    config.dataCallback = [](ma_device* device, void* output, const void*, ma_uint32 frames) {
        static_cast<AudioEngine*>(device->pUserData)->mix(static_cast<float*>(output), frames);
    };
    if (ma_device_init(nullptr, &config, &m_device->device) != MA_SUCCESS) {
        GHOST_WARN("Audio: no output device, running silent");
        return false;
    }
    m_device->open = true;
    if (ma_device_start(&m_device->device) != MA_SUCCESS) {
        GHOST_WARN("Audio: could not start the output device, running silent");
        return false;
    }
    m_running = true;
    GHOST_INFO("Audio: {} ({} Hz)", m_device->device.playback.name, m_device->device.sampleRate);
    return true;
}

AudioEngine::Voice* AudioEngine::find(VoiceId voice) {
    const auto it = std::find_if(m_voices.begin(), m_voices.end(), [voice](const Voice& v) { return v.id == voice; });
    return it == m_voices.end() ? nullptr : &*it;
}

VoiceId AudioEngine::play(SoundRef sound, const PlayParams& params) {
    if (!sound || sound->samples.empty()) {
        return 0;
    }
    const std::lock_guard lock(m_mutex);
    if (m_voices.size() >= kMaxVoices) {
        const auto oldest = std::find_if(m_voices.begin(), m_voices.end(), [](const Voice& v) { return !v.loop; });
        if (oldest == m_voices.end()) {
            return 0;
        }
        m_voices.erase(oldest);
    }
    Voice voice;
    voice.id = m_nextId++;
    voice.sound = std::move(sound);
    voice.volume = params.volume;
    voice.pitch = std::max(params.pitch, 0.01f);
    voice.positional = params.position.has_value();
    voice.position = params.position.value_or(glm::vec3(0.0f));
    voice.minDistance = params.minDistance;
    voice.maxDistance = params.maxDistance;
    voice.loop = params.loop;
    voice.group = std::clamp(params.group, 0, kGroups - 1);
    voice.delayFrames = static_cast<std::size_t>(std::max(params.delay, 0.0f) * static_cast<float>(kAudioSampleRate));
    if (params.fadeIn > 0.0f) {
        voice.fade = 0.0f;
        voice.fadeStep = 1.0f / (params.fadeIn * static_cast<float>(kAudioSampleRate));
    }
    voice.lowpass = params.lowpass;
    voice.highpass = params.highpass;
    m_voices.push_back(std::move(voice));
    return m_voices.back().id;
}

void AudioEngine::set(VoiceId voice, float volume, float pitch, std::optional<glm::vec3> position) {
    const std::lock_guard lock(m_mutex);
    if (Voice* v = find(voice)) {
        v->volume = volume;
        v->pitch = std::max(pitch, 0.01f);
        if (position) {
            v->position = *position;
        }
    }
}

void AudioEngine::filter(VoiceId voice, float lowpass, float highpass) {
    const std::lock_guard lock(m_mutex);
    if (Voice* v = find(voice)) {
        v->lowpass = std::max(lowpass, 0.0f);
        v->highpass = std::max(highpass, 0.0f);
    }
}

void AudioEngine::stop(VoiceId voice, float fadeSeconds) {
    const std::lock_guard lock(m_mutex);
    if (Voice* v = find(voice)) {
        v->fadeStep = -1.0f / (std::max(fadeSeconds, 0.002f) * static_cast<float>(kAudioSampleRate));
    }
}

bool AudioEngine::playing(VoiceId voice) const {
    const std::lock_guard lock(m_mutex);
    return std::any_of(m_voices.begin(), m_voices.end(), [voice](const Voice& v) { return v.id == voice; });
}

std::size_t AudioEngine::voiceCount() const {
    const std::lock_guard lock(m_mutex);
    return m_voices.size();
}

void AudioEngine::setListener(const Listener& listener) {
    const std::lock_guard lock(m_mutex);
    m_listener = listener;
}

void AudioEngine::setMasterVolume(float volume) {
    const std::lock_guard lock(m_mutex);
    m_master = std::max(volume, 0.0f);
}

void AudioEngine::setGroupVolume(int group, float volume) {
    const std::lock_guard lock(m_mutex);
    m_groups[static_cast<std::size_t>(std::clamp(group, 0, kGroups - 1))] = std::max(volume, 0.0f);
}

void AudioEngine::mix(float* output, std::size_t frames) {
    std::fill(output, output + frames * 2, 0.0f);
    const std::lock_guard lock(m_mutex);
    for (Voice& voice : m_voices) {
        std::size_t first = 0;
        if (voice.delayFrames > 0) {
            first = std::min(voice.delayFrames, frames);
            voice.delayFrames -= first;
            if (first == frames) {
                continue;
            }
        }
        const std::vector<float>& samples = voice.sound->samples;
        const double length = static_cast<double>(samples.size());

        StereoGain target{1.0f, 1.0f};
        if (voice.positional) {
            target = spatialGain(m_listener, voice.position, voice.minDistance, voice.maxDistance);
        }
        const float level = voice.volume * m_master * m_groups[static_cast<std::size_t>(voice.group)];
        target.left *= level;
        target.right *= level;
        if (voice.fresh) {
            voice.left = target.left;
            voice.right = target.right;
            voice.fresh = false;
        }
        const float count = static_cast<float>(frames - first);
        const float stepLeft = (target.left - voice.left) / count;
        const float stepRight = (target.right - voice.right) / count;
        const float low = onePole(voice.lowpass);
        const float high = voice.highpass > 0.0f ? std::exp(-6.28318531f * voice.highpass / static_cast<float>(kAudioSampleRate)) : 0.0f;
        const auto shape = [&](float x, int channel) {
            if (voice.highpass > 0.0f) {
                const float y = high * (voice.highState[channel] + x - voice.highLast[channel]);
                voice.highLast[channel] = x;
                voice.highState[channel] = y;
                x = y;
            }
            if (voice.lowpass > 0.0f) {
                float& a = voice.lowState[channel * 2];
                float& b = voice.lowState[channel * 2 + 1];
                a += low * (x - a);
                b += low * (a - b);
                x = b;
            }
            return x;
        };

        for (std::size_t i = first; i < frames; ++i) {
            if (voice.cursor >= length) {
                if (!voice.loop) {
                    voice.done = true;
                    break;
                }
                voice.cursor = std::fmod(voice.cursor, length);
            }
            const auto index = static_cast<std::size_t>(voice.cursor);
            const float t = static_cast<float>(voice.cursor - static_cast<double>(index));
            auto read = [&](const std::vector<float>& channel) {
                const float a = channel[index];
                const float b = index + 1 < channel.size() ? channel[index + 1] : (voice.loop ? channel[0] : 0.0f);
                return (a + (b - a) * t) * voice.fade;
            };
            float left = read(samples);
            float right = left;
            if (voice.sound->stereo()) {
                right = read(voice.sound->right);
                if (voice.positional) {
                    left = right = 0.5f * (left + right);
                }
            }
            if (voice.lowpass > 0.0f || voice.highpass > 0.0f) {
                left = shape(left, 0);
                right = shape(right, 1);
            }
            output[i * 2] += left * voice.left;
            output[i * 2 + 1] += right * voice.right;
            voice.left += stepLeft;
            voice.right += stepRight;
            voice.cursor += static_cast<double>(voice.pitch);
            if (voice.fadeStep != 0.0f) {
                voice.fade += voice.fadeStep;
                if (voice.fade >= 1.0f) {
                    voice.fade = 1.0f;
                    voice.fadeStep = 0.0f;
                } else if (voice.fade <= 0.0f) {
                    voice.done = true;
                    break;
                }
            }
        }
    }
    std::erase_if(m_voices, [](const Voice& v) { return v.done; });

    constexpr float kKnee = 0.7f;
    for (std::size_t i = 0; i < frames * 2; ++i) {
        const float x = output[i];
        const float magnitude = std::abs(x);
        if (magnitude > kKnee) {
            const float over = kKnee + (1.0f - kKnee) * std::tanh((magnitude - kKnee) / (1.0f - kKnee));
            output[i] = x < 0.0f ? -over : over;
        }
    }
}

}
