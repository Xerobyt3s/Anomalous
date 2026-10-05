#include "game/fx/mimic_body.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>

using namespace ghost::game;

namespace {
constexpr float kDt = 1.0f / 60.0f;

bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

std::optional<glm::vec3> floorHit(const glm::vec3& from, const glm::vec3& to) {
    if (from.y >= 0.0f && to.y < 0.0f) {
        return glm::mix(from, to, from.y / (from.y - to.y));
    }
    return std::nullopt;
}

bool walker(int tentacle) { return tentacle % 4 != 2; }

glm::vec3 tipOf(const MimicBody& body, int t) {
    return body.points()[static_cast<std::size_t>(t * MimicBody::kPoints + MimicBody::kPoints - 1)];
}

void checkLinks(const MimicBody& body) {
    for (int t = 0; t < MimicBody::kTentacles; ++t) {
        for (int k = 0; k + 1 < MimicBody::kPoints; ++k) {
            const glm::vec3 a = body.points()[static_cast<std::size_t>(t * MimicBody::kPoints + k)];
            const glm::vec3 b = body.points()[static_cast<std::size_t>(t * MimicBody::kPoints + k + 1)];
            CHECK(finite(a));
            CHECK(finite(b));
            CHECK(glm::distance(a, b) == doctest::Approx(body.linkLength(t)).epsilon(0.02));
        }
    }
}

float stepAndMeasure(MimicBody& body, const MimicBody::Input& in) {
    const std::array<glm::vec3, MimicBody::kTentacles * MimicBody::kPoints> before = body.points();
    body.update(kDt, in, floorHit);
    float furthest = 0.0f;
    for (std::size_t i = 0; i < before.size(); ++i) {
        furthest = std::max(furthest, glm::distance(before[i], body.points()[i]));
    }
    return furthest;
}

MimicBody standing(MimicBody::Input& in) {
    MimicBody body;
    in.center = {0.0f, 0.25f, 0.0f};
    in.mode = MimicBody::Mode::Idle;
    for (int i = 0; i < 60; ++i) {
        body.update(kDt, in, floorHit);
    }
    return body;
}

}

TEST_CASE("Walking, a mimic's tentacles plant on the floor and step after the body in turn; links keep their length") {
    MimicBody::Input in;
    MimicBody body = standing(in);
    for (int t = 0; t < MimicBody::kTentacles; ++t) {
        CHECK(body.planted(t) == walker(t));
        if (walker(t)) {
            CHECK(std::abs(body.foot(t).y) < 0.01f);
        } else {
            CHECK(tipOf(body, t).y > 0.3f);
        }
    }
    checkLinks(body);

    in.mode = MimicBody::Mode::Walk;
    in.velocity = {3.0f, 0.0f, 0.0f};
    const int stepsBefore = body.steps();
    for (int i = 0; i < 60; ++i) {
        in.center += in.velocity * kDt;
        body.update(kDt, in, floorHit);
        for (int t = 0; t < MimicBody::kTentacles; ++t) {
            CHECK(glm::distance(tipOf(body, t), in.center) < 1.9f);
        }
    }
    CHECK(body.steps() - stepsBefore > 12);
    CHECK(glm::distance(body.center(), in.center) < 0.26f);
    checkLinks(body);
}

TEST_CASE("A mimic's body moves fluidly: nothing snaps when it sets off, stops, takes to the air or lands") {
    MimicBody::Input in;
    MimicBody body = standing(in);
    auto run = [&](int frames, float limit) {
        for (int i = 0; i < frames; ++i) {
            in.center += in.velocity * kDt;
            CHECK(stepAndMeasure(body, in) < limit);
        }
    };
    in.mode = MimicBody::Mode::Walk;
    in.velocity = {7.0f, 0.0f, 0.0f};
    run(40, 0.65f);
    in.mode = MimicBody::Mode::Idle;
    in.velocity = glm::vec3(0.0f);
    run(40, 0.45f);
    in.mode = MimicBody::Mode::Air;
    in.velocity = {4.0f, 3.0f, 0.0f};
    run(10, 0.45f);
    in.velocity = {4.0f, -3.0f, 0.0f};
    run(10, 0.45f);
    in.center.y = 0.25f;
    in.mode = MimicBody::Mode::Idle;
    in.velocity = glm::vec3(0.0f);
    run(40, 0.45f);
    checkLinks(body);
}

