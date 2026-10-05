#include "engine/physics/physics_world.h"
#include "engine/physics/jolt_runtime.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseQuery.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cfloat>
#include <mutex>
#include <thread>

namespace ghost::engine {
namespace {
namespace Layers {
constexpr JPH::ObjectLayer kStatic = 0;
constexpr JPH::ObjectLayer kMoving = 1;
}

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer kStatic(0);
constexpr JPH::BroadPhaseLayer kMoving(1);
}

class BroadPhaseLayerMap final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer == Layers::kStatic ? BroadPhaseLayers::kStatic : BroadPhaseLayers::kMoving;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == BroadPhaseLayers::kStatic ? "static" : "moving";
    }
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer object, JPH::BroadPhaseLayer broadPhase) const override {
        return object == Layers::kMoving || broadPhase == BroadPhaseLayers::kMoving;
    }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a == Layers::kMoving || b == Layers::kMoving;
    }
};

class StaticOnlyFilter final : public JPH::BodyFilter {
public:
    explicit StaticOnlyFilter(bool staticOnly, JPH::BodyID ignore) : m_staticOnly(staticOnly), m_ignore(ignore) {}
    bool ShouldCollideLocked(const JPH::Body& body) const override {
        if (body.GetID() == m_ignore) {
            return false;
        }
        return !m_staticOnly || body.IsStatic();
    }

private:
    bool m_staticOnly;
    JPH::BodyID m_ignore;
};

constexpr float kMaxSlopeDeg = 50.0f;
constexpr float kStepUp = 0.35f;
constexpr float kStickDown = 0.45f;
constexpr float kStairMinForward = 0.08f;
constexpr float kStairForwardTest = 0.4f;
constexpr float kStepMinGain = 0.03f;
constexpr float kStairSweepStep = 0.1f;
constexpr float kStickMaxRise = 0.5f;
constexpr float kStepBlockUp = 0.64f;
constexpr float kStepBlockInto = 0.1f;
constexpr float kFitsSlack = 0.02f;
constexpr float kVehicleReach = 0.5f;
constexpr float kDefaultGravity = 9.81f;

JPH::Vec3 toJolt(const glm::vec3& v) { return {v.x, v.y, v.z}; }
JPH::RVec3 toJoltR(const glm::vec3& v) { return JPH::RVec3(v.x, v.y, v.z); }
JPH::Quat toJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w).Normalized(); }
glm::vec3 toGlm(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
glm::vec3 toGlmR(JPH::RVec3Arg v) {
    return {static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())};
}
glm::quat toGlm(JPH::QuatArg q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

JPH::Quat upRotation(const glm::vec3& up) {
    const glm::vec3 n = glm::normalize(up);
    const glm::vec3 y{0.0f, 1.0f, 0.0f};
    const float d = glm::dot(y, n);
    if (d > 0.99999f) {
        return JPH::Quat::sIdentity();
    }
    if (d < -0.99999f) {
        return JPH::Quat::sRotation(JPH::Vec3::sAxisX(), JPH::JPH_PI);
    }
    const glm::vec3 axis = glm::normalize(glm::cross(y, n));
    return JPH::Quat::sRotation(toJolt(axis), std::acos(std::clamp(d, -1.0f, 1.0f)));
}

float boxConvexRadius(const glm::vec3& half) {
    return std::min(JPH::cDefaultConvexRadius, 0.5f * std::min({half.x, half.y, half.z}));
}

JPH::RefConst<JPH::Shape> makeCapsule(float radius, float height) {
    const float halfCylinder = std::max(0.5f * height - radius, 0.01f);
    const float centre = halfCylinder + radius;
    JPH::RotatedTranslatedShapeSettings settings(JPH::Vec3(0.0f, centre, 0.0f), JPH::Quat::sIdentity(),
                                                 new JPH::CapsuleShape(halfCylinder, radius));
    JPH::ShapeSettings::ShapeResult result = settings.Create();
    if (result.HasError()) {
        return nullptr;
    }
    return result.Get();
}

class VehicleContacts final : public JPH::ContactListener {
public:
    void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold,
                        JPH::ContactSettings&) override {
        note(a, b, manifold);
    }
    void OnContactPersisted(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold,
                            JPH::ContactSettings&) override {
        note(a, b, manifold);
    }

    JPH::BodyID vehicle;
    std::mutex mutex;
    std::vector<std::pair<JPH::BodyID, glm::vec3>> touched;

private:
    void note(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold) {
        if (vehicle.IsInvalid() || manifold.mRelativeContactPointsOn1.empty()) {
            return;
        }
        const JPH::Body* other = a.GetID() == vehicle ? &b : (b.GetID() == vehicle ? &a : nullptr);
        if (!other || !other->IsDynamic()) {
            return;
        }
        const glm::vec3 point = toGlmR(manifold.GetWorldSpaceContactPointOn1(0));
        std::lock_guard<std::mutex> lock(mutex);
        touched.emplace_back(other->GetID(), point);
    }
};

}

