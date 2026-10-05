#include "physics/jolt_world.h"
#include "core/log.h"
#include "physics/heightfield.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <vector>

namespace anom {
namespace {

namespace layers {
constexpr JPH::ObjectLayer kStatic = 0;
constexpr JPH::ObjectLayer kMoving = 1;
}

namespace bp_layers {
constexpr JPH::BroadPhaseLayer kStatic(0);
constexpr JPH::BroadPhaseLayer kMoving(1);
}

class BroadPhaseLayerMap final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        return layer == layers::kStatic ? bp_layers::kStatic : bp_layers::kMoving;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return layer == bp_layers::kStatic ? "static" : "moving";
    }
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer object, JPH::BroadPhaseLayer broad_phase) const override
    {
        return object == layers::kMoving || broad_phase == bp_layers::kMoving;
    }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
    {
        return a == layers::kMoving || b == layers::kMoving;
    }
};

constexpr u32 kHeightBlock = 4;
constexpr f32 kMaxSlopeDeg = 50.0f;
constexpr f32 kStepUp = 0.35f;
constexpr f32 kStickDown = 0.45f;
constexpr f32 kStairMinForward = 0.08f;
constexpr f32 kStairForwardTest = 0.4f;
constexpr f32 kStepMinGain = 0.03f;
constexpr f32 kStairSweepStep = 0.1f;
constexpr f32 kStickMaxRise = 0.5f;
constexpr f32 kStepBlockUp = 0.64f;
constexpr f32 kStepBlockInto = 0.1f;
constexpr f32 kFitsSlack = 0.02f;
constexpr u32 kTempBytes = 4u * 1024u * 1024u;

JPH::Vec3 to_jolt(Vec3 v) { return {v.x, v.y, v.z}; }
JPH::RVec3 to_jolt_r(Vec3 v) { return JPH::RVec3(v.x, v.y, v.z); }
Vec3 from_jolt(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
Vec3 from_jolt_r(JPH::RVec3Arg v)
{
    return {static_cast<f32>(v.GetX()), static_cast<f32>(v.GetY()), static_cast<f32>(v.GetZ())};
}
JPH::Quat to_jolt(Quat q) { return JPH::Quat(q.x, q.y, q.z, q.w).Normalized(); }

JPH::Quat up_rotation(Vec3 up)
{
    return to_jolt(quat_from_to(Vec3{0.0f, 1.0f, 0.0f}, normalize(up)));
}

JPH::RefConst<JPH::Shape> make_capsule(f32 radius, f32 height)
{
    const f32 half_cylinder = f_max(0.5f * height - radius, 0.01f);
    const f32 centre = half_cylinder + radius;
    JPH::RotatedTranslatedShapeSettings settings(JPH::Vec3(0.0f, centre, 0.0f),
                                                 JPH::Quat::sIdentity(),
                                                 new JPH::CapsuleShape(half_cylinder, radius));
    JPH::ShapeSettings::ShapeResult result = settings.Create();
    if (result.HasError()) {
        log_error("jolt: capsule shape failed: %s", result.GetError().c_str());
        return nullptr;
    }
    return result.Get();
}

int g_live_worlds = 0;

} // namespace

struct JoltWorld::Impl {
    BroadPhaseLayerMap broad_phase_layers;
    ObjectVsBroadPhaseFilter object_vs_broad_phase;
    ObjectPairFilter object_pairs;
    JPH::TempAllocatorImpl temp{kTempBytes};
    JPH::PhysicsSystem* system = nullptr;
    JPH::Ref<JPH::CharacterVirtual> character;
    JPH::BodyID car;
    f32 radius = 0.3f;
    f32 height = 1.6f;
    Vec3 car_half;
    Vec3 car_offset;
    u32 static_bodies = 0;

    void create_system()
    {
        system = new JPH::PhysicsSystem();
        constexpr JPH::uint kMaxBodies = 4096;
        constexpr JPH::uint kMaxBodyPairs = 4096;
        constexpr JPH::uint kMaxContactConstraints = 2048;
        system->Init(kMaxBodies, 0, kMaxBodyPairs, kMaxContactConstraints, broad_phase_layers,
                     object_vs_broad_phase, object_pairs);
        car = JPH::BodyID();
        static_bodies = 0;
    }

