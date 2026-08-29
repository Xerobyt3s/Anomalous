#include "test.h"

#include "core/arena.h"
#include "core/rng.h"
#include "physics/collide.h"
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

} // namespace

TEST(physics, body_falls_and_settles_on_ground)
{
    Arena arena(megabytes(32));
    Heightfield hf = make_flat(arena);
    PhysWorld world;
    world.init(arena, &hf);

    const BodyHandle h = spawn_box(world, Vec3{0.0f, 6.0f, 0.0f}, Vec3{0.5f, 0.5f, 0.5f});
    CHECK(h.valid());

    run(world, 600);

    const RigidBody* body = world.body(h);
    CHECK(body != nullptr);
    CHECK(body_state_valid(*body));
    CHECK(body->pos.y > 0.0f);
    CHECK(body->pos.y < 1.2f);
    CHECK(f_abs(body->vel.y) < 0.2f);
}

TEST(physics, resting_body_does_not_sink)
{
    Arena arena(megabytes(32));
    Heightfield hf = make_flat(arena);
    PhysWorld world;
    world.init(arena, &hf);

    const BodyHandle h = spawn_box(world, Vec3{0.0f, 0.5f, 0.0f}, Vec3{0.5f, 0.5f, 0.5f});
    run(world, 240);
    const f32 settled = world.body(h)->pos.y;

    run(world, 600);
    const f32 later = world.body(h)->pos.y;

    CHECK_NEAR(settled, later, 0.02);
    CHECK(later > 0.30f);
}

TEST(physics, body_sleeps_when_at_rest)
{
    Arena arena(megabytes(32));
    Heightfield hf = make_flat(arena);
    PhysWorld world;
    world.init(arena, &hf);

    const BodyHandle h = spawn_box(world, Vec3{0.0f, 0.55f, 0.0f}, Vec3{0.5f, 0.5f, 0.5f});
    run(world, 600);
    CHECK(world.body(h)->asleep != 0);
}

TEST(physics, warm_started_stack_is_stable)
{
    Arena arena(megabytes(64));
    Heightfield hf = make_flat(arena);
    PhysWorld world;
    world.init(arena, &hf);

    BodyHandle handles[4];
    for (i32 i = 0; i < 4; i++) {
        handles[i] = spawn_box(world, Vec3{0.0f, 0.5f + static_cast<f32>(i) * 1.02f, 0.0f},
                               Vec3{0.5f, 0.5f, 0.5f});
    }

    run(world, 900);

    for (i32 i = 0; i < 4; i++) {
        const RigidBody* body = world.body(handles[i]);
        CHECK(body_state_valid(*body));
        CHECK(body->pos.y > 0.25f);
    }

    for (i32 i = 1; i < 4; i++) {
        CHECK(world.body(handles[i])->pos.y > world.body(handles[i - 1])->pos.y);
    }

    const f32 top = world.body(handles[3])->pos.y;
    CHECK(top < 5.0f);
}

TEST(physics, stack_does_not_drift_sideways)
{
    Arena arena(megabytes(64));
    Heightfield hf = make_flat(arena);
    PhysWorld world;
    world.init(arena, &hf);

    BodyHandle handles[3];
    for (i32 i = 0; i < 3; i++) {
        handles[i] = spawn_box(world, Vec3{0.0f, 0.5f + static_cast<f32>(i) * 1.02f, 0.0f},
                               Vec3{0.5f, 0.5f, 0.5f});
    }
    run(world, 900);

    for (i32 i = 0; i < 3; i++) {
        const RigidBody* body = world.body(handles[i]);
        CHECK(f_abs(body->pos.x) < 0.6f);
        CHECK(f_abs(body->pos.z) < 0.6f);
    }
}

