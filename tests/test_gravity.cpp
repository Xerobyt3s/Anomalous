#include "test.h"

#include "core/arena.h"
#include "physics/body.h"
#include "physics/gravity_field.h"
#include "physics/heightfield.h"
#include "physics/jolt_world.h"
#include "physics/static_grid.h"
#include "physics/world.h"
#include "vehicle/vehicle.h"
#include "player/movement.h"
#include "render/camera.h"

using namespace anom;

namespace {

constexpr f32 kDt = 1.0f / 120.0f;

Quat roll_deg(f32 deg)
{
    return quat_from_euler(0.0f, 0.0f, deg * kDegToRad);
}

GravityVolume box_volume(Vec3 pos, Quat rot, Vec3 half, f32 falloff)
{
    GravityVolume v;
    v.pos = pos;
    v.rot = rot;
    v.half = half;
    v.falloff = falloff;
    return v;
}

struct FlatRig {
    Arena arena{megabytes(32)};
    Heightfield hf;
    PhysWorld world;
    JoltWorld jolt;
    Movement move;

    void setup(Vec3 feet = Vec3{0.0f, 0.0f, 0.0f})
    {
        hf.init_procedural(arena, 96, 1.0f, 5u, 0.0f);
        world.init(arena, &hf);
        world.set_jolt(&jolt);
        move.init(&jolt, feet, Vec3{0.0f, 1.0f, 0.0f}, 0.0f);
    }

    void run(const MoveCommand& cmd, i32 ticks, const GravityField* field = nullptr)
    {
        for (i32 i = 0; i < ticks; i++) {
            move.tick(cmd, field, kDt);
        }
    }

    f32 planar_speed() const
    {
        const Vec3 v = move.state().vel;
        return length(Vec3{v.x, 0.0f, v.z});
    }
};

MoveCommand forward_cmd()
{
    MoveCommand cmd;
    cmd.move = Vec2{0.0f, 1.0f};
    return cmd;
}

} // namespace

TEST(gravity_field, outside_every_volume_gravity_is_plain_down)
{
    GravityField field;
    field.add(box_volume(Vec3{0.0f, 0.0f, 0.0f}, roll_deg(90.0f), Vec3{2.0f, 2.0f, 2.0f}, 3.0f));
    const GravitySample s = field.sample(Vec3{50.0f, 0.0f, 0.0f});
    CHECK_NEAR(s.gravity.y, -kDefaultGravity, 1e-4);
    CHECK_NEAR(s.up.y, 1.0f, 1e-5);
    CHECK_NEAR(s.presence, 0.0f, 1e-6);
}

TEST(gravity_field, inside_a_volume_down_is_its_local_down)
{
    GravityField field;
    field.add(box_volume(Vec3{0.0f, 0.0f, 0.0f}, roll_deg(90.0f), Vec3{2.0f, 2.0f, 2.0f}, 3.0f));
    const GravitySample s = field.sample(Vec3{0.5f, 0.5f, 0.0f});
    CHECK_NEAR(s.gravity.x, kDefaultGravity, 1e-3);
    CHECK_NEAR(s.gravity.y, 0.0f, 1e-3);
    CHECK_NEAR(s.presence, 1.0f, 1e-6);
}

TEST(gravity_field, the_falloff_band_turns_gravity_part_way)
{
    GravityField field;
    field.add(box_volume(Vec3{0.0f, 0.0f, 0.0f}, roll_deg(90.0f), Vec3{2.0f, 2.0f, 2.0f}, 4.0f));
    const GravitySample s = field.sample(Vec3{4.0f, 0.0f, 0.0f});
    CHECK(s.presence > 0.3f);
    CHECK(s.presence < 0.7f);
    CHECK(s.gravity.x > 1.0f);
    CHECK(s.gravity.y < -1.0f);
    CHECK_NEAR(length(s.gravity), kDefaultGravity, 1e-3);
}

