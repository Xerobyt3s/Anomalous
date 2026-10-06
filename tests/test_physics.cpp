#include "test.h"

#include "core/arena.h"
#include "core/rng.h"
#include "engine/physics/physics_world.h"
#include "physics/heightfield.h"
#include "physics/world.h"

using namespace anom;

namespace {
constexpr f32 kDt = 1.0f / 120.0f;

Heightfield make_flat(Arena& arena, u32 size = 64, f32 cell = 2.0f)
{
    Heightfield hf;
    hf.alloc(arena, size, cell);
    for (u32 z = 0; z < size; z++) {
        for (u32 x = 0; x < size; x++) {
            hf.set_height(x, z, 0.0f);
        }
    }
    hf.recompute_extents();
    return hf;
}

struct Rig {
    Arena arena{megabytes(64)};
    Heightfield hf;
    PhysWorld world;
    ghost::engine::PhysicsWorld jolt;

    Rig()
    {
        hf = make_flat(arena);
        world.init(arena, &hf);
        world.set_jolt(&jolt);
    }
};

BodyHandle spawn_box(PhysWorld& world, Vec3 pos, Vec3 half, f32 density = 200.0f)
{
    const f32 mass = density * 8.0f * half.x * half.y * half.z;
    return world.body_create_box(pos, quat_identity(), half, mass);
}

void run(PhysWorld& world, i32 ticks)
{
    for (i32 i = 0; i < ticks; i++) {
        world.tick(kDt);
    }
}

}

TEST(physics, body_falls_and_settles_on_ground)
{
    Rig rig;
    const BodyHandle h = spawn_box(rig.world, Vec3{0.0f, 6.0f, 0.0f}, Vec3{0.5f, 0.5f, 0.5f});
    CHECK(h.valid());

    run(rig.world, 600);

    const RigidBody* body = rig.world.body(h);
    CHECK(body != nullptr);
    CHECK(body_state_valid(*body));
    CHECK(body->pos.y > 0.0f);
    CHECK(body->pos.y < 1.2f);
    CHECK(f_abs(body->vel.y) < 0.2f);
}

TEST(physics, resting_body_does_not_sink)
{
    Rig rig;
    const BodyHandle h = spawn_box(rig.world, Vec3{0.0f, 0.5f, 0.0f}, Vec3{0.5f, 0.5f, 0.5f});
    run(rig.world, 240);
    const f32 settled = rig.world.body(h)->pos.y;

    run(rig.world, 600);
    const f32 later = rig.world.body(h)->pos.y;

    CHECK_NEAR(settled, later, 0.02);
    CHECK(later > 0.30f);
}

TEST(physics, body_sleeps_when_at_rest)
{
    Rig rig;
    const BodyHandle h = spawn_box(rig.world, Vec3{0.0f, 0.55f, 0.0f}, Vec3{0.5f, 0.5f, 0.5f});
    run(rig.world, 600);
    CHECK(rig.world.body(h)->asleep != 0);
}

TEST(physics, a_direct_velocity_write_reaches_the_solver)
{
    Rig rig;
    const BodyHandle h = spawn_box(rig.world, Vec3{0.0f, 0.5f, 0.0f}, Vec3{0.5f, 0.5f, 0.5f});
    run(rig.world, 240);
    CHECK(rig.world.body(h)->asleep != 0);

    rig.world.body(h)->vel = Vec3{0.0f, 0.0f, -4.0f};
    run(rig.world, 60);
    CHECK(rig.world.body(h)->asleep == 0);
    CHECK(rig.world.body(h)->pos.z < -1.0f);
}

TEST(physics, a_teleport_moves_the_solver_body)
{
    Rig rig;
    const BodyHandle h = spawn_box(rig.world, Vec3{0.0f, 0.5f, 0.0f}, Vec3{0.5f, 0.5f, 0.5f});
    run(rig.world, 60);

    RigidBody* body = rig.world.body(h);
    body->pos = Vec3{10.0f, 6.0f, 10.0f};
    body->vel = Vec3{};
    run(rig.world, 600);
    CHECK(f_abs(body->pos.x - 10.0f) < 0.05f);
    CHECK(f_abs(body->pos.z - 10.0f) < 0.05f);
    CHECK(body->pos.y > 0.0f);
    CHECK(body->pos.y < 1.2f);
}

TEST(physics, simulation_is_deterministic)
{
    const auto simulate = [](Vec3* out_positions) {
        Rig rig;
        Rng rng(1234);
        BodyHandle handles[16];
        for (i32 i = 0; i < 16; i++) {
            const Vec3 half{rng.range(0.25f, 0.8f), rng.range(0.25f, 0.8f),
                            rng.range(0.25f, 0.8f)};
            const Vec3 pos{rng.range(-2.0f, 2.0f), 3.0f + static_cast<f32>(i) * 1.5f,
                           rng.range(-2.0f, 2.0f)};
            handles[i] = spawn_box(rig.world, pos, half);
        }
        for (i32 i = 0; i < 720; i++) {
            rig.world.tick(kDt);
        }
        for (i32 i = 0; i < 16; i++) {
            out_positions[i] = rig.world.body(handles[i])->pos;
        }
    };

    Vec3 first[16];
    Vec3 second[16];
    simulate(first);
    simulate(second);

    for (i32 i = 0; i < 16; i++) {
        CHECK(first[i].x == second[i].x);
        CHECK(first[i].y == second[i].y);
        CHECK(first[i].z == second[i].z);
    }
}