struct PhysicsWorld::Impl {
    BroadPhaseLayerMap broadPhaseLayers;
    ObjectVsBroadPhaseFilter objectVsBroadPhase;
    ObjectPairFilter objectPairs;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
    JPH::PhysicsSystem system;
    VehicleContacts vehicleContacts;

    struct Character {
        JPH::Ref<JPH::CharacterVirtual> body;
        float radius = 0.3f;
        float height = 1.6f;
        bool solid = false;
    };
    std::array<Character, kMaxCharacters> characters{};
    JPH::CharacterVsCharacterCollisionSimple others;

    std::vector<JPH::BodyID> statics;
    std::vector<JPH::BodyID> dynamics;
    JPH::BodyID vehicle;
    glm::vec3 vehicleHalf{0.0f};
    glm::vec3 vehicleOffset{0.0f};
    std::vector<VehiclePush> pushes;
    GravityField gravity;

    JPH::BodyInterface& bodies() { return system.GetBodyInterface(); }
    const JPH::BodyInterface& bodies() const { return system.GetBodyInterface(); }

    JPH::DefaultBroadPhaseLayerFilter broadPhaseFilter() const {
        return system.GetDefaultBroadPhaseLayerFilter(Layers::kMoving);
    }
    JPH::DefaultObjectLayerFilter objectFilter() const { return system.GetDefaultLayerFilter(Layers::kMoving); }

    glm::vec3 gravityAt(const glm::vec3& p) const {
        return gravity ? gravity(p) : glm::vec3(0.0f, -kDefaultGravity, 0.0f);
    }

    Character* find(int id) {
        return id >= 0 && id < kMaxCharacters && characters[static_cast<std::size_t>(id)].body
                   ? &characters[static_cast<std::size_t>(id)]
                   : nullptr;
    }
    const Character* find(int id) const {
        return id >= 0 && id < kMaxCharacters && characters[static_cast<std::size_t>(id)].body
                   ? &characters[static_cast<std::size_t>(id)]
                   : nullptr;
    }

    void setSolid(Character& ch, bool solid) {
        if (ch.solid == solid) {
            return;
        }
        ch.solid = solid;
        if (solid) {
            others.Add(ch.body.GetPtr());
        } else {
            others.Remove(ch.body.GetPtr());
        }
    }

    void release(int id) {
        Character& ch = characters[static_cast<std::size_t>(id)];
        if (ch.body) {
            setSolid(ch, false);
        }
        ch = Character{};
    }

    void addStatic(const JPH::ShapeSettings& settings, const glm::vec3& position, const glm::quat& rotation,
                   std::uint64_t userData) {
        JPH::ShapeSettings::ShapeResult result = settings.Create();
        if (result.HasError()) {
            return;
        }
        JPH::BodyCreationSettings body(result.Get(), toJoltR(position), toJolt(rotation), JPH::EMotionType::Static,
                                       Layers::kStatic);
        body.mUserData = userData;
        const JPH::BodyID id = bodies().CreateAndAddBody(body, JPH::EActivation::DontActivate);
        if (!id.IsInvalid()) {
            statics.push_back(id);
        }
    }

    bool clearAt(const JPH::Shape* shape, JPH::RVec3Arg pos, JPH::QuatArg rot) const {
        const JPH::RMat44 transform = JPH::RMat44::sRotationTranslation(rot, pos).PreTranslated(shape->GetCenterOfMass());
        JPH::CollideShapeSettings settings;
        settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        system.GetNarrowPhaseQuery().CollideShape(shape, JPH::Vec3::sReplicate(1.0f), transform, settings,
                                                  transform.GetTranslation(), collector, broadPhaseFilter(),
                                                  objectFilter());
        for (const JPH::CollideShapeResult& hit : collector.mHits) {
            if (hit.mPenetrationDepth > kFitsSlack) {
                return false;
            }
        }
        return true;
    }

    bool land(JPH::CharacterVirtual& c, JPH::RVec3Arg from, JPH::Vec3Arg up, float drop, bool flat) {
        c.SetPosition(from);
        if (!c.StickToFloor(up * -drop, broadPhaseFilter(), objectFilter(), {}, {}, *tempAllocator)) {
            return false;
        }
        return !flat || c.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
    }

