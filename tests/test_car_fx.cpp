#include "test.h"

#include "carsys/car_lights.h"
#include "render/mesh_glow.h"

using namespace anom;

TEST(car_fx, the_glow_table_lights_named_meshes_only)
{
    const MeshGlowTable table = parse_mesh_glow(R"({"_comment": "x", "meshes": {"excel_brakelight": 6.0, "dim": -2}})");
    CHECK_NEAR(mesh_glow(table, "excel_brakelight"), 6.0f, 1e-6);
    CHECK_NEAR(mesh_glow(table, "dim"), 0.0f, 1e-6);
    CHECK_NEAR(mesh_glow(table, "excel_body"), 0.0f, 1e-6);
    CHECK(parse_mesh_glow("not json").entries.empty());
}

TEST(car_fx, the_cars_own_lights_come_first_and_the_list_is_capped)
{
    PointLight car[2];
    car[0].cone = 0.9f;
    car[1].color = Vec3{1.0f, 0.0f, 0.0f};
    PointLight extra[10];
    for (u32 i = 0; i < 10; i++) {
        extra[i].pos = Vec3{static_cast<f32>(i), 0.0f, 0.0f};
    }
    PointLight out[8];
    const u32 n = merge_lights(car, 2, extra, 10, out, 8);
    CHECK(n == 8u);
    CHECK_NEAR(out[0].cone, 0.9f, 1e-6);
    CHECK_NEAR(out[1].color.x, 1.0f, 1e-6);
    CHECK_NEAR(out[2].pos.x, 0.0f, 1e-6);
    CHECK_NEAR(out[7].pos.x, 5.0f, 1e-6);
    CHECK(out[2].cone < -1.0f);
}

#include "game/fx/car_fx.h"

namespace {
ghost::game::WheelSample skidding(glm::vec3 at, float slide = 8.0f, bool road = true)
{
    ghost::game::WheelSample s;
    s.contact = at;
    s.slideLong = slide;
    s.slipRatio = slide / 4.0f;
    s.grounded = true;
    s.road = road;
    return s;
}
}

TEST(car_fx, gripping_wheels_make_no_smoke_and_skidding_ones_do)
{
    using namespace ghost::game;
    CarSmoke smoke;
    WheelSample gripping[4];
    for (WheelSample& w : gripping) {
        w.grounded = true;
        w.slideLong = 0.5f;
    }
    for (int i = 0; i < 120; i++) {
        smoke.emitWheels(gripping, glm::vec3(0.0f), glm::vec3(0, 1, 0), 1.0f / 120.0f);
    }
    CHECK(smoke.puffs().empty());

    WheelSample spinning[4] = {gripping[0], gripping[1], skidding({0, 0, 0}), skidding({1, 0, 0}, 8.0f, false)};
    for (int i = 0; i < 60; i++) {
        smoke.emitWheels(spinning, glm::vec3(0.0f), glm::vec3(0, 1, 0), 1.0f / 120.0f);
    }
    CHECK(smoke.puffs().size() > 6u);
    bool dust = false;
    for (const CarPuff& p : smoke.puffs()) {
        dust = dust || p.kind == PuffKind::Dust;
    }
    CHECK(dust);
}

TEST(car_fx, smoke_rises_and_dies_and_the_pool_is_capped)
{
    using namespace ghost::game;
    CarSmoke smoke;
    WheelSample wheels[4] = {skidding({0, 0, 0}), skidding({1, 0, 0}), skidding({0, 0, 2}), skidding({1, 0, 2})};
    for (int i = 0; i < 600; i++) {
        smoke.emitWheels(wheels, glm::vec3(0.0f), glm::vec3(0, 1, 0), 1.0f / 120.0f);
        smoke.update(1.0f / 120.0f, glm::vec3(0, 1, 0), glm::vec3(0.0f));
    }
    CHECK(smoke.puffs().size() <= CarSmoke::kMaxPuffs);
    float highest = 0.0f;
    for (const CarPuff& p : smoke.puffs()) {
        highest = std::max(highest, p.center.y);
    }
    CHECK(highest > 0.5f);
    for (int i = 0; i < 600; i++) {
        smoke.update(1.0f / 120.0f, glm::vec3(0, 1, 0), glm::vec3(0.0f));
    }
    CHECK(smoke.puffs().empty());
}

TEST(car_fx, steam_only_comes_while_the_engine_is_hot)
{
    using namespace ghost::game;
    CarSmoke smoke;
    for (int i = 0; i < 240; i++) {
        smoke.emitSteam(glm::vec3(0.0f), 0.0f, glm::vec3(0, 1, 0), 1.0f / 120.0f);
    }
    CHECK(smoke.puffs().empty());
    for (int i = 0; i < 240; i++) {
        smoke.emitSteam(glm::vec3(0.0f), 10.0f, glm::vec3(0, 1, 0), 1.0f / 120.0f);
    }
    CHECK(!smoke.puffs().empty());
    CHECK(smoke.puffs()[0].kind == PuffKind::Steam);
}

