#pragma once

#include "core/pool.h"
#include "core/types.h"
#include "math/vmath.h"
#include "physics/body.h"
#include "physics/static_grid.h"

#include <span>

namespace anom {

class Arena;
class Heightfield;
class GravityField;
class JoltWorld;

struct PhysContact {
    u64 key;
    u32 body_a;
    u32 body_b;
    Vec3 point;
    Vec3 normal;
    Vec3 tangent[2];
    f32 depth;
    f32 normal_impulse;
    f32 tangent_impulse[2];
};

struct PhysRayHit {
    f32 t;
    Vec3 point;
    Vec3 normal;
};

struct PhysTuning {
    f32 gravity = -9.81f;
    f32 linear_damping = 0.02f;
    f32 angular_damping = 0.08f;
    f32 restitution_min_speed = 1.0f;
    f32 baumgarte = 0.20f;
    f32 slop = 0.005f;
    f32 max_correction_speed = 3.0f;
    u32 iterations = 8;
    bool warm_start = true;
    f32 sleep_linear = 0.006f;
    f32 sleep_angular = 0.05f;
    f32 sleep_time = 0.5f;
};

struct PhysStats {
    u32 live_bodies = 0;
    u32 pair_tests = 0;
    u32 contacts = 0;
    u32 warm_started = 0;
};

class PhysWorld {
public:
    static constexpr u32 kMaxBodies = 256;
    static constexpr u32 kMaxContacts = 2048;
    static constexpr u32 kStaticBody = 0xFFFFFFFFu;

    void init(Arena& arena, const Heightfield* hf);

    void statics_reserve(Arena& arena, u32 max_tris) { statics_.reserve(arena, max_tris); }
    void add_static_tri(Vec3 a, Vec3 b, Vec3 c) { statics_.add(a, b, c); }
    void statics_build(Arena& arena)
    {
        statics_.build(arena);
        sync_jolt();
    }
    void statics_clear() { statics_.clear(); }
    const StaticGrid& statics() const { return statics_; }
    const Heightfield* heightfield() const { return hf_; }

    BodyHandle body_create_box(Vec3 pos, Quat rot, Vec3 half_extents, f32 mass);
    void body_destroy(BodyHandle handle);
    RigidBody* body(BodyHandle handle) { return bodies_.get(handle); }
    const RigidBody* body(BodyHandle handle) const { return bodies_.get(handle); }
    BodyHandle handle_at(u32 slot) const { return bodies_.handle_at(slot); }

    Pool<RigidBody>& bodies() { return bodies_; }
    const Pool<RigidBody>& bodies() const { return bodies_; }

    void tick(f32 dt);
    bool raycast(Ray ray, f32 max_t, PhysRayHit* out_hit) const;

    std::span<const PhysContact> contacts() const { return {contacts_, contact_count_}; }
    const PhysStats& stats() const { return stats_; }

    PhysTuning& tuning() { return tuning_; }
    const PhysTuning& tuning() const { return tuning_; }

    Vec3 gravity() const { return Vec3{0.0f, tuning_.gravity, 0.0f}; }
    Vec3 gravity_at(Vec3 p) const;
    Vec3 up_at(Vec3 p) const;
    void set_gravity_field(const GravityField* field) { field_ = field; }
    const GravityField* gravity_field() const { return field_; }
    void set_jolt(JoltWorld* jolt)
    {
        jolt_ = jolt;
        sync_jolt();
    }
    JoltWorld* jolt() const { return jolt_; }
    void sync_jolt();

private:
    void integrate_velocities(f32 dt);
    void integrate_positions(f32 dt);
    void collect_contacts();
    void collect_static_contacts(u32 slot);
    void collect_pair_contacts();
    void warm_start();
    void solve(f32 dt);
    void update_sleep(f32 dt);
    void add_contact(const PhysContact& contact);

    Pool<RigidBody> bodies_;
    StaticGrid statics_;
    const Heightfield* hf_ = nullptr;
    const GravityField* field_ = nullptr;
    JoltWorld* jolt_ = nullptr;

    PhysContact* contacts_ = nullptr;
    PhysContact* previous_ = nullptr;
    u32 contact_count_ = 0;
    u32 previous_count_ = 0;

    u32* pair_a_ = nullptr;
    u32* pair_b_ = nullptr;
    u32 pair_count_ = 0;

    PhysTuning tuning_;
    PhysStats stats_;
};

} // namespace anom