    bool stepUp(JPH::CharacterVirtual& c, JPH::Vec3Arg up, JPH::Vec3Arg dir, float forward) {
        const JPH::RVec3 start = c.GetPosition();
        const JPH::Quat rot = c.GetRotation();
        const JPH::Shape* shape = c.GetShape();
        const JPH::RVec3 raised = start + up * kStepUp;
        const JPH::RVec3 probe = raised + dir * kStairForwardTest;
        const JPH::RVec3 ahead = raised + dir * forward;
        const auto restore = [&] {
            c.SetPosition(start);
            c.RefreshContacts(broadPhaseFilter(), objectFilter(), {}, {}, *tempAllocator);
            return false;
        };
        for (float along = 0.0f; along <= kStairForwardTest + 1e-4f; along += kStairSweepStep) {
            if (!clearAt(shape, raised + dir * along + up * kFitsSlack, rot)) {
                return false;
            }
        }
        if (!land(c, probe, up, kStepUp + kFitsSlack, true)) {
            return restore();
        }
        const float gain = JPH::Vec3(c.GetPosition() - start).Dot(up);
        if (gain < kStepMinGain) {
            return restore();
        }
        if (!clearAt(shape, ahead + up * kFitsSlack, rot) || !land(c, ahead, up, kStepUp + kFitsSlack, false) ||
            JPH::Vec3(c.GetPosition() - start).Dot(up) <= 0.0f) {
            return restore();
        }
        return true;
    }
};

PhysicsWorld::PhysicsWorld() {
    acquireJolt();

    m_impl = std::make_unique<Impl>();
    m_impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
    const int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    m_impl->jobSystem =
        std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);

    constexpr JPH::uint kMaxBodies = 8192;
    constexpr JPH::uint kMaxBodyPairs = 8192;
    constexpr JPH::uint kMaxContactConstraints = 4096;
    m_impl->system.Init(kMaxBodies, 0, kMaxBodyPairs, kMaxContactConstraints, m_impl->broadPhaseLayers,
                        m_impl->objectVsBroadPhase, m_impl->objectPairs);
    m_impl->system.SetContactListener(&m_impl->vehicleContacts);
}

PhysicsWorld::~PhysicsWorld() {
    for (int id = 0; id < kMaxCharacters; ++id) {
        m_impl->release(id);
    }
    m_impl.reset();
    releaseJolt();
}

void PhysicsWorld::addStaticBox(const glm::vec3& center, const glm::vec3& halfExtents, std::uint64_t userData) {
    addStaticBox(center, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), halfExtents, userData);
}

void PhysicsWorld::addStaticBox(const glm::vec3& center, const glm::quat& rotation, const glm::vec3& halfExtents,
                                std::uint64_t userData) {
    JPH::BoxShapeSettings settings(toJolt(halfExtents), boxConvexRadius(halfExtents));
    m_impl->addStatic(settings, center, rotation, userData);
}

void PhysicsWorld::addStaticMesh(std::span<const glm::vec3> triangles, std::uint64_t userData) {
    const std::size_t count = triangles.size() / 3;
    JPH::TriangleList list;
    list.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const glm::vec3& a = triangles[i * 3 + 0];
        const glm::vec3& b = triangles[i * 3 + 1];
        const glm::vec3& c = triangles[i * 3 + 2];
        const glm::vec3 n = glm::cross(b - a, c - a);
        if (glm::dot(n, n) < 1e-10f) {
            continue;
        }
        list.push_back(JPH::Triangle(JPH::Float3(a.x, a.y, a.z), JPH::Float3(b.x, b.y, b.z), JPH::Float3(c.x, c.y, c.z)));
    }
    if (list.empty()) {
        return;
    }
    JPH::MeshShapeSettings settings(list);
    m_impl->addStatic(settings, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), userData);
}

void PhysicsWorld::addHeightfield(std::span<const float> samples, std::uint32_t sampleCount, const glm::vec3& offset,
                                  const glm::vec3& scale, std::uint32_t blockSize, std::uint64_t userData) {
    if (sampleCount == 0 || samples.size() < static_cast<std::size_t>(sampleCount) * sampleCount) {
        return;
    }
    JPH::HeightFieldShapeSettings settings(samples.data(), toJolt(offset), toJolt(scale), sampleCount);
    settings.mBlockSize = blockSize;
    settings.mBitsPerSample = settings.CalculateBitsPerSampleForError(0.005f);
    m_impl->addStatic(settings, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), userData);
}

void PhysicsWorld::clearStatics() {
    JPH::BodyInterface& bodies = m_impl->bodies();
    for (const JPH::BodyID id : m_impl->statics) {
        bodies.RemoveBody(id);
        bodies.DestroyBody(id);
    }
    m_impl->statics.clear();
    for (const JPH::BodyID id : m_impl->dynamics) {
        bodies.ActivateBody(id);
    }
}

std::uint32_t PhysicsWorld::staticCount() const { return static_cast<std::uint32_t>(m_impl->statics.size()); }

PhysicsWorld::BodyHandle PhysicsWorld::addDynamicBox(const glm::vec3& center, const glm::vec3& halfExtents, float mass,
                                                     std::uint64_t userData) {
    return addDynamicBox(center, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), halfExtents, mass, userData, glm::vec3(0.0f));
}

