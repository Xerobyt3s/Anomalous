#pragma once

#include "engine/audio/sound_bank.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace ghost::engine {
struct Listener {
    glm::vec3 position{0.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
};

struct StereoGain {
    float left = 1.0f;
    float right = 1.0f;
};

StereoGain spatialGain(const Listener& listener, const glm::vec3& source, float minDistance, float maxDistance);

struct PlayParams {
    float volume = 1.0f;
    float pitch = 1.0f;
    std::optional<glm::vec3> position;
    float minDistance = 2.0f;
    float maxDistance = 60.0f;
    bool loop = false;
    int group = 0;
    float delay = 0.0f;
    float fadeIn = 0.0f;
};

using VoiceId = std::uint32_t;

class AudioEngine {
public:
    static constexpr int kGroups = 6;
    static constexpr std::size_t kMaxVoices = 64;

    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool start();
    bool running() const { return m_running; }

    VoiceId play(SoundRef sound, const PlayParams& params = {});

    void set(VoiceId voice, float volume, float pitch, std::optional<glm::vec3> position = std::nullopt);
    void stop(VoiceId voice, float fadeSeconds = 0.05f);
    bool playing(VoiceId voice) const;
    std::size_t voiceCount() const;

    void setListener(const Listener& listener);
    void setMasterVolume(float volume);
    void setGroupVolume(int group, float volume);
    float masterVolume() const { return m_master; }
    float groupVolume(int group) const { return m_groups[static_cast<std::size_t>(group)]; }

    void mix(float* output, std::size_t frames);

private:
    struct Voice {
        VoiceId id = 0;
        SoundRef sound;
        double cursor = 0.0;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool positional = false;
        glm::vec3 position{0.0f};
        float minDistance = 2.0f;
        float maxDistance = 60.0f;
        bool loop = false;
        int group = 0;
        std::size_t delayFrames = 0;
        float fade = 1.0f;
        float fadeStep = 0.0f;
        float left = 0.0f;
        float right = 0.0f;
        bool fresh = true;
        bool done = false;
    };
    struct Device;

    Voice* find(VoiceId voice);

    mutable std::mutex m_mutex;
    std::vector<Voice> m_voices;
    Listener m_listener;
    float m_master = 0.8f;
    std::array<float, kGroups> m_groups{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    VoiceId m_nextId = 1;
    bool m_running = false;
    std::unique_ptr<Device> m_device;
};

}