TEST(car_fx, a_straight_burnout_lays_a_few_long_marks_not_one_per_tick)
{
    using namespace ghost::game;
    SkidMarks marks;
    for (int i = 0; i < 240; i++) {
        marks.sample(0, skidding({0.0f, 0.0f, -0.05f * static_cast<float>(i)}));
    }
    const std::size_t count = marks.segments().size();
    CHECK(count >= 7u);
    CHECK(count <= 10u);
    float covered = 0.0f;
    for (const SkidSegment& s : marks.segments()) {
        covered += s.halfLength * 2.0f;
        CHECK(std::abs(s.along.z) > 0.99f);
    }
    CHECK(covered > 11.5f);
}

TEST(car_fx, a_turn_a_jump_or_lifting_off_breaks_the_mark)
{
    using namespace ghost::game;
    SkidMarks marks;
    for (int i = 0; i < 10; i++) {
        marks.sample(0, skidding({0.0f, 0.0f, -0.05f * static_cast<float>(i)}));
    }
    CHECK(marks.segments().size() == 1u);
    for (int i = 1; i <= 10; i++) {
        marks.sample(0, skidding({0.05f * static_cast<float>(i), 0.0f, -0.45f}));
    }
    CHECK(marks.segments().size() == 2u);
    marks.sample(0, skidding({20.0f, 0.0f, 0.0f}));
    CHECK(marks.segments().size() == 3u);
    WheelSample air = skidding({20.0f, 0.0f, -0.05f});
    air.grounded = false;
    marks.sample(0, air);
    for (int i = 0; i < SkidMarks::kStartTicks; i++) {
        marks.sample(0, skidding({20.0f, 0.0f, -0.1f - 0.05f * static_cast<float>(i)}));
    }
    CHECK(marks.segments().size() == 4u);
    marks.sample(0, skidding({20.0f, 0.0f, -0.4f}, 1.0f));
    for (int i = 0; i < SkidMarks::kStartTicks; i++) {
        marks.sample(0, skidding({20.0f, 0.0f, -0.45f - 0.05f * static_cast<float>(i)}));
    }
    CHECK(marks.segments().size() == 5u);
}

TEST(car_fx, one_tick_slip_spikes_leave_no_dots_and_a_spin_in_place_lays_a_patch_along_the_wheel)
{
    using namespace ghost::game;
    SkidMarks marks;
    for (int i = 0; i < 40; i++) {
        marks.sample(0, skidding({0.0f, 0.0f, -0.05f * static_cast<float>(i)}, i % 5 == 0 ? 6.0f : 0.5f));
    }
    CHECK(marks.segments().empty());
    WheelSample spin = skidding({3.0f, 0.0f, 0.0f});
    spin.forward = glm::vec3(1.0f, 0.0f, 0.0f);
    for (int i = 0; i < 30; i++) {
        marks.sample(1, spin);
    }
    CHECK(marks.segments().size() == 1u);
    CHECK(marks.segments()[0].along.x > 0.99f);
    CHECK(marks.segments()[0].halfLength >= marks.segments()[0].halfWidth * 0.5f);
}

TEST(car_fx, a_skid_that_flickers_around_the_threshold_keeps_one_mark)
{
    using namespace ghost::game;
    SkidMarks marks;
    for (int i = 0; i < 24; i++) {
        marks.sample(0, skidding({0.0f, 0.0f, -0.05f * static_cast<float>(i)}, i < 4 || i % 2 == 0 ? 4.0f : 3.0f));
    }
    CHECK(marks.segments().size() == 1u);
}

TEST(car_fx, marks_fade_out_and_the_ring_is_capped)
{
    using namespace ghost::game;
    SkidMarks marks;
    for (int i = 0; i < 3000; i++) {
        marks.sample(static_cast<std::size_t>(i % 4), skidding({static_cast<float>(i) * 3.0f, 0.0f, 0.0f}));
    }
    CHECK(marks.segments().size() == SkidMarks::kMaxSegments);
    marks.update(30.0f);
    CHECK(marks.fade(marks.segments()[0]) > 0.99f);
    marks.update(40.0f);
    CHECK(marks.fade(marks.segments()[0]) < 0.01f);
}

TEST(car_fx, off_road_marks_are_tagged_and_the_tuning_reads_from_data)
{
    using namespace ghost::game;
    SkidMarks marks;
    for (int i = 0; i < SkidMarks::kStartTicks; i++) {
        marks.sample(1, skidding({0, 0, -0.05f * static_cast<float>(i)}, 8.0f, false));
    }
    CHECK(marks.segments().size() == 1u);
    if (!marks.segments().empty()) {
        CHECK(!marks.segments()[0].road);
    }
    const CarFxTuning t = parseCarFxTuning(R"({"marks": {"halfWidth": 0.2, "life": 10, "fadeStart": 20}})");
    CHECK_NEAR(t.markHalfWidth, 0.2f, 1e-6);
    CHECK(t.markLife > t.markFadeStart);
    CHECK_NEAR(parseCarFxTuning("nope").smokeRate, CarFxTuning{}.smokeRate, 1e-6);
}