    void destroy_system()
    {
        character = nullptr;
        delete system;
        system = nullptr;
    }

    JPH::DefaultBroadPhaseLayerFilter broad_phase_filter() const
    {
        return system->GetDefaultBroadPhaseLayerFilter(layers::kMoving);
    }

    JPH::DefaultObjectLayerFilter object_filter() const
    {
        return system->GetDefaultLayerFilter(layers::kMoving);
    }

    bool clear_at(const JPH::Shape* shape, JPH::RVec3Arg pos, JPH::QuatArg rot) const
    {
        const JPH::RMat44 transform = JPH::RMat44::sRotationTranslation(rot, pos)
                                          .PreTranslated(shape->GetCenterOfMass());
        JPH::CollideShapeSettings settings;
        settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        system->GetNarrowPhaseQuery().CollideShape(shape, JPH::Vec3::sReplicate(1.0f), transform,
                                                   settings, transform.GetTranslation(), collector,
                                                   broad_phase_filter(), object_filter());
        for (const JPH::CollideShapeResult& hit : collector.mHits) {
            if (hit.mPenetrationDepth > kFitsSlack) {
                return false;
            }
        }
        return true;
    }

    bool land(JPH::CharacterVirtual& c, JPH::RVec3Arg from, JPH::Vec3Arg up, f32 drop, bool flat)
    {
        c.SetPosition(from);
        if (!c.StickToFloor(up * -drop, broad_phase_filter(), object_filter(), {}, {}, temp)) {
            return false;
        }
        return !flat || c.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
    }

    bool step_up(JPH::CharacterVirtual& c, JPH::Vec3Arg up, JPH::Vec3Arg dir, f32 forward)
    {
        const JPH::RVec3 start = c.GetPosition();
        const JPH::Quat rot = c.GetRotation();
        const JPH::Shape* shape = c.GetShape();
        const JPH::RVec3 raised = start + up * kStepUp;
        const JPH::RVec3 probe = raised + dir * kStairForwardTest;
        const JPH::RVec3 ahead = raised + dir * forward;
        const auto restore = [&] {
            c.SetPosition(start);
            c.RefreshContacts(broad_phase_filter(), object_filter(), {}, {}, temp);
            return false;
        };
        for (f32 along = 0.0f; along <= kStairForwardTest + 1e-4f; along += kStairSweepStep) {
            if (!clear_at(shape, raised + dir * along + up * kFitsSlack, rot)) {
                return false;
            }
        }
        if (!land(c, probe, up, kStepUp + kFitsSlack, true)) {
            return restore();
        }
        const f32 gain = JPH::Vec3(c.GetPosition() - start).Dot(up);
        if (gain < kStepMinGain) {
            return restore();
        }
        if (!clear_at(shape, ahead + up * kFitsSlack, rot) || !land(c, ahead, up, kStepUp + kFitsSlack, false)
            || JPH::Vec3(c.GetPosition() - start).Dot(up) <= 0.0f) {
            return restore();
        }
        return true;
    }

    void add_static(const JPH::ShapeSettings& settings, Vec3 pos, Quat rot)
    {
        JPH::ShapeSettings::ShapeResult result = settings.Create();
        if (result.HasError()) {
            log_error("jolt: static shape failed: %s", result.GetError().c_str());
            return;
        }
        JPH::BodyCreationSettings body(result.Get(), to_jolt_r(pos), to_jolt(rot),
                                       JPH::EMotionType::Static, layers::kStatic);
        const JPH::BodyID id = system->GetBodyInterface().CreateAndAddBody(
            body, JPH::EActivation::DontActivate);
        if (!id.IsInvalid()) {
            static_bodies++;
        }
    }
};

JoltWorld::JoltWorld()
{
    if (g_live_worlds++ == 0) {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }
    impl_ = new Impl();
    impl_->create_system();
}

