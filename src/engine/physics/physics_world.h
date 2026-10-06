#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace ghost::engine {
struct RayHit {
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float fraction = 0.0f;
    std::uint64_t userData = 0;
    std::uint32_t body = 0;
    bool dynamic = false;
};

struct CapsuleMove {
    glm::vec3 position{0.0f};
    bool grounded = false;
    std::array<glm::vec3, 4> normals{};
    int normalCount = 0;
};

struct CharacterMove {
    glm::vec3 position{0.0f};
    bool grounded = false;
    bool steep = false;
    bool stepped = false;
    glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
    glm::vec3 groundVelocity{0.0f};
    std::array<glm::vec3, 4> normals{};
    int normalCount = 0;
};

struct BodyState {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    bool active = false;
};

struct RayFilter {
    bool staticOnly = false;
    std::uint32_t ignore = 0xFFFFFFFFu;
};

struct DynamicBodyDesc {
    glm::vec3 center{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 halfExtents{0.5f};
    glm::vec3 shapeOffset{0.0f};
    float mass = 1.0f;
    std::optional<glm::vec3> inertiaDiagonal;
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
    float friction = 0.6f;
    float restitution = 0.15f;
    bool linearCast = false;
    bool allowSleeping = true;
    glm::vec3 velocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    std::uint64_t userData = 0;
    std::vector<glm::vec3> hull;
};

inline constexpr std::uint32_t kNoBody = 0xFFFFFFFFu;
inline constexpr int kMaxCharacters = 8;

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    using BodyHandle = std::uint32_t;
    using GravityField = std::function<glm::vec3(const glm::vec3&)>;

    void addStaticBox(const glm::vec3& center, const glm::vec3& halfExtents, std::uint64_t userData);
    void addStaticBox(const glm::vec3& center, const glm::quat& rotation, const glm::vec3& halfExtents,
                      std::uint64_t userData);
    void addStaticMesh(std::span<const glm::vec3> triangles, std::uint64_t userData);
    void addHeightfield(std::span<const float> samples, std::uint32_t sampleCount, const glm::vec3& offset,
                        const glm::vec3& scale, std::uint32_t blockSize, std::uint64_t userData);
    void clearStatics();
    std::uint32_t staticCount() const;

    BodyHandle addDynamicBox(const glm::vec3& center, const glm::vec3& halfExtents, float mass,
                             std::uint64_t userData);
    BodyHandle addDynamicBox(const glm::vec3& center, const glm::quat& rotation, const glm::vec3& halfExtents, float mass,
                             std::uint64_t userData, const glm::vec3& velocity);
    BodyHandle addDynamicBody(const DynamicBodyDesc& desc);
    void removeBody(BodyHandle body);
    void addForce(BodyHandle body, const glm::vec3& force);
    void addForceAtPoint(BodyHandle body, const glm::vec3& force, const glm::vec3& worldPoint);
    void addTorque(BodyHandle body, const glm::vec3& torque);
    void setVelocities(BodyHandle body, const glm::vec3& velocity, const glm::vec3& angularVelocity);
    void activate(BodyHandle body);
    bool isActive(BodyHandle body) const;
    bool valid(BodyHandle body) const;
    std::uint32_t dynamicCount() const;
    BodyState state(BodyHandle body) const;
    void setState(BodyHandle body, const BodyState& state);
    void addImpulse(BodyHandle body, const glm::vec3& impulse, const glm::vec3& atPoint);
    void pose(BodyHandle body, glm::vec3& position, glm::quat& rotation) const;
    glm::vec3 linearVelocity(BodyHandle body) const;
    void setLinearVelocity(BodyHandle body, const glm::vec3& velocity);

    void setPose(BodyHandle body, const glm::vec3& position, const glm::quat& rotation);

    void setGravityField(GravityField field);
    void wakeAll();

    void optimize();

    std::optional<RayHit> raycast(const glm::vec3& from, const glm::vec3& to) const;
    std::optional<RayHit> raycast(const glm::vec3& from, const glm::vec3& to, const RayFilter& filter) const;

    CapsuleMove moveCapsule(const glm::vec3& feet, const glm::vec3& velocity, float dt, float radius, float height);

    void characterCreate(int id, const glm::vec3& feet, const glm::vec3& up, float radius, float height);
    void characterDestroy(int id);
    bool characterValid(int id) const;
    void characterSetSolid(int id, bool solid);
    bool characterSolid(int id) const;
    void characterTeleport(int id, const glm::vec3& feet, const glm::vec3& up);
    bool characterSetHeight(int id, float height, const glm::vec3& up);
    bool characterFits(int id, const glm::vec3& feet, const glm::vec3& up, float height) const;
    CharacterMove characterMove(int id, const glm::vec3& velocity, const glm::vec3& up, const glm::vec3& gravity, float dt,
                                bool stick);
    glm::vec3 characterPosition(int id) const;
    float characterHeight(int id) const;
    float characterRadius(int id) const;

    void step(float dt);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
