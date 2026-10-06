#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <span>
#include <vector>

namespace ghost::game {
class ShardFlock {
public:
    static constexpr int kFull = 280;

    enum class Form {
        Cluster,
        Raven,
        Stoop,
        Cloud,
        Ball
    };

    struct Input {
        glm::vec3 center{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        glm::vec3 up{0.0f, 1.0f, 0.0f};
        Form form = Form::Cloud;
        float size = 1.0f;
        float seed = 0.0f;
        float flare = 0.0f;
    };

    struct Shard {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec3 axis{0.0f, 1.0f, 0.0f};
        float length = 0.2f;
        float width = 0.04f;
        float roll = 0.0f;
        float fade = 1.0f;
        float base = 0.2f;
        float slim = 0.35f;
    };

    void update(float dt, const Input& in);

    void burst(const glm::vec3& from);

    void shatter();

    void takeIn(const ShardFlock& other);

    ShardFlock divide();

    void fall(float dt);

    std::span<const Shard> shards() const { return m_shards; }
    std::span<const Shard> debris() const { return m_debris; }

    struct Trail {
        std::vector<glm::vec3> points;
        float width = 0.1f;
    };
    static constexpr int kTrails = 6;
    std::span<const Trail> trails() const { return m_trails; }
    static int countFor(float size);
    bool empty() const { return m_shards.empty() && m_debris.empty(); }

private:
    struct Slot {
        glm::vec3 position{0.0f};
        glm::vec3 axis{0.0f, 1.0f, 0.0f};
        float length = 1.0f;
    };
    Slot slot(int index, int count, Form form, const Input& in, float scale) const;

    std::vector<Shard> m_shards;
    std::vector<Shard> m_debris;
    glm::vec3 m_forward{0.0f, 0.0f, -1.0f};
    glm::vec3 m_up{0.0f, 1.0f, 0.0f};
    glm::vec3 m_lastCenter{0.0f};
    float m_time = 0.0f;
    float m_flung = 0.0f;
    std::array<Trail, kTrails> m_trails{};
    float m_trailClock = 0.0f;
    int m_trailKind = 0;
    Form m_from = Form::Cloud;
    Form m_to = Form::Cloud;
    float m_change = 10.0f;
    float m_bank = 0.0f;
    float m_flare = 0.0f;
    float m_beat = 0.0f;
    float m_beatDepth = 0.4f;
    bool m_started = false;
};

}