JoltWorld::~JoltWorld()
{
    impl_->destroy_system();
    delete impl_;
    impl_ = nullptr;
    if (--g_live_worlds == 0) {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}

void JoltWorld::reset()
{
    impl_->destroy_system();
    impl_->create_system();
}

void JoltWorld::set_terrain(const Heightfield& hf)
{
    if (!hf.valid()) {
        return;
    }
    const u32 n = hf.size_x() > hf.size_z() ? hf.size_x() : hf.size_z();
    const u32 padded = ((n + kHeightBlock - 1) / kHeightBlock) * kHeightBlock;
    std::vector<float> samples(static_cast<size_t>(padded) * padded, JPH::HeightFieldShapeConstants::cNoCollisionValue);
    for (u32 z = 0; z < hf.size_z(); z++) {
        for (u32 x = 0; x < hf.size_x(); x++) {
            samples[static_cast<size_t>(z) * padded + x] = hf.height_at(x, z);
        }
    }
    const Vec3 origin = hf.origin();
    JPH::HeightFieldShapeSettings settings(samples.data(), JPH::Vec3(origin.x, 0.0f, origin.z),
                                           JPH::Vec3(hf.cell_size(), 1.0f, hf.cell_size()), padded);
    settings.mBlockSize = kHeightBlock;
    settings.mBitsPerSample = settings.CalculateBitsPerSampleForError(0.005f);
    impl_->add_static(settings, Vec3{}, quat_identity());
}

void JoltWorld::add_static_mesh(std::span<const Vec3> triangles)
{
    const size_t count = triangles.size() / 3;
    if (count == 0) {
        return;
    }
    JPH::TriangleList list;
    list.reserve(count);
    for (size_t i = 0; i < count; i++) {
        const Vec3 a = triangles[i * 3 + 0];
        const Vec3 b = triangles[i * 3 + 1];
        const Vec3 c = triangles[i * 3 + 2];
        if (length_sq(cross(b - a, c - a)) < 1e-10f) {
            continue;
        }
        list.push_back(JPH::Triangle(JPH::Float3(a.x, a.y, a.z), JPH::Float3(b.x, b.y, b.z),
                                     JPH::Float3(c.x, c.y, c.z)));
    }
    if (list.empty()) {
        return;
    }
    JPH::MeshShapeSettings settings(list);
    impl_->add_static(settings, Vec3{}, quat_identity());
}

void JoltWorld::add_static_box(Vec3 center, Quat rot, Vec3 half)
{
    const f32 convex = f_min(JPH::cDefaultConvexRadius, 0.5f * f_min(half.x, f_min(half.y, half.z)));
    JPH::BoxShapeSettings settings(to_jolt(half), convex);
    impl_->add_static(settings, center, rot);
}

void JoltWorld::optimize()
{
    impl_->system->OptimizeBroadPhase();
}

void JoltWorld::set_car(Vec3 pos, Quat rot, Vec3 half, Vec3 offset, Vec3 vel, Vec3 angular_vel)
{
    JPH::BodyInterface& bodies = impl_->system->GetBodyInterface();
    const bool reshape = !impl_->car.IsInvalid()
                      && (length_sq(half - impl_->car_half) > 1e-8f
                          || length_sq(offset - impl_->car_offset) > 1e-8f);
    if (reshape) {
        clear_car();
    }
    if (impl_->car.IsInvalid()) {
        const f32 convex = f_min(JPH::cDefaultConvexRadius,
                                 0.5f * f_min(half.x, f_min(half.y, half.z)));
        JPH::RotatedTranslatedShapeSettings shape(to_jolt(offset), JPH::Quat::sIdentity(),
                                                  new JPH::BoxShape(to_jolt(half), convex));
        JPH::ShapeSettings::ShapeResult result = shape.Create();
        if (result.HasError()) {
            return;
        }
        JPH::BodyCreationSettings body(result.Get(), to_jolt_r(pos), to_jolt(rot),
                                       JPH::EMotionType::Kinematic, layers::kMoving);
        impl_->car = bodies.CreateAndAddBody(body, JPH::EActivation::DontActivate);
        impl_->car_half = half;
        impl_->car_offset = offset;
    }
    if (impl_->car.IsInvalid()) {
        return;
    }
    bodies.SetPositionAndRotation(impl_->car, to_jolt_r(pos), to_jolt(rot),
                                  JPH::EActivation::DontActivate);
    bodies.SetLinearAndAngularVelocity(impl_->car, to_jolt(vel), to_jolt(angular_vel));
}

void JoltWorld::clear_car()
{
    if (impl_->car.IsInvalid()) {
        return;
    }
    JPH::BodyInterface& bodies = impl_->system->GetBodyInterface();
    bodies.RemoveBody(impl_->car);
    bodies.DestroyBody(impl_->car);
    impl_->car = JPH::BodyID();
}

bool JoltWorld::raycast(Vec3 from, Vec3 to, JoltRayHit* out) const
{
    const JPH::RRayCast ray{to_jolt_r(from), to_jolt(to - from)};
    JPH::RayCastResult result;
    if (!impl_->system->GetNarrowPhaseQuery().CastRay(ray, result)) {
        return false;
    }
    if (out) {
        const JPH::RVec3 point = ray.GetPointOnRay(result.mFraction);
        out->fraction = result.mFraction;
        out->point = from_jolt_r(point);
        JPH::BodyLockRead lock(impl_->system->GetBodyLockInterface(), result.mBodyID);
        if (lock.Succeeded()) {
            out->normal = from_jolt(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point));
        }
    }
    return true;
}

