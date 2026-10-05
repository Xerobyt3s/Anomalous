#pragma once

#include "engine/audio/audio_engine.h"
#include "engine/audio/sound_bank.h"
#include "game/ammo/ammo_data.h"
#include "game/events.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <random>
#include <string_view>
#include <vector>

namespace ghost::game {
enum class SoundGroup : int { Weapon, World, Ghosts, Player, Bench, Count };

struct SoundEmitter {
    std::uint32_t id = 0;
    glm::vec3 position{0.0f};
    float level = 1.0f;
    float pitch = 1.0f;
};

struct AudioFrame {
    float dt = 0.0f;
    glm::vec3 listener{0.0f};
    glm::vec3 listenerRight{1.0f, 0.0f, 0.0f};
    bool onFoot = false;
    glm::vec3 feet{0.0f};
    glm::vec3 velocity{0.0f};
    bool grounded = true;
    bool sliding = false;
    bool crouched = false;
    bool crawling = false;
    float health = 1.0f;
    bool hasted = false;
    bool shrouded = false;
    int alignedChamber = 0;
    bool grinding = false;
    std::vector<SoundEmitter> fires;
    std::vector<SoundEmitter> infernos;
    std::vector<SoundEmitter> wisps;
};

class GameAudio {
public:
    explicit GameAudio(const AmmoData& ammo);

    void onEvent(const GameEvent& event);
    void update(const AudioFrame& frame);

    void play(std::string_view name, SoundGroup group, float volume = 1.0f, std::optional<glm::vec3> position = std::nullopt,
              float pitch = 1.0f, float delay = 0.0f);

    void drawDebugUi();
    engine::AudioEngine& engine() { return m_engine; }
    const engine::SoundBank& bank() const { return m_bank; }

private:
    struct Loop {
        std::uint32_t id = 0;
        engine::VoiceId voice = 0;
        bool seen = false;
    };
    void followEmitters(std::vector<Loop>& loops, const std::vector<SoundEmitter>& emitters, std::string_view sound,
                        SoundGroup group, float maxDistance, const glm::vec3& listener);
    void holdLoop(engine::VoiceId& voice, bool on, std::string_view sound, SoundGroup group, float volume, float pitch = 1.0f);
    float secondsAway(const glm::vec3& position) const;

    const AmmoData* m_ammo;
    engine::SoundBank m_bank;
    engine::AudioEngine m_engine;
    std::minstd_rand m_rng{99};
    glm::vec3 m_listener{0.0f};

    std::vector<Loop> m_fireLoops;
    std::vector<Loop> m_infernoLoops;
    std::vector<Loop> m_wispLoops;
    engine::VoiceId m_tailwind = 0;
    engine::VoiceId m_shroud = 0;
    engine::VoiceId m_heartbeat = 0;
    engine::VoiceId m_grind = 0;
    engine::VoiceId m_slide = 0;
    float m_stride = 0.0f;
    bool m_wasGrounded = true;
    float m_fallSpeed = 0.0f;
    int m_lastChamber = -1;
    bool m_muted = false;
    float m_volumeBeforeMute = 0.8f;
};

}
