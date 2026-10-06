#include "game/player/body_rig.h"

#include <doctest/doctest.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>

using namespace ghost::game;

TEST_CASE("A body far from the origin keeps its feet still while its up slowly turns") {
    BodyRig rig;
    BodyInput in;
    const float dt = 1.0f / 120.0f;
    const glm::vec3 anchor{200.0f, 30.0f, 230.0f};
    for (int i = 0; i < 240; ++i) {
        rig.update(dt, in);
    }
    glm::quat frame{1.0f, 0.0f, 0.0f, 0.0f};
    const auto world = [&](const glm::vec3& local) { return anchor + frame * local; };
    const glm::vec3 leftStart = world(rig.pose().legs[0].end);
    const glm::vec3 rightStart = world(rig.pose().legs[1].end);
    const glm::vec3 handStart = world(rig.pose().arms[0].back());
    const glm::quat turn = glm::angleAxis(glm::radians(0.2f), glm::normalize(glm::vec3(1.0f, 0.0f, 0.3f)));
    float worst = 0.0f;
    for (int i = 0; i < 120; ++i) {
        const glm::quat next = glm::normalize(turn * frame);
        rig.rebase(glm::mat3_cast(glm::conjugate(next) * frame), glm::vec3(0.0f));
        frame = next;
        rig.update(dt, in);
        worst = std::max(worst, glm::distance(world(rig.pose().legs[0].end), leftStart));
        worst = std::max(worst, glm::distance(world(rig.pose().legs[1].end), rightStart));
    }
    CHECK(worst < 0.02f);
    CHECK(rig.stepsTaken() == 0);
    CHECK(glm::distance(world(rig.pose().arms[0].back()), handStart) < 0.5f);
}

TEST_CASE("Rebasing moves what the rig remembers by exactly the given shift") {
    BodyRig rig;
    BodyInput in;
    for (int i = 0; i < 120; ++i) {
        rig.update(1.0f / 120.0f, in);
    }
    const BodyPose before = rig.pose();
    rig.rebase(glm::mat3(1.0f), glm::vec3(0.0f, 0.0f, 3.0f));
    CHECK(rig.pose().legs[0].end.z == doctest::Approx(before.legs[0].end.z + 3.0f));
    CHECK(rig.pose().head.z == doctest::Approx(before.head.z + 3.0f));
    CHECK(rig.pose().headForward.z == doctest::Approx(before.headForward.z));
}