PhysicsWorld::BodyHandle PhysicsWorld::addDynamicBox(const glm::vec3& center, const glm::quat& rotation,
                                                     const glm::vec3& halfExtents, float mass, std::uint64_t userData,
                                                     const glm::vec3& velocity) {
    JPH::BodyCreationSettings settings(new JPH::BoxShape(toJolt(halfExtents), boxConvexRadius(halfExtents)),
                                       toJoltR(center), toJolt(rotation), JPH::EMotionType::Dynamic, Layers::kMoving);
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = mass;
    settings.mUserData = userData;
    settings.mLinearVelocity = toJolt(velocity);
    settings.mFriction = 0.6f;
    settings.mRestitution = 0.15f;
    if (m_impl->gravity) {
        settings.mGravityFactor = 0.0f;
    }
    const JPH::BodyID id = m_impl->bodies().CreateAndAddBody(settings, JPH::EActivation::Activate);
    if (id.IsInvalid()) {
        return kNoBody;
    }
    m_impl->dynamics.push_back(id);
    return id.GetIndexAndSequenceNumber();
}

void PhysicsWorld::removeBody(BodyHandle body) {
    if (!valid(body)) {
        return;
    }
    const JPH::BodyID id(body);
    JPH::BodyInterface& bodies = m_impl->bodies();
    bodies.RemoveBody(id);
    bodies.DestroyBody(id);
    std::erase(m_impl->dynamics, id);
}

bool PhysicsWorld::valid(BodyHandle body) const {
    return body != kNoBody && m_impl->bodies().IsAdded(JPH::BodyID(body));
}

std::uint32_t PhysicsWorld::dynamicCount() const { return static_cast<std::uint32_t>(m_impl->dynamics.size()); }

BodyState PhysicsWorld::state(BodyHandle body) const {
    BodyState out;
    if (body == kNoBody) {
        return out;
    }
    JPH::BodyLockRead lock(m_impl->system.GetBodyLockInterface(), JPH::BodyID(body));
    if (!lock.Succeeded()) {
        return out;
    }
    const JPH::Body& b = lock.GetBody();
    out.position = toGlmR(b.GetPosition());
    out.rotation = toGlm(b.GetRotation());
    out.velocity = toGlm(b.GetLinearVelocity());
    out.angularVelocity = toGlm(b.GetAngularVelocity());
    out.active = b.IsActive();
    return out;
}

void PhysicsWorld::setState(BodyHandle body, const BodyState& state) {
    if (!valid(body)) {
        return;
    }
    m_impl->bodies().SetPositionRotationAndVelocity(JPH::BodyID(body), toJoltR(state.position), toJolt(state.rotation),
                                                    toJolt(state.velocity), toJolt(state.angularVelocity));
    if (state.active) {
        m_impl->bodies().ActivateBody(JPH::BodyID(body));
    }
}

void PhysicsWorld::addImpulse(BodyHandle body, const glm::vec3& impulse, const glm::vec3& atPoint) {
    JPH::BodyInterface& bodies = m_impl->bodies();
    const JPH::BodyID id(body);
    bodies.ActivateBody(id);
    bodies.AddImpulse(id, toJolt(impulse), JPH::RVec3(atPoint.x, atPoint.y, atPoint.z));
}

void PhysicsWorld::pose(BodyHandle body, glm::vec3& position, glm::quat& rotation) const {
    JPH::RVec3 p;
    JPH::Quat q;
    m_impl->bodies().GetPositionAndRotation(JPH::BodyID(body), p, q);
    position = toGlmR(p);
    rotation = toGlm(q);
}

glm::vec3 PhysicsWorld::linearVelocity(BodyHandle body) const {
    return toGlm(m_impl->bodies().GetLinearVelocity(JPH::BodyID(body)));
}

void PhysicsWorld::setLinearVelocity(BodyHandle body, const glm::vec3& velocity) {
    JPH::BodyInterface& bodies = m_impl->bodies();
    bodies.ActivateBody(JPH::BodyID(body));
    bodies.SetLinearVelocity(JPH::BodyID(body), toJolt(velocity));
}

void PhysicsWorld::setPose(BodyHandle body, const glm::vec3& position, const glm::quat& rotation) {
    m_impl->bodies().SetPositionAndRotation(JPH::BodyID(body), toJoltR(position), toJolt(rotation),
                                            JPH::EActivation::Activate);
}

