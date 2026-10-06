#include "test.h"

#include "game/fx/draw_order.h"
#include "render/haze.h"

#include <cmath>
#include <limits>
#include <vector>

using namespace anom;

namespace {

constexpr f32 kMargin = 40.0f;
constexpr f32 kDensity = 0.02f;

const Vec4 kSpheres[2] = {Vec4{0.0f, 20.0f, -80.0f, 50.0f}, Vec4{60.0f, 30.0f, -150.0f, 35.0f}};

f32 whole(Vec3 eye, Vec3 dir, f32 dist)
{
    return haze_optical_depth(kSpheres, 2, kMargin, kDensity, eye, dir, dist, 0.0f, dist);
}

f32 sliced(Vec3 eye, Vec3 dir, f32 dist, std::vector<float> things)
{
    f32 sum = 0.0f;
    for (const ghost::game::DrawWindow& w : ghost::game::drawSlices(std::move(things))) {
        const HazeSpan span = haze_window_span(kSpheres, 2, eye, w.nearD, w.farD);
        const f32 end = f_min(span.end, dist);
        if (end > span.start) {
            sum += haze_optical_depth(kSpheres, 2, kMargin, kDensity, eye, dir, dist, span.start, end);
        }
    }
    return sum;
}

struct HazeRay {
    Vec3 eye;
    Vec3 dir;
    f32 dist;
};

}

TEST(haze, splitting_the_ray_into_draw_windows_adds_up_to_the_whole)
{
    const HazeRay rays[] = {
        {Vec3{0.0f, 10.0f, 40.0f}, normalize(Vec3{0.0f, 0.1f, -1.0f}), 600.0f},
        {Vec3{0.0f, 20.0f, -80.0f}, normalize(Vec3{0.3f, 0.0f, -1.0f}), 600.0f},
        {Vec3{0.0f, 20.0f, -80.0f}, normalize(Vec3{0.0f, 0.0f, -1.0f}), 12.0f},
        {Vec3{-49.9f, 20.0f, 40.0f}, Vec3{0.0f, 0.0f, -1.0f}, 600.0f},
        {Vec3{30.0f, 15.0f, 10.0f}, normalize(Vec3{0.2f, 0.1f, -1.0f}), 95.0f},
    };
    const std::vector<float> things = {5.0f, 33.0f, 70.0f, 71.0f, 120.0f, 400.0f};
    for (const HazeRay& r : rays) {
        const f32 a = whole(r.eye, r.dir, r.dist);
        const f32 b = sliced(r.eye, r.dir, r.dist, things);
        CHECK(std::isfinite(b));
        CHECK(std::fabs(a - b) <= 1e-4f * f_max(a, 1.0f));
        CHECK(std::fabs(a - sliced(r.eye, r.dir, r.dist, {})) <= 1e-4f * f_max(a, 1.0f));
    }
}

TEST(haze, compositing_windows_far_to_near_matches_one_pass)
{
    const Vec3 eye{0.0f, 10.0f, 40.0f};
    const Vec3 dir = normalize(Vec3{0.05f, 0.1f, -1.0f});
    const f32 dist = 600.0f;
    const f32 scene = 0.25f;
    const f32 lit = 0.8f;
    f32 dst = scene;
    for (const ghost::game::DrawWindow& w : ghost::game::drawSlices({20.0f, 60.0f, 90.0f, 130.0f})) {
        const HazeSpan span = haze_window_span(kSpheres, 2, eye, w.nearD, w.farD);
        if (span.empty()) {
            continue;
        }
        const f32 a = 1.0f - std::exp(-haze_optical_depth(kSpheres, 2, kMargin, kDensity, eye, dir, dist, span.start, span.end));
        dst = lit * a + dst * (1.0f - a);
    }
    const f32 t = std::exp(-whole(eye, dir, dist));
    CHECK(std::fabs(dst - (scene * t + lit * (1.0f - t))) <= 1e-4f);
}

TEST(haze, window_spans_are_finite_and_clamped)
{
    const Vec3 eye{0.0f, 10.0f, 40.0f};
    const HazeSpan all = haze_window_span(kSpheres, 2, eye, -1.0f, std::numeric_limits<f32>::infinity());
    CHECK(all.start == 0.0f);
    CHECK(std::isfinite(all.end));
    CHECK(all.end > 150.0f);
    CHECK(haze_window_span(kSpheres, 2, eye, all.end + 1.0f, all.end + 50.0f).empty());
    const Vec3 inside{0.0f, 20.0f, -80.0f};
    const HazeSpan nearest = haze_window_span(kSpheres, 2, inside, -1.0f, 3.0f);
    CHECK(!nearest.empty());
    CHECK(haze_sphere_touches(kSpheres[0], inside, nearest));
    CHECK(!haze_sphere_touches(kSpheres[1], eye, HazeSpan{0.0f, 10.0f}));
}

TEST(haze, degenerate_rays_stay_finite)
{
    const Vec4 tiny[1] = {Vec4{0.0f, 0.0f, -10.0f, 0.0f}};
    const f32 a = haze_optical_depth(tiny, 1, kMargin, kDensity, Vec3{}, Vec3{0.0f, 0.0f, -1.0f}, 100.0f, 0.0f, 100.0f);
    CHECK(std::isfinite(a) && a >= 0.0f);
    const f32 graze = haze_optical_depth(kSpheres, 2, kMargin, kDensity, Vec3{50.0f, 20.0f, 0.0f}, Vec3{0.0f, 0.0f, -1.0f},
                                         600.0f, 0.0f, 600.0f);
    CHECK(std::isfinite(graze) && graze >= 0.0f);
    const f32 zero = haze_optical_depth(kSpheres, 2, kMargin, kDensity, Vec3{0.0f, 20.0f, -80.0f}, Vec3{1.0f, 0.0f, 0.0f},
                                        0.0f, 0.0f, 0.0f);
    CHECK(zero == 0.0f);
}