TEST(gravity_field, opposite_gravity_turns_through_a_side_and_never_cancels)
{
    GravityField field;
    field.add(box_volume(Vec3{0.0f, 0.0f, 0.0f}, roll_deg(180.0f), Vec3{2.0f, 2.0f, 2.0f}, 4.0f));
    for (f32 x = 2.0f; x <= 6.0f; x += 0.25f) {
        const GravitySample s = field.sample(Vec3{x, 0.0f, 0.0f});
        CHECK_NEAR(length(s.gravity), kDefaultGravity, 1e-3);
        CHECK_NEAR(length(s.up), 1.0f, 1e-4);
    }
    const GravitySample mid = field.sample(Vec3{4.0f, 0.0f, 0.0f});
    CHECK(f_abs(mid.up.y) < 0.9f);
}

TEST(gravity_field, a_higher_priority_volume_wins_where_they_overlap)
{
    GravityField field;
    GravityVolume high = box_volume(Vec3{0.0f, 0.0f, 0.0f}, roll_deg(-90.0f), Vec3{3.0f, 3.0f, 3.0f}, 1.0f);
    high.priority = 5;
    field.add(high);
    field.add(box_volume(Vec3{0.0f, 0.0f, 0.0f}, roll_deg(90.0f), Vec3{3.0f, 3.0f, 3.0f}, 1.0f));
    const GravitySample s = field.sample(Vec3{0.0f, 0.0f, 0.0f});
    CHECK_NEAR(s.gravity.x, -kDefaultGravity, 1e-3);
}

TEST(gravity_field, a_sphere_volume_uses_its_radius)
{
    GravityField field;
    GravityVolume v = box_volume(Vec3{0.0f, 0.0f, 0.0f}, roll_deg(180.0f), Vec3{3.0f, 0.0f, 0.0f}, 0.0f);
    v.shape = GravityShape::Sphere;
    field.add(v);
    CHECK_NEAR(field.sample(Vec3{2.9f, 0.0f, 0.0f}).up.y, -1.0f, 1e-4);
    CHECK_NEAR(field.sample(Vec3{3.1f, 0.0f, 0.0f}).up.y, 1.0f, 1e-4);
}

TEST(gravity_field, a_curl_turns_down_into_sideways_round_its_quarter)
{
    GravityField field;
    GravityVolume v = box_volume(Vec3{0.0f, 10.0f, 0.0f}, quat_identity(), Vec3{11.0f, 11.0f, 5.0f}, 0.0f);
    v.mode = GravityMode::Curl;
    field.add(v);
    const GravitySample bottom = field.sample(Vec3{0.01f, 0.5f, 0.0f});
    CHECK_NEAR(bottom.up.y, 1.0f, 1e-2);
    const GravitySample mid = field.sample(Vec3{6.7f, 3.3f, 0.0f});
    CHECK_NEAR(mid.up.x, -0.7071f, 1e-2);
    CHECK_NEAR(mid.up.y, 0.7071f, 1e-2);
    const GravitySample top = field.sample(Vec3{9.5f, 9.99f, 0.0f});
    CHECK_NEAR(top.up.x, -1.0f, 1e-2);
    CHECK_NEAR(field.sample(Vec3{-2.0f, 0.5f, 0.0f}).presence, 0.0f, 1e-6);
    CHECK_NEAR(field.sample(Vec3{5.0f, 12.0f, 0.0f}).presence, 0.0f, 1e-6);
}

TEST(gravity_field, a_partial_curl_only_covers_its_sector)
{
    GravityField field;
    GravityVolume v = box_volume(Vec3{0.0f, 10.0f, 0.0f}, quat_identity(), Vec3{11.0f, 11.0f, 5.0f}, 0.0f);
    v.mode = GravityMode::Curl;
    v.sector = 42.0f * kDegToRad;
    field.add(v);
    const f32 inside = 30.0f * kDegToRad;
    const GravitySample a = field.sample(Vec3{std::sin(inside) * 9.5f, 10.0f - std::cos(inside) * 9.5f, 0.0f});
    CHECK_NEAR(a.presence, 1.0f, 1e-6);
    CHECK_NEAR(a.up.x, -std::sin(inside), 1e-3);
    const f32 outside = 60.0f * kDegToRad;
    CHECK_NEAR(field.sample(Vec3{std::sin(outside) * 9.5f, 10.0f - std::cos(outside) * 9.5f, 0.0f}).presence,
               0.0f, 1e-6);
}