void PhysicsWorld::setVehicle(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& halfExtents,
                              const glm::vec3& offset, const glm::vec3& velocity, const glm::vec3& angularVelocity,
                              std::uint64_t userData) {
    JPH::BodyInterface& bodies = m_impl->bodies();
    const glm::vec3 dh = halfExtents - m_impl->vehicleHalf;
    const glm::vec3 doff = offset - m_impl->vehicleOffset;
    if (!m_impl->vehicle.IsInvalid() && (glm::dot(dh, dh) > 1e-8f || glm::dot(doff, doff) > 1e-8f)) {
        clearVehicle();
    }
    if (m_impl->vehicle.IsInvalid()) {
        JPH::RotatedTranslatedShapeSettings shape(toJolt(offset), JPH::Quat::sIdentity(),
                                                  new JPH::BoxShape(toJolt(halfExtents), boxConvexRadius(halfExtents)));
        JPH::ShapeSettings::ShapeResult result = shape.Create();
        if (result.HasError()) {
            return;
        }
        JPH::BodyCreationSettings body(result.Get(), toJoltR(position), toJolt(rotation), JPH::EMotionType::Kinematic,
                                       Layers::kMoving);
        body.mUserData = userData;
        m_impl->vehicle = bodies.CreateAndAddBody(body, JPH::EActivation::Activate);
        m_impl->vehicleHalf = halfExtents;
        m_impl->vehicleOffset = offset;
        m_impl->vehicleContacts.vehicle = m_impl->vehicle;
    }
    if (m_impl->vehicle.IsInvalid()) {
        return;
    }
    bodies.SetPositionAndRotation(m_impl->vehicle, toJoltR(position), toJolt(rotation), JPH::EActivation::Activate);
    bodies.SetLinearAndAngularVelocity(m_impl->vehicle, toJolt(velocity), toJolt(angularVelocity));
}

void PhysicsWorld::moveVehicle(const glm::vec3& position, const glm::quat& rotation, float dt) {
    if (m_impl->vehicle.IsInvalid() || dt <= 0.0f) {
        return;
    }
    m_impl->bodies().MoveKinematic(m_impl->vehicle, toJoltR(position), toJolt(rotation), dt);
}

void PhysicsWorld::clearVehicle() {
    if (m_impl->vehicle.IsInvalid()) {
        return;
    }
    JPH::BodyInterface& bodies = m_impl->bodies();
    bodies.RemoveBody(m_impl->vehicle);
    bodies.DestroyBody(m_impl->vehicle);
    m_impl->vehicle = JPH::BodyID();
    m_impl->vehicleContacts.vehicle = JPH::BodyID();
}

PhysicsWorld::BodyHandle PhysicsWorld::vehicle() const {
    return m_impl->vehicle.IsInvalid() ? kNoBody : m_impl->vehicle.GetIndexAndSequenceNumber();
}

std::vector<VehiclePush> PhysicsWorld::takeVehiclePushes() {
    std::vector<VehiclePush> out;
    out.swap(m_impl->pushes);
    return out;
}

void PhysicsWorld::setGravityField(GravityField field) {
    m_impl->gravity = std::move(field);
    m_impl->system.SetGravity(m_impl->gravity ? JPH::Vec3::sZero() : JPH::Vec3(0.0f, -kDefaultGravity, 0.0f));
    JPH::BodyInterface& bodies = m_impl->bodies();
    for (const JPH::BodyID id : m_impl->dynamics) {
        bodies.SetGravityFactor(id, m_impl->gravity ? 0.0f : 1.0f);
    }
    wakeAll();
}

void PhysicsWorld::wakeAll() {
    JPH::BodyInterface& bodies = m_impl->bodies();
    for (const JPH::BodyID id : m_impl->dynamics) {
        bodies.ActivateBody(id);
    }
}

void PhysicsWorld::optimize() { m_impl->system.OptimizeBroadPhase(); }

std::optional<RayHit> PhysicsWorld::raycast(const glm::vec3& from, const glm::vec3& to) const {
    return raycast(from, to, RayFilter{});
}

std::optional<RayHit> PhysicsWorld::raycast(const glm::vec3& from, const glm::vec3& to, const RayFilter& filter) const {
    const JPH::RRayCast ray{JPH::RVec3(from.x, from.y, from.z), toJolt(to - from)};
    JPH::RayCastResult result;
    const StaticOnlyFilter bodyFilter(filter.staticOnly, filter.ignore == kNoBody ? JPH::BodyID() : JPH::BodyID(filter.ignore));
    if (!m_impl->system.GetNarrowPhaseQuery().CastRay(ray, result, {}, {}, bodyFilter)) {
        return std::nullopt;
    }

    RayHit hit;
    hit.fraction = result.mFraction;
    const JPH::RVec3 point = ray.GetPointOnRay(result.mFraction);
    hit.point = toGlmR(point);

    JPH::BodyLockRead lock(m_impl->system.GetBodyLockInterface(), result.mBodyID);
    if (lock.Succeeded()) {
        const JPH::Body& body = lock.GetBody();
        hit.normal = toGlm(body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point));
        hit.userData = body.GetUserData();
        hit.body = result.mBodyID.GetIndexAndSequenceNumber();
        hit.dynamic = body.IsDynamic();
    }
    return hit;
}

