#pragma once

#include <array>

#include <vector>

#include "core/pool.h"
#include "core/types.h"
#include "math/vmath.h"
#include "physics/body.h"
#include "physics/static_grid.h"

#include <cfloat>
#include <span>

namespace ghost::engine {
class PhysicsWorld;
}

namespace anom {
inline constexpr f32 kNoCollisionHeight = FLT_MAX;

class Arena;
class Heightfield;
class GravityField;

struct PhysRayHit {
    f32 t;
    Vec3 point;
    Vec3 normal;
};

struct PhysTuning {
    f32 gravity = -9.81f;
    f32 linear_damping = 0.02f;
    f32 angular_damping = 0.08f;
};

inline constexpr u32 kBodyHullPoints = 16;

struct BodyDesc {
    Vec3 half_extents{0.5f, 0.5f, 0.5f};
    Vec3 box_offset{0.0f, 0.0f, 0.0f};
    f32 mass = 1.0f;
    Vec3 inertia_diag{1.0f, 1.0f, 1.0f};
    f32 friction = 0.6f;
    f32 restitution = 0.15f;
    bool linear_cast = false;
    bool allow_sleeping = true;
    std::array<Vec3, kBodyHullPoints> hull{};
    u32 hull_count = 0;
};

inline bool body_desc_equal(const BodyDesc& a, const BodyDesc& b)
{
    return a.half_extents == b.half_extents && a.box_offset == b.box_offset && a.mass == b.mass
        && a.inertia_diag == b.inertia_diag && a.friction == b.friction
        && a.restitution == b.restitution && a.linear_cast == b.linear_cast
        && a.allow_sleeping == b.allow_sleeping && a.hull_count == b.hull_count && a.hull == b.hull;
}

Vec3 solid_box_inertia(Vec3 half_extents, f32 mass);

class PhysWorld {
public:
    static constexpr u32 kMaxBodies = 256;

    void init(Arena& arena, const Heightfield* hf);

    void statics_reserve(Arena& arena, u32 max_tris)
    {
        statics_.reserve(arena, max_tris);
        static_boxes_.clear();
    }
    void add_static_tri(Vec3 a, Vec3 b, Vec3 c) { statics_.add(a, b, c); }
    void add_static_box(Vec3 center, Vec3 half, u8 surface) { static_boxes_.push_back({center, half, surface}); }
    u32 static_box_count() const { return static_cast<u32>(static_boxes_.size()); }
    void statics_build(Arena& arena)
    {
        statics_.build(arena);
        sync_jolt();
    }
    void statics_clear()
    {
        statics_.clear();
        static_boxes_.clear();
    }
    const StaticGrid& statics() const { return statics_; }
    const Heightfield* heightfield() const { return hf_; }

    BodyHandle body_create_box(Vec3 pos, Quat rot, Vec3 half_extents, f32 mass);
    BodyHandle body_create_box(Vec3 pos, Quat rot, const BodyDesc& desc);
    void body_reshape(BodyHandle handle, const BodyDesc& desc);
    void body_destroy(BodyHandle handle);
    RigidBody* body(BodyHandle handle) { return bodies_.get(handle); }
    const RigidBody* body(BodyHandle handle) const { return bodies_.get(handle); }
    BodyHandle handle_at(u32 slot) const { return bodies_.handle_at(slot); }

    Pool<RigidBody>& bodies() { return bodies_; }
    const Pool<RigidBody>& bodies() const { return bodies_; }

    void tick(f32 dt);
    bool raycast(Ray ray, f32 max_t, PhysRayHit* out_hit) const;

    PhysTuning& tuning() { return tuning_; }
    const PhysTuning& tuning() const { return tuning_; }

    Vec3 gravity() const { return Vec3{0.0f, tuning_.gravity, 0.0f}; }
    Vec3 gravity_at(Vec3 p) const;
    Vec3 up_at(Vec3 p) const;
    void set_gravity_field(const GravityField* field)
    {
        field_ = field;
        install_gravity();
    }
    const GravityField* gravity_field() const { return field_; }
    void set_jolt(ghost::engine::PhysicsWorld* jolt);
    ghost::engine::PhysicsWorld* jolt() const { return jolt_; }
    void sync_jolt();

private:
    void install_gravity();
    void ensure_jolt_bodies();
    u32 create_jolt_body(const RigidBody& body, const BodyDesc& desc) const;
    void apply_desc(RigidBody& body, const BodyDesc& desc) const;

    Pool<RigidBody> bodies_;
    struct StaticBox {
        Vec3 center;
        Vec3 half;
        u8 surface;
    };
    std::vector<StaticBox> static_boxes_;
    std::vector<BodyDesc> descs_;
    StaticGrid statics_;
    const Heightfield* hf_ = nullptr;
    const GravityField* field_ = nullptr;
    ghost::engine::PhysicsWorld* jolt_ = nullptr;
    bool warned_no_jolt_ = false;

    PhysTuning tuning_;
};

}