TEST(gravity_field, a_point_volume_pulls_toward_its_centre)
{
    GravityField field;
    GravityVolume v = box_volume(Vec3{0.0f, 50.0f, 0.0f}, quat_identity(), Vec3{8.0f, 0.0f, 0.0f}, 2.0f);
    v.shape = GravityShape::Sphere;
    v.mode = GravityMode::Point;
    field.add(v);
    const GravitySample side = field.sample(Vec3{6.0f, 50.0f, 0.0f});
    CHECK_NEAR(side.up.x, 1.0f, 1e-4);
    const GravitySample under = field.sample(Vec3{0.0f, 44.0f, 0.0f});
    CHECK_NEAR(under.up.y, -1.0f, 1e-4);
}

TEST(gravity_physics, a_body_in_a_sideways_volume_falls_sideways)
{
    Arena arena{megabytes(16)};
    Heightfield hf;
    hf.init_procedural(arena, 64, 1.0f, 3u, 0.0f);
    PhysWorld world;
    world.init(arena, &hf);
    GravityField field;
    field.add(box_volume(Vec3{0.0f, 10.0f, 0.0f}, roll_deg(90.0f), Vec3{8.0f, 8.0f, 8.0f}, 2.0f));
    world.set_gravity_field(&field);

    const BodyHandle h = world.body_create_box(Vec3{0.0f, 10.0f, 0.0f}, quat_identity(),
                                               Vec3{0.3f, 0.3f, 0.3f}, 10.0f);
    for (i32 i = 0; i < 30; i++) {
        world.tick(kDt);
    }
    const RigidBody* body = world.body(h);
    CHECK(body->vel.x > 2.0f);
    CHECK(f_abs(body->vel.y) < 0.2f);
}

TEST(gravity_physics, a_sleeping_body_wakes_when_its_gravity_changes)
{
    Arena arena{megabytes(16)};
    Heightfield hf;
    hf.init_procedural(arena, 64, 1.0f, 3u, 0.0f);
    PhysWorld world;
    world.init(arena, &hf);
    GravityField field;
    world.set_gravity_field(&field);

    const BodyHandle h = world.body_create_box(Vec3{0.0f, 0.5f, 0.0f}, quat_identity(),
                                               Vec3{0.3f, 0.3f, 0.3f}, 10.0f);
    for (i32 i = 0; i < 600; i++) {
        world.tick(kDt);
    }
    CHECK(world.body(h)->asleep);

    field.add(box_volume(Vec3{0.0f, 0.0f, 0.0f}, roll_deg(180.0f), Vec3{4.0f, 4.0f, 4.0f}, 1.0f));
    for (i32 i = 0; i < 60; i++) {
        world.tick(kDt);
    }
    CHECK(!world.body(h)->asleep);
    CHECK(world.body(h)->pos.y > 1.0f);
}

TEST(gravity_frame, turning_up_about_the_view_axis_keeps_the_heading)
{
    const Quat frame = quat_identity();
    const Quat turned = frame_turn_up(frame, Vec3{1.0f, 0.0f, 0.0f});
    const Vec3 fwd = frame_forward(turned, 0.0f);
    CHECK_NEAR(fwd.x, 0.0f, 1e-5);
    CHECK_NEAR(fwd.y, 0.0f, 1e-5);
    CHECK_NEAR(fwd.z, -1.0f, 1e-5);
    CHECK_NEAR(frame_up(turned).x, 1.0f, 1e-5);
}

TEST(gravity_frame, view_angles_round_trip_through_a_tilted_frame)
{
    const Quat frame = frame_turn_up(quat_identity(), normalize(Vec3{0.3f, 0.2f, -0.9f}));
    const Vec3 dir = frame_view(frame, 0.7f, -0.3f);
    f32 yaw = 0.0f;
    f32 pitch = 0.0f;
    frame_view_angles(frame, dir, yaw, pitch);
    CHECK_NEAR(yaw, 0.7f, 1e-4);
    CHECK_NEAR(pitch, -0.3f, 1e-4);
}