void JoltWorld::character_create(Vec3 feet, Vec3 up, f32 radius, f32 height)
{
    impl_->radius = radius;
    impl_->height = height;
    JPH::RefConst<JPH::Shape> shape = make_capsule(radius, height);
    if (!shape) {
        return;
    }
    JPH::CharacterVirtualSettings settings;
    settings.mShape = shape;
    settings.mUp = to_jolt(normalize(up));
    settings.mMaxSlopeAngle = kMaxSlopeDeg * kDegToRad;
    settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);
    settings.mMass = 70.0f;
    settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
    settings.mEnhancedInternalEdgeRemoval = true;
    impl_->character = new JPH::CharacterVirtual(&settings, to_jolt_r(feet), up_rotation(up), 0,
                                                 impl_->system);
}

bool JoltWorld::character_valid() const
{
    return impl_->character != nullptr;
}

void JoltWorld::character_teleport(Vec3 feet, Vec3 up)
{
    if (!impl_->character) {
        return;
    }
    JPH::CharacterVirtual& c = *impl_->character;
    c.SetUp(to_jolt(normalize(up)));
    c.SetRotation(up_rotation(up));
    c.SetPosition(to_jolt_r(feet));
    c.SetLinearVelocity(JPH::Vec3::sZero());
    c.RefreshContacts(impl_->broad_phase_filter(), impl_->object_filter(), {}, {}, impl_->temp);
}

bool JoltWorld::character_set_height(f32 height, Vec3 up)
{
    if (!impl_->character) {
        return false;
    }
    if (f_abs(height - impl_->height) < 1e-4f) {
        return true;
    }
    JPH::RefConst<JPH::Shape> shape = make_capsule(impl_->radius, height);
    if (!shape) {
        return false;
    }
    JPH::CharacterVirtual& c = *impl_->character;
    c.SetUp(to_jolt(normalize(up)));
    c.SetRotation(up_rotation(up));
    const f32 max_pen = height > impl_->height ? kFitsSlack : FLT_MAX;
    if (!c.SetShape(shape, max_pen, impl_->broad_phase_filter(), impl_->object_filter(), {}, {},
                    impl_->temp)) {
        return false;
    }
    impl_->height = height;
    return true;
}

bool JoltWorld::character_fits(Vec3 feet, Vec3 up, f32 height) const
{
    JPH::RefConst<JPH::Shape> shape = make_capsule(impl_->radius, height);
    if (!shape) {
        return true;
    }
    const JPH::RMat44 transform = JPH::RMat44::sRotationTranslation(up_rotation(up),
                                                                    to_jolt_r(feet + normalize(up) * 0.02f))
                                      .PreTranslated(shape->GetCenterOfMass());
    JPH::CollideShapeSettings settings;
    settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    impl_->system->GetNarrowPhaseQuery().CollideShape(
        shape, JPH::Vec3::sReplicate(1.0f), transform, settings, transform.GetTranslation(),
        collector, impl_->broad_phase_filter(), impl_->object_filter());
    for (const JPH::CollideShapeResult& hit : collector.mHits) {
        if (hit.mPenetrationDepth > kFitsSlack) {
            return false;
        }
    }
    return true;
}