TEST(physics, raycast_hits_flat_ground)
{
    Rig rig;
    const Ray down{{1.0f, 10.0f, 1.0f}, {0.0f, -1.0f, 0.0f}};
    PhysRayHit hit{};
    CHECK(rig.world.raycast(down, 100.0f, &hit));
    CHECK_NEAR(hit.t, 10.0f, 1e-2);
    CHECK(hit.normal.y > 0.9f);
}

TEST(physics, raycast_misses_beyond_max_t)
{
    Rig rig;
    const Ray down{{0.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
    PhysRayHit hit{};
    CHECK(!rig.world.raycast(down, 5.0f, &hit));
}

TEST(physics, raycast_ignores_dynamic_bodies_and_sees_static_boxes)
{
    Rig rig;
    spawn_box(rig.world, Vec3{0.0f, 2.0f, 0.0f}, Vec3{0.5f, 0.5f, 0.5f});
    rig.world.add_static_box(Vec3{6.0f, 1.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f}, 0);
    rig.world.statics_build(rig.arena);
    run(rig.world, 1);

    PhysRayHit hit{};
    const Ray through{{0.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
    CHECK(rig.world.raycast(through, 100.0f, &hit));
    CHECK_NEAR(hit.t, 10.0f, 1e-2);

    const Ray onto_box{{6.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
    CHECK(rig.world.raycast(onto_box, 100.0f, &hit));
    CHECK_NEAR(hit.t, 8.0f, 1e-2);
    CHECK(hit.normal.y > 0.9f);
}

TEST(physics, static_triangles_block_a_falling_body)
{
    Rig rig;
    rig.world.statics_reserve(rig.arena, 8);
    const f32 y = 3.0f;
    rig.world.add_static_tri(Vec3{-4.0f, y, -4.0f}, Vec3{4.0f, y, 4.0f}, Vec3{4.0f, y, -4.0f});
    rig.world.add_static_tri(Vec3{-4.0f, y, -4.0f}, Vec3{-4.0f, y, 4.0f}, Vec3{4.0f, y, 4.0f});
    rig.world.statics_build(rig.arena);
    CHECK(rig.world.statics().built());

    const BodyHandle h = spawn_box(rig.world, Vec3{0.0f, 8.0f, 0.0f}, Vec3{0.5f, 0.5f, 0.5f});
    run(rig.world, 600);

    const RigidBody* body = rig.world.body(h);
    CHECK(body->pos.y > y);
    CHECK(body->pos.y < y + 1.5f);
}

TEST(physics, a_static_box_blocks_a_falling_body)
{
    Rig rig;
    rig.world.add_static_box(Vec3{0.0f, 1.0f, 0.0f}, Vec3{2.0f, 1.0f, 2.0f}, 0);
    rig.world.statics_build(rig.arena);

    const BodyHandle h = spawn_box(rig.world, Vec3{0.0f, 6.0f, 0.0f}, Vec3{0.5f, 0.5f, 0.5f});
    run(rig.world, 600);
    CHECK(rig.world.body(h)->pos.y > 2.0f);
    CHECK(rig.world.body(h)->pos.y < 3.0f);
}

TEST(physics, reshaping_keeps_the_handle_and_the_motion)
{
    Rig rig;
    const BodyHandle h = spawn_box(rig.world, Vec3{0.0f, 3.0f, 0.0f}, Vec3{0.5f, 0.5f, 0.5f});
    RigidBody* body = rig.world.body(h);
    body->vel = Vec3{3.0f, 0.0f, 0.0f};
    run(rig.world, 1);
    const u32 before = body->jolt_id;

    BodyDesc desc;
    desc.half_extents = Vec3{0.3f, 0.3f, 0.3f};
    desc.mass = 2.0f;
    desc.inertia_diag = solid_box_inertia(desc.half_extents, desc.mass);
    rig.world.body_reshape(h, desc);

    CHECK(rig.world.body(h) == body);
    CHECK(body->jolt_id != before);
    CHECK_NEAR(body->inv_mass, 0.5f, 1e-6);
    run(rig.world, 1);
    CHECK(body->vel.x > 2.5f);
    CHECK(rig.jolt.dynamicCount() == 1);
}

TEST(physics, exhausting_the_pool_is_diagnosed)
{
    Rig rig;
    u32 created = 0;
    for (u32 i = 0; i < PhysWorld::kMaxBodies + 4; i++) {
        if (spawn_box(rig.world, Vec3{0.0f, 100.0f + static_cast<f32>(i), 0.0f},
                      Vec3{0.2f, 0.2f, 0.2f})
                .valid()) {
            created++;
        }
    }
    CHECK(created == PhysWorld::kMaxBodies);
    CHECK(rig.jolt.dynamicCount() == PhysWorld::kMaxBodies);
}

TEST(physics, destroying_a_body_removes_it_from_the_solver)
{
    Rig rig;
    const BodyHandle h = spawn_box(rig.world, Vec3{0.0f, 2.0f, 0.0f}, Vec3{0.5f, 0.5f, 0.5f});
    CHECK(rig.jolt.dynamicCount() == 1);
    rig.world.body_destroy(h);
    CHECK(rig.jolt.dynamicCount() == 0);
    CHECK(rig.world.body(h) == nullptr);
}