TEST(gravity_camera, an_identity_frame_views_like_before)
{
    Camera cam;
    cam.pos = Vec3{1.0f, 2.0f, 3.0f};
    cam.yaw = 0.4f;
    cam.pitch = -0.2f;
    const Mat4 expected = mat4_look_at(cam.pos, cam.pos + cam.forward(), Vec3{0.0f, 1.0f, 0.0f});
    const Mat4 got = cam.view();
    for (u32 i = 0; i < 16; i++) {
        CHECK_NEAR(got.m[i], expected.m[i], 1e-5);
    }
}

TEST(gravity_camera, a_frame_on_its_side_puts_up_along_the_new_up)
{
    Camera cam;
    cam.frame = quat_from_to(Vec3{0.0f, 1.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f});
    const Vec3 up = cam.up();
    CHECK_NEAR(up.x, 1.0f, 1e-5);
    CHECK_NEAR(dot(cam.forward(), Vec3{1.0f, 0.0f, 0.0f}), 0.0f, 1e-5);
}

TEST(movement, walking_settles_at_walk_speed_and_sprinting_faster)
{
    FlatRig walk;
    walk.setup();
    walk.run(forward_cmd(), 120);
    CHECK_NEAR(walk.planar_speed(), walk.move.tuning().walk_speed, 0.05);
    CHECK(walk.move.state().grounded);

    FlatRig sprint;
    sprint.setup();
    MoveCommand cmd = forward_cmd();
    cmd.sprint = true;
    sprint.run(cmd, 120);
    CHECK_NEAR(sprint.planar_speed(), sprint.move.tuning().sprint_speed, 0.05);
}

TEST(movement, letting_go_stops_quickly)
{
    FlatRig rig;
    rig.setup();
    rig.run(forward_cmd(), 120);
    rig.run(MoveCommand{}, 30);
    CHECK(rig.planar_speed() < 0.05f);
}

TEST(movement, crouching_is_slow_and_low)
{
    FlatRig rig;
    rig.setup();
    MoveCommand cmd = forward_cmd();
    cmd.crouch = true;
    rig.run(cmd, 120);
    CHECK(rig.move.state().stance == Stance::Crouch);
    CHECK(rig.planar_speed() <= rig.move.tuning().crouch_speed + 0.01f);
    CHECK(rig.move.state().eye_height < rig.move.tuning().eye_height - 0.2f);
}

TEST(movement, crouch_at_a_sprint_slides_with_a_kick)
{
    FlatRig rig;
    rig.setup();
    MoveCommand cmd = forward_cmd();
    cmd.sprint = true;
    rig.run(cmd, 120);
    cmd.crouch = true;
    rig.run(cmd, 1);
    CHECK(rig.move.state().stance == Stance::Slide);
    CHECK(rig.planar_speed() > rig.move.tuning().sprint_speed);
    rig.run(cmd, 240);
    CHECK(rig.move.state().stance == Stance::Crouch);
}

TEST(movement, a_jump_in_the_air_is_a_dive_that_lands_on_the_belly)
{
    FlatRig rig;
    rig.setup();
    rig.run(MoveCommand{}, 30);
    MoveCommand jump;
    jump.jump = true;
    rig.run(jump, 1);
    rig.run(MoveCommand{}, 10);
    CHECK(!rig.move.state().grounded);
    rig.run(jump, 1);
    CHECK(rig.move.state().stance == Stance::Dive);
    CHECK(rig.planar_speed() >= rig.move.tuning().dive_speed - 0.01f);
    rig.run(MoveCommand{}, 240);
    CHECK(rig.move.state().stance == Stance::Crawl);
    CHECK(rig.move.state().grounded);
}

TEST(movement, there_is_no_standing_up_under_a_low_beam)
{
    FlatRig rig;
    rig.setup();
    rig.jolt.add_static_box(Vec3{0.0f, 1.45f, -3.0f}, quat_identity(), Vec3{3.0f, 0.1f, 1.5f});
    MoveCommand cmd = forward_cmd();
    cmd.crouch = true;
    rig.run(cmd, 150);
    CHECK(rig.move.state().pos.z < -2.0f);
    rig.run(MoveCommand{}, 60);
    CHECK(rig.move.state().stance == Stance::Crouch);
}