CharacterMove JoltWorld::character_move(Vec3 velocity, Vec3 up, Vec3 gravity, f32 dt, bool stick)
{
    CharacterMove out;
    if (!impl_->character) {
        return out;
    }
    JPH::CharacterVirtual& c = *impl_->character;
    const Vec3 n_up = normalize(up);
    c.SetUp(to_jolt(n_up));
    c.SetRotation(up_rotation(n_up));
    c.SetLinearVelocity(to_jolt(velocity));

    const JPH::DefaultBroadPhaseLayerFilter bp_filter = impl_->broad_phase_filter();
    const JPH::DefaultObjectLayerFilter layer_filter = impl_->object_filter();
    const JPH::BodyFilter body_filter;
    const JPH::ShapeFilter shape_filter;
    const JPH::Vec3 j_up = to_jolt(n_up);

    c.StartTrackingContactChanges();
    const JPH::Vec3 desired = to_jolt(velocity);
    c.SetLinearVelocity(c.CancelVelocityTowardsSteepSlopes(desired));
    const JPH::RVec3 old_position = c.GetPosition();
    c.Update(dt, to_jolt(gravity), bp_filter, layer_filter, body_filter, shape_filter, impl_->temp);

    if (stick && !c.IsSupported()) {
        const f32 rise = JPH::Vec3(c.GetPosition() - old_position).Dot(j_up) / dt;
        if (rise <= kStickMaxRise) {
            c.StickToFloor(j_up * -kStickDown, bp_filter, layer_filter, body_filter, shape_filter,
                           impl_->temp);
        }
    }

    bool stepped = false;
    JPH::Vec3 want = desired * dt;
    want -= want.Dot(j_up) * j_up;
    const f32 want_len = want.Length();
    if (want_len > 1e-5f && c.IsSupported()) {
        const JPH::Vec3 dir = want / want_len;
        const f32 got = f_max(JPH::Vec3(c.GetPosition() - old_position).Dot(dir), 0.0f);
        bool blocked = false;
        for (const JPH::CharacterVirtual::Contact& contact : c.GetActiveContacts()) {
            if (contact.mHadCollision && contact.mContactNormal.Dot(j_up) < kStepBlockUp
                && contact.mContactNormal.Dot(dir) < -kStepBlockInto) {
                blocked = true;
                break;
            }
        }
        if (blocked && got + 1e-4f < want_len) {
            stepped = impl_->step_up(c, j_up, dir, f_max(kStairMinForward, want_len - got));
        }
    }
    c.FinishTrackingContactChanges();

    out.position = from_jolt_r(c.GetPosition());
    const JPH::CharacterBase::EGroundState ground = c.GetGroundState();
    out.stepped = stepped;
    out.grounded = stepped || ground == JPH::CharacterBase::EGroundState::OnGround;
    out.steep = ground == JPH::CharacterBase::EGroundState::OnSteepGround;
    out.ground_normal = from_jolt(c.GetGroundNormal());
    out.ground_velocity = from_jolt(c.GetGroundVelocity());
    for (const JPH::CharacterVirtual::Contact& contact : c.GetActiveContacts()) {
        if (!contact.mHadCollision || out.normal_count >= 4) {
            continue;
        }
        const Vec3 n = from_jolt(contact.mContactNormal);
        bool seen = false;
        for (u32 i = 0; i < out.normal_count; i++) {
            if (dot(out.normals[i], n) > 0.99f) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            out.normals[out.normal_count++] = n;
        }
    }
    return out;
}

Vec3 JoltWorld::character_position() const
{
    return impl_->character ? from_jolt_r(impl_->character->GetPosition()) : Vec3{};
}

f32 JoltWorld::character_height() const
{
    return impl_->height;
}

f32 JoltWorld::character_radius() const
{
    return impl_->radius;
}

u32 JoltWorld::static_body_count() const
{
    return impl_->static_bodies;
}

} // namespace anom
