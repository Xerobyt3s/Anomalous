#pragma once

#include "core/types.h"
#include "math/vmath.h"

#include <span>

namespace anom {

class Heightfield;

struct JoltRayHit {
    Vec3 point{};
    Vec3 normal{0.0f, 1.0f, 0.0f};
    f32 fraction = 0.0f;
};

struct CharacterMove {
    Vec3 position{};
    bool grounded = false;
    bool steep = false;
    bool stepped = false;
    Vec3 ground_normal{0.0f, 1.0f, 0.0f};
    Vec3 ground_velocity{};
    Vec3 normals[4]{};
    u32 normal_count = 0;
};

class JoltWorld {
public:
    JoltWorld();
    ~JoltWorld();
    JoltWorld(const JoltWorld&) = delete;
    JoltWorld& operator=(const JoltWorld&) = delete;

    void reset();
    void set_terrain(const Heightfield& hf);
    void add_static_mesh(std::span<const Vec3> triangles);
    void add_static_box(Vec3 center, Quat rot, Vec3 half);
    void optimize();

    void set_car(Vec3 pos, Quat rot, Vec3 half, Vec3 offset, Vec3 vel, Vec3 angular_vel);
    void clear_car();

    bool raycast(Vec3 from, Vec3 to, JoltRayHit* out) const;

    void character_create(Vec3 feet, Vec3 up, f32 radius, f32 height);
    bool character_valid() const;
    void character_teleport(Vec3 feet, Vec3 up);
    bool character_set_height(f32 height, Vec3 up);
    bool character_fits(Vec3 feet, Vec3 up, f32 height) const;
    CharacterMove character_move(Vec3 velocity, Vec3 up, Vec3 gravity, f32 dt, bool stick);
    Vec3 character_position() const;
    f32 character_height() const;
    f32 character_radius() const;

    u32 static_body_count() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace anom