TEST(physics, simulation_is_deterministic)
{
    const auto simulate = [](Vec3* out_positions) {
        Arena arena(megabytes(64));
        Heightfield hf = make_flat(arena);
        PhysWorld world;
        world.init(arena, &hf);

        Rng rng(1234);
        BodyHandle handles[16];
        for (i32 i = 0; i < 16; i++) {
            const Vec3 half{rng.range(0.25f, 0.8f), rng.range(0.25f, 0.8f),
                            rng.range(0.25f, 0.8f)};
            const Vec3 pos{rng.range(-2.0f, 2.0f), 3.0f + static_cast<f32>(i) * 1.5f,
                           rng.range(-2.0f, 2.0f)};
            handles[i] = spawn_box(world, pos, half);
        }
        for (i32 i = 0; i < 720; i++) {
            world.tick(kDt);
        }
        for (i32 i = 0; i < 16; i++) {
            out_positions[i] = world.body(handles[i])->pos;
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

TEST(physics, boxes_do_not_interpenetrate)
{
    Arena arena(megabytes(64));
    Heightfield hf = make_flat(arena);
    PhysWorld world;
    world.init(arena, &hf);

    const BodyHandle a = spawn_box(world, Vec3{0.0f, 0.5f, 0.0f}, Vec3{0.5f, 0.5f, 0.5f});
    const BodyHandle b = spawn_box(world, Vec3{0.4f, 0.5f, 0.0f}, Vec3{0.5f, 0.5f, 0.5f});

    run(world, 600);

    const f32 separation = distance(world.body(a)->pos, world.body(b)->pos);
    CHECK(separation > 0.85f);
}

TEST(physics, raycast_hits_flat_ground)
{
    Arena arena(megabytes(32));
    Heightfield hf = make_flat(arena);
    PhysWorld world;
    world.init(arena, &hf);

    const Ray down{{1.0f, 10.0f, 1.0f}, {0.0f, -1.0f, 0.0f}};
    PhysRayHit hit{};
    CHECK(world.raycast(down, 100.0f, &hit));
    CHECK_NEAR(hit.t, 10.0f, 1e-2);
    CHECK(hit.normal.y > 0.9f);
}

TEST(physics, raycast_misses_beyond_max_t)
{
    Arena arena(megabytes(32));
    Heightfield hf = make_flat(arena);
    PhysWorld world;
    world.init(arena, &hf);

    const Ray down{{0.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
    PhysRayHit hit{};
    CHECK(!world.raycast(down, 5.0f, &hit));
}

TEST(physics, static_triangles_block_a_falling_body)
{
    Arena arena(megabytes(64));
    Heightfield hf = make_flat(arena);
    PhysWorld world;
    world.init(arena, &hf);

    world.statics_reserve(arena, 8);
    const f32 y = 3.0f;
    world.add_static_tri(Vec3{-4.0f, y, -4.0f}, Vec3{4.0f, y, -4.0f}, Vec3{4.0f, y, 4.0f});
    world.add_static_tri(Vec3{-4.0f, y, -4.0f}, Vec3{4.0f, y, 4.0f}, Vec3{-4.0f, y, 4.0f});
    world.statics_build(arena);
    CHECK(world.statics().built());

    const BodyHandle h = spawn_box(world, Vec3{0.0f, 8.0f, 0.0f}, Vec3{0.5f, 0.5f, 0.5f});
    run(world, 600);

    const RigidBody* body = world.body(h);
    CHECK(body->pos.y > y);
    CHECK(body->pos.y < y + 1.5f);
}

TEST(physics, broadphase_only_tests_live_bodies)
{
    Arena arena(megabytes(64));
    Heightfield hf = make_flat(arena);
    PhysWorld world;
    world.init(arena, &hf);

    BodyHandle handles[8];
    for (i32 i = 0; i < 8; i++) {
        handles[i] = spawn_box(world, Vec3{static_cast<f32>(i) * 8.0f, 2.0f, 0.0f},
                               Vec3{0.5f, 0.5f, 0.5f});
    }
    world.tick(kDt);
    CHECK(world.stats().live_bodies == 8);
    CHECK(world.stats().pair_tests == 28);

    for (i32 i = 0; i < 6; i++) {
        world.body_destroy(handles[i]);
    }
    world.tick(kDt);
    CHECK(world.stats().live_bodies == 2);
    CHECK(world.stats().pair_tests == 1);
}

TEST(physics, warm_starting_reports_reuse)
{
    Arena arena(megabytes(32));
    Heightfield hf = make_flat(arena);
    PhysWorld world;
    world.init(arena, &hf);

    spawn_box(world, Vec3{0.0f, 0.5f, 0.0f}, Vec3{0.5f, 0.5f, 0.5f});
    run(world, 30);
    CHECK(world.stats().contacts > 0);
    CHECK(world.stats().warm_started > 0);

    run(world, 300);
    CHECK(world.stats().contacts == 0);
}

TEST(physics, disabling_warm_start_still_settles)
{
    Arena arena(megabytes(32));
    Heightfield hf = make_flat(arena);
    PhysWorld world;
    world.init(arena, &hf);
    world.tuning().warm_start = false;

    const BodyHandle h = spawn_box(world, Vec3{0.0f, 4.0f, 0.0f}, Vec3{0.5f, 0.5f, 0.5f});
    run(world, 600);
    CHECK(world.body(h)->pos.y > 0.0f);
    CHECK(world.body(h)->pos.y < 1.5f);
}

TEST(physics, exhausting_the_pool_is_diagnosed)
{
    Arena arena(megabytes(128));
    Heightfield hf = make_flat(arena);
    PhysWorld world;
    world.init(arena, &hf);

    u32 created = 0;
    for (u32 i = 0; i < PhysWorld::kMaxBodies + 4; i++) {
        if (spawn_box(world, Vec3{0.0f, 100.0f + static_cast<f32>(i), 0.0f},
                      Vec3{0.2f, 0.2f, 0.2f})
                .valid()) {
            created++;
        }
    }
    CHECK(created == PhysWorld::kMaxBodies);
}

TEST(collide, sphere_versus_obb_reports_shallowest_axis)
{
    RigidBody box{};
    box.pos = Vec3{0.0f, 0.0f, 0.0f};
    box.rot = quat_identity();
    box.half_extents = Vec3{1.0f, 1.0f, 1.0f};

    Sphere sphere;
    sphere.center = Vec3{1.4f, 0.0f, 0.0f};
    sphere.radius = 0.5f;

    SphereContact hit{};
    CHECK(collide_sphere_obb(sphere, box, hit));
    CHECK(hit.normal.x > 0.9f);
    CHECK_NEAR(hit.depth, 0.1f, 1e-4);

    sphere.center = Vec3{3.0f, 0.0f, 0.0f};
    CHECK(!collide_sphere_obb(sphere, box, hit));
}

TEST(collide, sphere_inside_obb_pushes_out_nearest_face)
{
    RigidBody box{};
    box.rot = quat_identity();
    box.half_extents = Vec3{1.0f, 2.0f, 3.0f};

    Sphere sphere;
    sphere.center = Vec3{0.9f, 0.0f, 0.0f};
    sphere.radius = 0.2f;

    SphereContact hit{};
    CHECK(collide_sphere_obb(sphere, box, hit));
    CHECK(hit.normal.x > 0.9f);
    CHECK(hit.depth > 0.0f);
}