CapsuleMove PhysicsWorld::moveCapsule(const glm::vec3& feet, const glm::vec3& velocity, float dt, float radius,
                                      float height) {
    const float halfCylinder = std::max(0.5f * height - radius, 0.01f);
    JPH::CapsuleShape capsule(halfCylinder, radius);
    capsule.SetEmbedded();
    const glm::vec3 toCenter{0.0f, 0.5f * height, 0.0f};

    JPH::CollideShapeSettings settings;
    settings.mMaxSeparationDistance = 0.02f;
    JPH::BodyInterface& bodies = m_impl->bodies();

    CapsuleMove result;
    glm::vec3 center = feet + velocity * dt + toCenter;
    auto note = [&result](const glm::vec3& normal) {
        for (int i = 0; i < result.normalCount; ++i) {
            if (glm::dot(result.normals[static_cast<std::size_t>(i)], normal) > 0.99f) {
                return;
            }
        }
        if (result.normalCount < static_cast<int>(result.normals.size())) {
            result.normals[static_cast<std::size_t>(result.normalCount++)] = normal;
        }
    };

    for (int iteration = 0; iteration < 5; ++iteration) {
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        const JPH::RVec3 at(center.x, center.y, center.z);
        m_impl->system.GetNarrowPhaseQuery().CollideShape(&capsule, JPH::Vec3::sReplicate(1.0f),
                                                          JPH::RMat44::sTranslation(at), settings, at, collector);
        float deepest = 0.0f;
        glm::vec3 deepestNormal{0.0f};
        for (const JPH::CollideShapeResult& hit : collector.mHits) {
            if (hit.mPenetrationAxis.LengthSq() < 1e-12f) {
                continue;
            }
            const glm::vec3 normal = -toGlm(hit.mPenetrationAxis.Normalized());
            note(normal);
            if (normal.y > 0.7f) {
                result.grounded = true;
            }

            if (bodies.GetMotionType(hit.mBodyID2) == JPH::EMotionType::Dynamic && normal.y < 0.5f) {
                glm::vec3 push{-normal.x, 0.0f, -normal.z};
                const float pushLength = glm::length(push);
                const float approach = glm::dot(velocity, -normal);
                if (pushLength > 1e-3f && approach > 0.0f) {
                    push /= pushLength;
                    float inverseMass = 0.0f;
                    {
                        JPH::BodyLockRead lock(m_impl->system.GetBodyLockInterface(), hit.mBodyID2);
                        if (lock.Succeeded()) {
                            inverseMass = lock.GetBody().GetMotionProperties()->GetInverseMass();
                        }
                    }
                    const float give = std::clamp(12.0f * inverseMass, 0.15f, 1.0f);
                    const float along = glm::dot(toGlm(bodies.GetLinearVelocity(hit.mBodyID2)), push);
                    if (approach * give > along) {
                        bodies.ActivateBody(hit.mBodyID2);
                        bodies.AddLinearVelocity(hit.mBodyID2, toJolt(push * (approach * give - along)));
                    }
                }
            }
            if (hit.mPenetrationDepth > deepest) {
                deepest = hit.mPenetrationDepth;
                deepestNormal = normal;
            }
        }
        if (deepest <= 1e-4f) {
            break;
        }
        center += deepestNormal * deepest;
    }
    result.position = center - toCenter;
    return result;
}

void PhysicsWorld::characterCreate(int id, const glm::vec3& feet, const glm::vec3& up, float radius, float height) {
    if (id < 0 || id >= kMaxCharacters) {
        return;
    }
    m_impl->release(id);
    JPH::RefConst<JPH::Shape> shape = makeCapsule(radius, height);
    if (!shape) {
        return;
    }
    JPH::CharacterVirtualSettings settings;
    settings.mShape = shape;
    settings.mUp = toJolt(glm::normalize(up));
    settings.mMaxSlopeAngle = JPH::DegreesToRadians(kMaxSlopeDeg);
    settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);
    settings.mMass = 70.0f;
    settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
    settings.mEnhancedInternalEdgeRemoval = true;
    Impl::Character& ch = m_impl->characters[static_cast<std::size_t>(id)];
    ch.radius = radius;
    ch.height = height;
    ch.body = new JPH::CharacterVirtual(&settings, toJoltR(feet), upRotation(up), static_cast<JPH::uint64>(id),
                                        &m_impl->system);
    ch.body->SetCharacterVsCharacterCollision(&m_impl->others);
    m_impl->setSolid(ch, true);
}

void PhysicsWorld::characterDestroy(int id) {
    if (id >= 0 && id < kMaxCharacters) {
        m_impl->release(id);
    }
}

bool PhysicsWorld::characterValid(int id) const { return m_impl->find(id) != nullptr; }

void PhysicsWorld::characterSetSolid(int id, bool solid) {
    if (Impl::Character* ch = m_impl->find(id)) {
        m_impl->setSolid(*ch, solid);
    }
}

bool PhysicsWorld::characterSolid(int id) const {
    const Impl::Character* ch = m_impl->find(id);
    return ch && ch->solid;
}