TEST_CASE("A mimic's body is nothing while disguised, unfurls a tentacle at a time, folds away again, and lashes at its target") {
    MimicBody body;
    MimicBody::Input in;
    in.center = {0.0f, 0.25f, 0.0f};
    in.mode = MimicBody::Mode::Hidden;
    body.update(kDt, in, floorHit);
    CHECK(body.scale() == doctest::Approx(0.0f));
    in.mode = MimicBody::Mode::Reveal;
    bool staggered = false;
    for (int i = 0; i <= 48; ++i) {
        in.progress = static_cast<float>(i) / 48.0f;
        body.update(kDt, in, floorHit);
        float least = 1.0f;
        float most = 0.0f;
        for (int t = 0; t < MimicBody::kTentacles; ++t) {
            least = std::min(least, body.out(t));
            most = std::max(most, body.out(t));
        }
        staggered = staggered || (most > 0.9f && least < 0.3f);
    }
    CHECK(staggered);
    CHECK(body.scale() == doctest::Approx(1.0f));
    for (int t = 0; t < MimicBody::kTentacles; ++t) {
        CHECK(body.out(t) == doctest::Approx(1.0f));
    }
    checkLinks(body);

    in.mode = MimicBody::Mode::Lash;
    in.target = {1.0f, 0.9f, 0.0f};
    in.progress = 0.5f;
    for (int i = 0; i < 12; ++i) {
        body.update(kDt, in, floorHit);
    }
    in.progress = 1.2f;
    float nearest = 1e9f;
    for (int i = 0; i < 10; ++i) {
        body.update(kDt, in, floorHit);
        for (int t = 0; t < MimicBody::kTentacles; ++t) {
            nearest = std::min(nearest, glm::distance(tipOf(body, t), in.target));
        }
    }
    CHECK(nearest < 0.35f);
    checkLinks(body);

    in.mode = MimicBody::Mode::Reveal;
    in.violence = 0.15f;
    for (int i = 36; i >= 0; --i) {
        in.progress = static_cast<float>(i) / 36.0f;
        CHECK(stepAndMeasure(body, in) < 0.6f);
    }
    CHECK(body.scale() == doctest::Approx(0.0f));
}

TEST_CASE("Tearing out of its disguise a mimic spins and thrashes in all directions, out to about a metre") {
    MimicBody body;
    MimicBody::Input in;
    in.center = {0.0f, 0.25f, 0.0f};
    in.mode = MimicBody::Mode::Reveal;
    float furthest = 0.0f;
    float highest = 0.0f;
    float swept = 0.0f;
    float lastAngle = 0.0f;
    for (int i = 0; i <= 48; ++i) {
        in.progress = static_cast<float>(i) / 48.0f;
        body.update(kDt, in, floorHit);
        for (int t = 0; t < MimicBody::kTentacles; ++t) {
            const glm::vec3 d = tipOf(body, t) - body.center();
            CHECK(finite(d));
            furthest = std::max(furthest, glm::length(glm::vec2(d.x, d.z)));
            highest = std::max(highest, d.y);
        }
        const glm::vec3 d = tipOf(body, 1) - body.center();
        const float angle = std::atan2(d.z, d.x);
        if (i > 20) {
            float turn = angle - lastAngle;
            turn -= 6.2831853f * std::round(turn / 6.2831853f);
            swept += std::abs(turn);
        }
        lastAngle = angle;
    }
    CHECK(furthest > 0.8f);
    CHECK(furthest < 1.3f);
    CHECK(highest > 0.5f);
    CHECK(swept > 3.1416f);
    checkLinks(body);
}