TEST(movement, a_quarter_metre_ledge_is_stepped_up)
{
    FlatRig rig;
    rig.setup();
    rig.jolt.add_static_box(Vec3{0.0f, 0.125f, -6.0f}, quat_identity(), Vec3{3.0f, 0.125f, 3.0f});
    rig.run(forward_cmd(), 150);
    CHECK(rig.move.state().pos.z < -4.0f);
    CHECK_NEAR(rig.move.state().pos.y, 0.25f, 0.05);
}

TEST(movement, on_a_wall_with_its_own_gravity_you_stand_walk_and_jump_off_it)
{
    FlatRig rig;
    rig.setup();
    const Quat wall = roll_deg(90.0f);
    rig.jolt.add_static_box(Vec3{0.0f, 8.0f, 0.0f}, wall, Vec3{6.0f, 0.5f, 6.0f});
    GravityField field;
    field.add(box_volume(Vec3{-3.0f, 8.0f, 0.0f}, wall, Vec3{6.0f, 3.0f, 6.0f}, 2.0f));

    const Vec3 wall_up{-1.0f, 0.0f, 0.0f};
    rig.move.init(&rig.jolt, Vec3{-2.0f, 8.0f, 0.0f}, wall_up, 0.0f);
    rig.run(MoveCommand{}, 240, &field);
    CHECK(rig.move.state().grounded);
    CHECK_NEAR(rig.move.state().pos.x, -0.5f, 0.05);
    CHECK_NEAR(dot(rig.move.up(), wall_up), 1.0f, 1e-3);

    const Vec3 start = rig.move.state().pos;
    rig.run(forward_cmd(), 60, &field);
    CHECK(distance(start, rig.move.state().pos) > 1.5f);
    CHECK_NEAR(rig.move.state().pos.x, -0.5f, 0.05);

    MoveCommand jump;
    jump.jump = true;
    rig.run(jump, 1, &field);
    rig.run(MoveCommand{}, 10, &field);
    CHECK(rig.move.state().pos.x < -0.7f);
    rig.run(MoveCommand{}, 240, &field);
    CHECK(rig.move.state().grounded);
}

TEST(movement, flipped_gravity_in_the_air_turns_you_over_smoothly)
{
    FlatRig rig;
    rig.setup(Vec3{0.0f, 10.0f, 0.0f});
    GravityField field;
    field.add(box_volume(Vec3{0.0f, 10.0f, 0.0f}, roll_deg(180.0f), Vec3{30.0f, 50.0f, 30.0f}, 4.0f));

    f32 max_rate = 0.0f;
    f32 prev = rig.move.up().y;
    bool monotonic = true;
    for (i32 i = 0; i < 300; i++) {
        rig.move.tick(MoveCommand{}, &field, kDt);
        max_rate = f_max(max_rate, rig.move.state().up_turn_rate);
        const f32 now = rig.move.up().y;
        if (now > prev + 1e-4f) {
            monotonic = false;
        }
        prev = now;
    }
    CHECK(!rig.move.state().grounded);
    CHECK(rig.move.state().pos.y > 20.0f);
    CHECK_NEAR(rig.move.state().field_presence, 1.0f, 1e-5);
    CHECK(monotonic);
    CHECK(max_rate <= rig.move.tuning().up_turn_air + 1e-3f);
    CHECK(rig.move.up().y < -0.9f);
}