void PhysicsWorld::characterTeleport(int id, const glm::vec3& feet, const glm::vec3& up) {
    Impl::Character* ch = m_impl->find(id);
    if (!ch) {
        return;
    }
    JPH::CharacterVirtual& c = *ch->body;
    c.SetUp(toJolt(glm::normalize(up)));
    c.SetRotation(upRotation(up));
    c.SetPosition(toJoltR(feet));
    c.SetLinearVelocity(JPH::Vec3::sZero());
    c.RefreshContacts(m_impl->broadPhaseFilter(), m_impl->objectFilter(), {}, {}, *m_impl->tempAllocator);
}

bool PhysicsWorld::characterSetHeight(int id, float height, const glm::vec3& up) {
    Impl::Character* ch = m_impl->find(id);
    if (!ch) {
        return false;
    }
    if (std::abs(height - ch->height) < 1e-4f) {
        return true;
    }
    JPH::RefConst<JPH::Shape> shape = makeCapsule(ch->radius, height);
    if (!shape) {
        return false;
    }
    JPH::CharacterVirtual& c = *ch->body;
    c.SetUp(toJolt(glm::normalize(up)));
    c.SetRotation(upRotation(up));
    const float maxPenetration = height > ch->height ? kFitsSlack : FLT_MAX;
    if (!c.SetShape(shape, maxPenetration, m_impl->broadPhaseFilter(), m_impl->objectFilter(), {}, {},
                    *m_impl->tempAllocator)) {
        return false;
    }
    ch->height = height;
    return true;
}

bool PhysicsWorld::characterFits(int id, const glm::vec3& feet, const glm::vec3& up, float height) const {
    const Impl::Character* ch = m_impl->find(id);
    JPH::RefConst<JPH::Shape> shape = makeCapsule(ch ? ch->radius : 0.3f, height);
    if (!shape) {
        return true;
    }
    const JPH::RMat44 transform =
        JPH::RMat44::sRotationTranslation(upRotation(up), toJoltR(feet + glm::normalize(up) * 0.02f))
            .PreTranslated(shape->GetCenterOfMass());
    JPH::CollideShapeSettings settings;
    settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    m_impl->system.GetNarrowPhaseQuery().CollideShape(shape, JPH::Vec3::sReplicate(1.0f), transform, settings,
                                                      transform.GetTranslation(), collector, m_impl->broadPhaseFilter(),
                                                      m_impl->objectFilter());
    for (const JPH::CollideShapeResult& hit : collector.mHits) {
        if (hit.mPenetrationDepth > kFitsSlack) {
            return false;
        }
    }
    return true;
}

CharacterMove PhysicsWorld::characterMove(int id, const glm::vec3& velocity, const glm::vec3& up,
                                          const glm::vec3& gravity, float dt, bool stick) {
    CharacterMove out;
    Impl::Character* ch = m_impl->find(id);
    if (!ch) {
        return out;
    }
    JPH::CharacterVirtual& c = *ch->body;
    const glm::vec3 nUp = glm::normalize(up);
    c.SetUp(toJolt(nUp));
    c.SetRotation(upRotation(nUp));
    c.SetLinearVelocity(toJolt(velocity));

    const JPH::DefaultBroadPhaseLayerFilter bpFilter = m_impl->broadPhaseFilter();
    const JPH::DefaultObjectLayerFilter layerFilter = m_impl->objectFilter();
    const JPH::BodyFilter bodyFilter;
    const JPH::ShapeFilter shapeFilter;
    const JPH::Vec3 jUp = toJolt(nUp);

    c.StartTrackingContactChanges();
    const JPH::Vec3 desired = toJolt(velocity);
    c.SetLinearVelocity(c.CancelVelocityTowardsSteepSlopes(desired));
    const JPH::RVec3 oldPosition = c.GetPosition();
    c.Update(dt, toJolt(gravity), bpFilter, layerFilter, bodyFilter, shapeFilter, *m_impl->tempAllocator);

    if (stick && !c.IsSupported()) {
        const float rise = JPH::Vec3(c.GetPosition() - oldPosition).Dot(jUp) / dt;
        if (rise <= kStickMaxRise) {
            c.StickToFloor(jUp * -kStickDown, bpFilter, layerFilter, bodyFilter, shapeFilter, *m_impl->tempAllocator);
        }
    }

    bool stepped = false;
    JPH::Vec3 want = desired * dt;
    want -= want.Dot(jUp) * jUp;
    const float wantLength = want.Length();
    if (wantLength > 1e-5f && c.IsSupported()) {
        const JPH::Vec3 dir = want / wantLength;
        const float got = std::max(JPH::Vec3(c.GetPosition() - oldPosition).Dot(dir), 0.0f);
        bool blocked = false;
        for (const JPH::CharacterVirtual::Contact& contact : c.GetActiveContacts()) {
            if (contact.mHadCollision && contact.mContactNormal.Dot(jUp) < kStepBlockUp &&
                contact.mContactNormal.Dot(dir) < -kStepBlockInto) {
                blocked = true;
                break;
            }
        }
        if (blocked && got + 1e-4f < wantLength) {
            stepped = m_impl->stepUp(c, jUp, dir, std::max(kStairMinForward, wantLength - got));
        }
    }
    c.FinishTrackingContactChanges();

    out.position = toGlmR(c.GetPosition());
    const JPH::CharacterBase::EGroundState ground = c.GetGroundState();
    out.stepped = stepped;
    out.grounded = stepped || ground == JPH::CharacterBase::EGroundState::OnGround;
    out.steep = ground == JPH::CharacterBase::EGroundState::OnSteepGround;
    out.groundNormal = toGlm(c.GetGroundNormal());
    out.groundVelocity = toGlm(c.GetGroundVelocity());
    for (const JPH::CharacterVirtual::Contact& contact : c.GetActiveContacts()) {
        if (!contact.mHadCollision || out.normalCount >= static_cast<int>(out.normals.size())) {
            continue;
        }
        const glm::vec3 n = toGlm(contact.mContactNormal);
        bool seen = false;
        for (int i = 0; i < out.normalCount; ++i) {
            if (glm::dot(out.normals[static_cast<std::size_t>(i)], n) > 0.99f) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            out.normals[static_cast<std::size_t>(out.normalCount++)] = n;
        }
    }
    return out;
}

