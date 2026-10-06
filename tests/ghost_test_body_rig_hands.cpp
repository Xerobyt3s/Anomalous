#include "game/player/body_rig.h"

#include <doctest/doctest.h>

#include <cmath>

using namespace ghost::game;

namespace {
constexpr float kDt = 1.0f / 60.0f;

BodyInput holstered() {
    BodyInput in;
    in.holster = 1.0f;
    in.gunGrip = glm::vec3(0.15f, 1.15f, -0.4f);
    in.gunFront = in.gunGrip + glm::vec3(0.0f, 0.05f, -0.25f);
    in.cylinder = in.gunGrip + glm::vec3(0.0f, 0.05f, -0.08f);
    return in;
}

BodyInput seatedIn(bool steering) {
    BodyInput in = holstered();
    in.seated = 1.0f;
    in.seatYaw = 0.0f;
    in.seatHips = glm::vec3(0.0f, 0.42f, 0.0f);
    in.pedals = glm::vec3(0.0f, 0.14f, -0.36f);
    in.steering = steering;
    in.wheelCenter = glm::vec3(0.0f, 0.78f, -0.38f);
    in.wheelNormal = glm::normalize(glm::vec3(0.0f, 0.4f, 0.92f));
    in.wheelUp = glm::normalize(glm::vec3(0.0f, 0.92f, -0.4f));
    in.wheelRadius = 0.18f;
    in.shifter = glm::vec3(0.36f, 0.45f, -0.22f);
    return in;
}

const BodyPose& settle(BodyRig& rig, const BodyInput& in, float seconds = 1.5f) {
    for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
        rig.update(kDt, in);
    }
    return rig.pose();
}

glm::vec3 hand(const BodyPose& p, int arm) { return p.arms[static_cast<std::size_t>(arm)].back(); }

glm::vec3 rimPoint(const BodyInput& in, float angle) {
    const glm::vec3 right = glm::normalize(glm::cross(in.wheelUp, in.wheelNormal));
    return in.wheelCenter + (in.wheelUp * std::cos(angle) + right * std::sin(angle)) * in.wheelRadius;
}

}

TEST_CASE("Seated, the hips go to the seat and the feet to the pedals") {
    BodyRig rig;
    const BodyInput in = seatedIn(false);
    const BodyPose& p = settle(rig, in);
    const glm::vec3 hips = p.body - p.bodyBasis[1] * (p.bodyRadii.y - 0.02f);
    CHECK(glm::distance(hips, in.seatHips) < 0.06f);
    for (const Limb& leg : p.legs) {
        CHECK(glm::distance(glm::vec3(leg.end.x, 0.0f, leg.end.z), glm::vec3(in.pedals.x, 0.0f, in.pedals.z)) < 0.2f);
        CHECK(std::abs(leg.end.y - in.pedals.y) < 0.08f);
        CHECK(leg.joint.y > leg.end.y);
    }
    CHECK(p.seated == doctest::Approx(1.0f));
}

TEST_CASE("Driving, both hands hold the wheel rim and follow the steer") {
    BodyRig rig;
    BodyInput in = seatedIn(true);
    const BodyPose& p = settle(rig, in);
    CHECK(glm::distance(hand(p, 0), rimPoint(in, -1.05f)) < 0.06f);
    CHECK(glm::distance(hand(p, 1), rimPoint(in, 1.05f)) < 0.06f);
    const glm::vec3 leftBefore = hand(p, 0);

    in.steer = 0.6f;
    settle(rig, in, 1.0f);
    CHECK(glm::distance(hand(rig.pose(), 0), rimPoint(in, 0.6f - 1.05f)) < 0.06f);
    CHECK(glm::distance(hand(rig.pose(), 1), rimPoint(in, 0.6f + 1.05f)) < 0.06f);
    CHECK(glm::distance(hand(rig.pose(), 0), leftBefore) > 0.05f);
}

TEST_CASE("Changing gear, the right hand goes to the shifter and comes back") {
    BodyRig rig;
    BodyInput in = seatedIn(true);
    in.shifting = 1.0f;
    settle(rig, in);
    CHECK(glm::distance(hand(rig.pose(), 1), in.shifter) < 0.06f);
    CHECK(glm::distance(hand(rig.pose(), 0), rimPoint(in, -1.05f)) < 0.06f);
    in.shifting = 0.0f;
    settle(rig, in);
    CHECK(glm::distance(hand(rig.pose(), 1), rimPoint(in, 1.05f)) < 0.06f);
}

TEST_CASE("A passenger rests their hands, and the gun hand still aims when drawn") {
    BodyRig rig;
    BodyInput in = seatedIn(false);
    const BodyPose& rest = settle(rig, in);
    const glm::vec3 restRight = hand(rest, 1);
    CHECK(hand(rest, 0).y < rest.body.y);
    CHECK(restRight.y < rest.body.y);

    in.holster = 0.0f;
    in.aiming = true;
    in.gunGrip = glm::vec3(0.12f, 1.0f, -0.45f);
    settle(rig, in);
    CHECK(glm::distance(hand(rig.pose(), 1), in.gunGrip) < 0.06f);
}

TEST_CASE("Carrying two-handed holds the item at the chest between both hands") {
    BodyRig rig;
    BodyInput in = holstered();
    in.holdHands = 2;
    in.holdHalfWidth = 0.16f;
    const BodyPose& p = settle(rig, in);
    CHECK(p.holding == doctest::Approx(1.0f).epsilon(0.02));
    CHECK(glm::distance(hand(p, 0), hand(p, 1)) == doctest::Approx(0.32f).epsilon(0.15));
    CHECK(glm::distance((hand(p, 0) + hand(p, 1)) * 0.5f, p.held) < 0.05f);
    CHECK(p.held.z < p.body.z - 0.1f);
}

TEST_CASE("Carrying one-handed holds the item at the hip below the gun hand") {
    BodyRig rig;
    BodyInput in = holstered();
    in.holdHands = 1;
    const BodyPose& p = settle(rig, in);
    CHECK(p.holding == doctest::Approx(1.0f).epsilon(0.02));
    CHECK(glm::distance(hand(p, 1), p.held + glm::vec3(0.0f, 0.1f, 0.0f)) < 0.06f);
    CHECK(p.held.x > p.body.x);
    CHECK(p.held.y < p.body.y);

    in.holdHands = 0;
    settle(rig, in);
    CHECK(rig.pose().holding < 0.02f);
}

TEST_CASE("Getting in, the body blends from standing to seated") {
    BodyRig standing;
    BodyRig half;
    BodyRig sitting;
    BodyInput in = seatedIn(true);
    in.seated = 0.0f;
    const float standingHips = settle(standing, in).body.y;
    in.seated = 0.5f;
    const float halfHips = settle(half, in).body.y;
    in.seated = 1.0f;
    const float sittingHips = settle(sitting, in).body.y;
    CHECK(standingHips > halfHips);
    CHECK(halfHips > sittingHips);
}