TEST(movement, standing_on_a_slope_you_stay_put_and_do_not_build_up_speed)
{
    FlatRig rig;
    rig.hf.init_slope(rig.arena, 96, 1.0f, 0.36f);
    rig.world.init(rig.arena, &rig.hf);
    rig.world.set_jolt(&rig.jolt);
    rig.move.init(&rig.jolt, Vec3{10.0f, 3.7f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, 0.0f);
    rig.run(MoveCommand{}, 60);
    const Vec3 rest = rig.move.state().pos;
    rig.run(MoveCommand{}, 240);
    CHECK(rig.move.state().grounded);
    CHECK(distance(rest, rig.move.state().pos) < 0.05f);
    CHECK(length(rig.move.state().vel) < 0.2f);
}

TEST(movement, walking_across_a_slope_stops_when_you_let_go)
{
    FlatRig rig;
    rig.hf.init_slope(rig.arena, 96, 1.0f, 0.36f);
    rig.world.init(rig.arena, &rig.hf);
    rig.world.set_jolt(&rig.jolt);
    rig.move.init(&rig.jolt, Vec3{10.0f, 3.7f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, 0.0f);
    MoveCommand downhill;
    downhill.move = Vec2{-1.0f, 0.0f};
    rig.run(downhill, 120);
    const Vec3 released = rig.move.state().pos;
    rig.run(MoveCommand{}, 120);
    CHECK(distance(released, rig.move.state().pos) < 0.6f);
    CHECK(length(rig.move.state().vel) < 0.2f);
}


namespace {

void add_box_tris(PhysWorld& world, Vec3 c, Vec3 h)
{
    const Vec3 p[8] = {
        {c.x - h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y - h.y, c.z - h.z},
        {c.x + h.x, c.y + h.y, c.z - h.z}, {c.x - h.x, c.y + h.y, c.z - h.z},
        {c.x - h.x, c.y - h.y, c.z + h.z}, {c.x + h.x, c.y - h.y, c.z + h.z},
        {c.x + h.x, c.y + h.y, c.z + h.z}, {c.x - h.x, c.y + h.y, c.z + h.z},
    };
    const u32 faces[6][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7}, {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0}};
    for (const auto& f : faces) {
        world.add_static_tri(p[f[0]], p[f[1]], p[f[2]]);
        world.add_static_tri(p[f[0]], p[f[2]], p[f[3]]);
    }
}

bool finite(Vec3 v)
{
    return v.x == v.x && v.y == v.y && v.z == v.z && f_abs(v.x) < 1e30f && f_abs(v.y) < 1e30f
        && f_abs(v.z) < 1e30f;
}

} // namespace

TEST(hardening, stepping_up_never_carries_you_through_a_thin_wall)
{
    FlatRig rig;
    rig.setup();
    rig.world.statics_reserve(rig.arena, 64);
    add_box_tris(rig.world, Vec3{0.0f, 1.0f, -3.0f}, Vec3{3.0f, 1.0f, 0.025f});
    add_box_tris(rig.world, Vec3{0.0f, 0.1f, -6.0f}, Vec3{3.0f, 0.1f, 2.95f});
    rig.world.statics_build(rig.arena);
    rig.move.attach(&rig.jolt);
    rig.run(forward_cmd(), 360);
    CHECK(rig.move.state().pos.z > -3.0f);
}

TEST(hardening, zero_strength_gravity_leaves_you_floating_with_a_valid_up)
{
    FlatRig rig;
    rig.setup(Vec3{0.0f, 10.0f, 0.0f});
    GravityField field;
    GravityVolume v = box_volume(Vec3{0.0f, 10.0f, 0.0f}, roll_deg(70.0f), Vec3{8.0f, 8.0f, 8.0f}, 2.0f);
    v.strength = 0.0f;
    field.add(v);
    rig.run(MoveCommand{}, 240, &field);
    CHECK(finite(rig.move.state().pos));
    CHECK_NEAR(length(rig.move.up()), 1.0f, 1e-4);
    CHECK_NEAR(rig.move.state().pos.y, 10.0f, 0.05);
    CHECK_NEAR(dot(rig.move.up(), field.up_at(Vec3{0.0f, 10.0f, 0.0f})), 1.0f, 1e-3);
}

TEST(hardening, recovering_in_zero_gravity_keeps_a_valid_upright_car)
{
    Arena arena{megabytes(64)};
    Heightfield hf;
    hf.init_procedural(arena, 64, 1.0f, 3u, 0.0f);
    PhysWorld phys;
    phys.init(arena, &hf);
    GravityField field;
    GravityVolume v = box_volume(Vec3{0.0f, 20.0f, 0.0f}, roll_deg(90.0f), Vec3{15.0f, 15.0f, 15.0f}, 0.0f);
    v.strength = 0.0f;
    field.add(v);
    phys.set_gravity_field(&field);
    Vehicle veh;
    CHECK(veh.init(phys, arena, "assets/cars/excel.cfg", Vec3{0.0f, 20.0f, 0.0f}, 0.0f));
    veh.recover(phys);
    for (i32 i = 0; i < 120; i++) {
        veh.tick(phys, kDt);
        phys.tick(kDt);
    }
    const RigidBody& body = *phys.body(veh.body());
    CHECK(body_state_valid(body));
    CHECK_NEAR(rotate(body.rot, Vec3{0.0f, 1.0f, 0.0f}).x, -1.0f, 1e-2);
}

TEST(hardening, a_jolt_rebuild_keeps_a_crouch_under_a_beam)
{
    FlatRig rig;
    rig.setup();
    rig.world.statics_reserve(rig.arena, 64);
    add_box_tris(rig.world, Vec3{0.0f, 1.45f, -3.0f}, Vec3{3.0f, 0.1f, 1.5f});
    rig.world.statics_build(rig.arena);
    rig.move.attach(&rig.jolt);
    MoveCommand cmd = forward_cmd();
    cmd.crouch = true;
    rig.run(cmd, 140);
    MoveCommand hold;
    hold.crouch = true;
    rig.run(hold, 30);
    const Vec3 under = rig.move.state().pos;
    CHECK(under.z < -2.0f);

    rig.world.sync_jolt();
    rig.move.attach(&rig.jolt);
    rig.run(MoveCommand{}, 60);
    CHECK(rig.move.state().stance == Stance::Crouch);
    CHECK(distance(under, rig.move.state().pos) < 0.1f);
    CHECK(rig.move.state().pos.y < 0.05f);
}

TEST(hardening, the_static_budget_counts_what_it_drops)
{
    Arena arena{megabytes(4)};
    StaticGrid grid;
    grid.reserve(arena, 4);
    for (u32 i = 0; i < 6; i++) {
        const f32 x = static_cast<f32>(i);
        grid.add(Vec3{x, 0.0f, 0.0f}, Vec3{x + 1.0f, 0.0f, 0.0f}, Vec3{x, 0.0f, 1.0f});
    }
    CHECK(grid.tri_count() == 4);
    CHECK(grid.dropped() == 2);
}

TEST(hardening, the_gravity_field_stays_sane_under_random_volumes)
{
    u32 state = 12345u;
    const auto rnd = [&state]() {
        state = state * 1664525u + 1013904223u;
        return static_cast<f32>(state >> 8) / 16777216.0f;
    };
    for (u32 round = 0; round < 20; round++) {
        GravityField field;
        const u32 count = 1 + static_cast<u32>(rnd() * 8.0f);
        for (u32 k = 0; k < count; k++) {
            GravityVolume v;
            v.pos = Vec3{(rnd() - 0.5f) * 60.0f, rnd() * 40.0f, (rnd() - 0.5f) * 60.0f};
            v.rot = quat_from_euler(rnd() * kTau, rnd() * kTau, rnd() * kTau);
            v.half = Vec3{0.5f + rnd() * 15.0f, rnd() * 15.0f, 0.5f + rnd() * 15.0f};
            v.falloff = rnd() < 0.2f ? 0.0f : rnd() * 10.0f;
            v.strength = rnd() < 0.15f ? 0.0f : rnd() * 25.0f;
            v.priority = static_cast<i32>(rnd() * 3.0f);
            v.shape = rnd() < 0.3f ? GravityShape::Sphere : GravityShape::Box;
            const f32 m = rnd();
            v.mode = m < 0.4f ? GravityMode::Directional : (m < 0.75f ? GravityMode::Curl : GravityMode::Point);
            v.sector = rnd() * kPi;
            field.add(v);
        }
        const u32 paths = static_cast<u32>(rnd() * 4.0f);
        for (u32 k = 0; k < paths; k++) {
            GravityPath path;
            path.count = 2 + static_cast<u32>(rnd() * 46.0f);
            const Vec3 origin{(rnd() - 0.5f) * 60.0f, rnd() * 40.0f, (rnd() - 0.5f) * 60.0f};
            const bool collapsed = rnd() < 0.15f;
            for (u32 i = 0; i < path.count; i++) {
                const f32 t = static_cast<f32>(i);
                path.points[i] = collapsed ? origin : origin + Vec3{t * (rnd() * 2.0f), t * (rnd() - 0.5f), t * (rnd() - 0.5f) * 2.0f};
                path.ups[i] = normalize(Vec3{rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f} + Vec3{0.0f, 1e-3f, 0.0f});
            }
            path.half_width = rnd() * 6.0f;
            path.height = rnd() * 5.0f;
            path.below = rnd() * 2.0f;
            path.falloff = rnd() < 0.2f ? 0.0f : rnd() * 4.0f;
            path.strength = rnd() * 20.0f;
            field.add_path(path);
        }
        for (u32 i = 0; i < 500; i++) {
            const Vec3 p{(rnd() - 0.5f) * 90.0f, rnd() * 60.0f - 10.0f, (rnd() - 0.5f) * 90.0f};
            const GravitySample s = field.sample(p);
            CHECK(finite(s.gravity));
            CHECK_NEAR(length(s.up), 1.0f, 1e-3);
            CHECK(s.presence >= 0.0f && s.presence <= 1.0f);
        }
    }
}

TEST(hardening, the_jolt_terrain_matches_the_engine_heightfield)
{
    for (int variant = 0; variant < 2; variant++) {
        FlatRig rig;
        if (variant == 0) {
            rig.hf.init_procedural(rig.arena, 96, 1.0f, 7u, 1.0f);
        } else {
            rig.hf.init_procedural(rig.arena, 128, 2.0f, 7u, 1.0f);
        }
        rig.world.init(rig.arena, &rig.hf);
        rig.world.set_jolt(&rig.jolt);
        f32 worst = 0.0f;
        for (int i = 0; i < 200; i++) {
            const f32 x = -20.0f + 0.37f * static_cast<f32>(i);
            const f32 z = 3.1f + 0.11f * static_cast<f32>(i % 17);
            JoltRayHit hit;
            if (rig.jolt.raycast(Vec3{x, 200.0f, z}, Vec3{x, -200.0f, z}, &hit)) {
                worst = f_max(worst, f_abs(hit.point.y - rig.hf.sample(x, z)));
            }
        }
        CHECK(worst < 0.03f);
    }
}

TEST(movement, walking_up_down_and_across_a_slope_keeps_your_heading_and_pace)
{
    for (f32 slope : {0.36f, 0.6f}) {
        const Vec2 moves[4] = {{1.0f, 0.0f}, {-1.0f, 0.0f}, {0.7f, 0.7f}, {0.0f, 1.0f}};
        for (u32 mode = 0; mode < 5; mode++) {
            FlatRig rig;
            rig.hf.init_slope(rig.arena, 96, 1.0f, slope);
            rig.world.init(rig.arena, &rig.hf);
            rig.world.set_jolt(&rig.jolt);
            const f32 yaw = mode == 4 ? -kPi * 0.5f : 0.0f;
            rig.move.init(&rig.jolt, Vec3{40.0f, slope * 40.0f + 0.3f, 30.0f}, Vec3{0.0f, 1.0f, 0.0f}, yaw);
            rig.run(MoveCommand{}, 60);
            const Vec3 start = rig.move.state().pos;
            MoveCommand cmd;
            cmd.move = mode == 4 ? Vec2{0.0f, 1.0f} : moves[mode];
            cmd.sprint = mode == 4;
            u32 air = 0;
            for (i32 i = 0; i < 240; i++) {
                rig.run(cmd, 1);
                air += rig.move.state().grounded ? 0 : 1;
            }
            const Vec3 d = rig.move.state().pos - start;
            CHECK(air == 0);
            CHECK(length(d) > (mode == 4 ? 10.0f : 7.2f));
            if (mode == 2) {
                CHECK(f_abs(d.x + d.z) < 0.15f);
            }
            if (mode == 3) {
                CHECK(f_abs(d.x) < 0.1f);
            }
        }
    }
}
