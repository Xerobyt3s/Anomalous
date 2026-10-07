#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace ghost::game {

struct CarFxTuning {
    float smokeRate = 22.0f;
    float smokeLife = 2.6f;
    float smokeStartRadius = 0.3f;
    float smokeEndRadius = 1.6f;
    float smokeRise = 0.55f;
    float smokeDrag = 1.6f;
    float dustDensity = 0.7f;
    float smokeThickness = 5.0f;
    float steamRate = 0.6f;
    float steamLife = 1.8f;
    float steamRise = 1.2f;
    float steamStartRadius = 0.15f;
    float steamEndRadius = 0.8f;
    float driftAngleStart = 14.0f;
    float driftAngleFull = 30.0f;
    float steeredAngleExtra = 16.0f;
    float spinRatioStart = 0.35f;
    float spinRatioFull = 1.0f;
    float markThreshold = 0.18f;
    float markHalfWidth = 0.08f;
    float markMaxLength = 1.5f;
    float markTurnDeg = 6.0f;
    float markJump = 1.0f;
    float markFadeStart = 45.0f;
    float markLife = 60.0f;
};

CarFxTuning parseCarFxTuning(std::string_view json);

struct WheelSample {
    glm::vec3 contact{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    float slideLat = 0.0f;
    float slideLong = 0.0f;
    float slipAngle = 0.0f;
    float slipRatio = 0.0f;
    bool steered = false;
    bool grounded = false;
    bool road = true;
};

float skidIntensity(const WheelSample& wheel, const CarFxTuning& tuning);

enum class PuffKind : std::uint8_t { Tire, Dust, Steam };

struct CarPuff {
    glm::vec3 center{0.0f};
    glm::vec3 velocity{0.0f};
    float age = 0.0f;
    float life = 1.0f;
    float startRadius = 0.3f;
    float endRadius = 1.0f;
    float density = 1.0f;
    float seed = 0.0f;
    PuffKind kind = PuffKind::Tire;

    float radius() const;
    float opacity() const;
};

class CarSmoke {
public:
    static constexpr std::size_t kMaxPuffs = 64;
    static constexpr std::size_t kWheels = 4;

    void setTuning(const CarFxTuning& tuning) { m_tuning = tuning; }
    void emitWheels(std::span<const WheelSample> wheels, const glm::vec3& carVelocity, const glm::vec3& up, float dt);
    void emitSteam(const glm::vec3& at, float heat, const glm::vec3& up, float dt);
    void update(float dt, const glm::vec3& up, const glm::vec3& wind);
    void clear();
    const std::vector<CarPuff>& puffs() const { return m_puffs; }

private:
    void spawn(const CarPuff& puff);
    float random();

    CarFxTuning m_tuning;
    std::vector<CarPuff> m_puffs;
    float m_wheelDue[kWheels]{};
    float m_steamDue = 0.0f;
    std::uint32_t m_rng = 0x2545F491u;
    std::size_t m_oldest = 0;
};

struct SkidSegment {
    glm::vec3 center{0.0f};
    glm::vec3 along{1.0f, 0.0f, 0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float halfLength = 0.0f;
    float halfWidth = 0.08f;
    float strength = 0.0f;
    float age = 0.0f;
    bool road = true;
};

class SkidMarks {
public:
    static constexpr std::size_t kMaxSegments = 1024;
    static constexpr std::size_t kWheels = 4;
    static constexpr int kStartTicks = 4;

    void setTuning(const CarFxTuning& tuning) { m_tuning = tuning; }
    void sample(std::size_t wheel, const WheelSample& sample);
    void update(float dt);
    void clear();
    const std::vector<SkidSegment>& segments() const { return m_segments; }
    float fade(const SkidSegment& segment) const;

private:
    struct Trail {
        bool active = false;
        int pending = 0;
        std::size_t segment = 0;
        glm::vec3 start{0.0f};
        glm::vec3 last{0.0f};
    };

    std::size_t open(const glm::vec3& at, const WheelSample& sample, float strength);
    void stretch(SkidSegment& segment, const glm::vec3& start, const glm::vec3& end, const WheelSample& sample,
                 float strength);

    CarFxTuning m_tuning;
    std::vector<SkidSegment> m_segments;
    std::size_t m_next = 0;
    Trail m_trails[kWheels];
};

}