TEST(car_fx, hard_cornering_leaves_nothing_but_a_full_drift_or_a_burnout_does)
{
    using namespace ghost::game;
    const CarFxTuning t;
    WheelSample corner;
    corner.grounded = true;
    corner.slipAngle = glm::radians(8.0f);
    corner.slideLat = 4.0f;
    corner.slipRatio = 0.1f;
    corner.slideLong = 3.0f;
    CHECK(skidIntensity(corner, t) == 0.0f);

    WheelSample plowing = corner;
    plowing.steered = true;
    plowing.slipAngle = glm::radians(24.0f);
    plowing.slideLat = 9.0f;
    CHECK(skidIntensity(plowing, t) == 0.0f);

    WheelSample drift = corner;
    drift.slipAngle = glm::radians(35.0f);
    drift.slideLat = 8.0f;
    CHECK(skidIntensity(drift, t) > 0.9f);

    WheelSample burnout;
    burnout.grounded = true;
    burnout.slipRatio = 3.0f;
    burnout.slideLong = 6.0f;
    CHECK(skidIntensity(burnout, t) > 0.9f);

    WheelSample launch = burnout;
    launch.slipRatio = 0.25f;
    launch.slideLong = 1.0f;
    CHECK(skidIntensity(launch, t) == 0.0f);

    burnout.grounded = false;
    CHECK(skidIntensity(burnout, t) == 0.0f);
}

#include "game/fx/poncho_cloth.h"

namespace {
float hem_mean_x(const ghost::game::PonchoCloth& cloth)
{
    float sum = 0.0f;
    for (const glm::vec3& o : cloth.offsets()) {
        sum += o.x;
    }
    return sum / static_cast<float>(ghost::game::PonchoCloth::kNodes);
}
}

TEST(poncho, a_standing_body_lets_the_hem_hang_still)
{
    ghost::game::PonchoCloth cloth;
    for (int i = 0; i < 600; i++) {
        cloth.step(glm::vec3(0.0f, 1.0f, 0.0f), glm::mat3(1.0f), glm::vec3(0.0f, -9.81f, 0.0f), glm::vec3(0.0f), 1.0f / 60.0f);
    }
    for (const glm::vec3& o : cloth.offsets()) {
        CHECK(glm::length(o) < 1e-3f);
    }
}

TEST(poncho, a_body_that_sets_off_leaves_the_hem_behind_and_it_settles)
{
    ghost::game::PonchoCloth cloth;
    glm::vec3 at(0.0f);
    const float dt = 1.0f / 60.0f;
    float lag = 0.0f;
    for (int i = 0; i < 30; i++) {
        at.x += 5.0f * dt * std::min(1.0f, static_cast<float>(i) / 10.0f);
        cloth.step(at, glm::mat3(1.0f), glm::vec3(0.0f, -9.81f, 0.0f), glm::vec3(0.0f), dt);
        lag = std::min(lag, hem_mean_x(cloth));
    }
    CHECK(lag < -0.05f);
    for (int i = 0; i < 600; i++) {
        cloth.step(at, glm::mat3(1.0f), glm::vec3(0.0f, -9.81f, 0.0f), glm::vec3(0.0f), dt);
    }
    CHECK(std::abs(hem_mean_x(cloth)) < 0.01f);
}

TEST(poncho, wind_blows_the_hem_downwind_and_nothing_escapes_its_reach)
{
    ghost::game::PonchoCloth cloth;
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 600; i++) {
        cloth.step(glm::vec3(0.0f), glm::mat3(1.0f), glm::vec3(0.0f, -9.81f, 0.0f), glm::vec3(6.0f, 0.0f, 0.0f), dt);
    }
    CHECK(hem_mean_x(cloth) > 0.05f);
    for (int i = 0; i < 200; i++) {
        cloth.step(glm::vec3(static_cast<float>(i % 2) * 3.0f, 0.0f, 0.0f), glm::mat3(1.0f), glm::vec3(0.0f, -9.81f, 0.0f),
                   glm::vec3(30.0f, 0.0f, 0.0f), 0.1f);
    }
    for (const glm::vec3& o : cloth.offsets()) {
        CHECK(std::isfinite(o.x) && std::isfinite(o.y) && std::isfinite(o.z));
        CHECK(glm::length(glm::vec2(o.x, o.z)) <= ghost::game::PonchoTuning{}.reach + 1e-4f);
    }
}