glm::vec3 PhysicsWorld::characterPosition(int id) const {
    const Impl::Character* ch = m_impl->find(id);
    return ch ? toGlmR(ch->body->GetPosition()) : glm::vec3(0.0f);
}

float PhysicsWorld::characterHeight(int id) const {
    const Impl::Character* ch = m_impl->find(id);
    return ch ? ch->height : 0.0f;
}

float PhysicsWorld::characterRadius(int id) const {
    const Impl::Character* ch = m_impl->find(id);
    return ch ? ch->radius : 0.0f;
}

void PhysicsWorld::step(float dt) {
    JPH::BodyInterface& bodies = m_impl->bodies();
    struct Before {
        JPH::BodyID id;
        glm::vec3 velocity{0.0f};
        glm::vec3 gravity{0.0f};
        float mass = 0.0f;
    };
    std::vector<Before> near;
    if (!m_impl->vehicle.IsInvalid()) {
        const JPH::AABox box = bodies.GetTransformedShape(m_impl->vehicle).GetWorldSpaceBounds();
        JPH::AABox reach = box;
        reach.ExpandBy(JPH::Vec3::sReplicate(kVehicleReach));
        JPH::AllHitCollisionCollector<JPH::CollideShapeBodyCollector> collector;
        m_impl->system.GetBroadPhaseQuery().CollideAABox(reach, collector);
        std::sort(collector.mHits.begin(), collector.mHits.end());
        for (const JPH::BodyID id : collector.mHits) {
            JPH::BodyLockRead lock(m_impl->system.GetBodyLockInterface(), id);
            if (!lock.Succeeded() || !lock.GetBody().IsDynamic()) {
                continue;
            }
            const JPH::Body& body = lock.GetBody();
            const float inverseMass = body.GetMotionProperties()->GetInverseMass();
            near.push_back({id, toGlm(body.GetLinearVelocity()), m_impl->gravityAt(toGlmR(body.GetPosition())),
                            inverseMass > 0.0f ? 1.0f / inverseMass : 0.0f});
        }
    }

    if (m_impl->gravity) {
        JPH::BodyIDVector active;
        m_impl->system.GetActiveBodies(JPH::EBodyType::RigidBody, active);
        std::sort(active.begin(), active.end());
        for (const JPH::BodyID id : active) {
            JPH::BodyLockWrite lock(m_impl->system.GetBodyLockInterface(), id);
            if (!lock.Succeeded() || !lock.GetBody().IsDynamic()) {
                continue;
            }
            JPH::Body& body = lock.GetBody();
            const float inverseMass = body.GetMotionProperties()->GetInverseMass();
            if (inverseMass <= 0.0f) {
                continue;
            }
            body.AddForce(toJolt(m_impl->gravity(toGlmR(body.GetPosition())) / inverseMass));
        }
    }

    m_impl->vehicleContacts.touched.clear();
    m_impl->system.Update(dt, 1, m_impl->tempAllocator.get(), m_impl->jobSystem.get());

    std::vector<std::pair<JPH::BodyID, glm::vec3>> touched = m_impl->vehicleContacts.touched;
    std::sort(touched.begin(), touched.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    JPH::BodyID last;
    for (const auto& [id, point] : touched) {
        if (id == last) {
            continue;
        }
        last = id;
        const auto before = std::find_if(near.begin(), near.end(), [&](const Before& b) { return b.id == id; });
        if (before == near.end() || before->mass <= 0.0f) {
            continue;
        }
        const glm::vec3 after = toGlm(bodies.GetLinearVelocity(id));
        const glm::vec3 change = after - before->velocity - before->gravity * dt;
        m_impl->pushes.push_back({-change * before->mass, point});
    }
}

}
