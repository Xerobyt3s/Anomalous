#include "game/player/body_rig.h"

#include <glm/gtc/constants.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <tuple>
#include <vector>

using namespace ghost::game;

namespace {
constexpr float kDt = 1.0f / 60.0f;

bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

bool finite(const BodyPose& p) {
    bool ok = finite(p.body) && finite(p.head) && finite(p.bodyRadii) && std::isfinite(p.stir);
    for (const Arm& arm : p.arms) {
        for (const glm::vec3& point : arm) {
            ok = ok && finite(point);
        }
    }
    for (const Limb& limb : p.legs) {
        ok = ok && finite(limb.root) && finite(limb.joint) && finite(limb.end);
    }
    for (int c = 0; c < 3; ++c) {
        ok = ok && finite(p.bodyBasis[c]);
    }
    return ok;
}

BodyInput standingAt(const glm::vec3& feet) {
    BodyInput in;
    in.feet = feet;
    in.gunGrip = feet + glm::vec3(0.15f, 1.15f, -0.4f);
    in.gunFront = in.gunGrip + glm::vec3(0.0f, 0.05f, -0.25f);
    in.cylinder = in.gunGrip + glm::vec3(0.0f, 0.05f, -0.08f);
    return in;
}

void run(BodyRig& rig, BodyInput& in, float seconds, bool move = true) {
    const int frames = std::max(1, static_cast<int>(seconds / kDt));
    for (int i = 0; i < frames; ++i) {
        if (move) {
            const glm::vec3 step = in.velocity * kDt;
            in.feet += step;
            in.gunGrip += step;
            in.gunFront += step;
            in.cylinder += step;
        }
        CHECK(finite(rig.update(kDt, in)));
    }
}

float armLength(const Arm& arm) {
    float length = 0.0f;
    for (std::size_t i = 0; i + 1 < arm.size(); ++i) {
        length += glm::distance(arm[i], arm[i + 1]);
    }
    return length;
}

bool upright(const BodyPose& p) { return p.bodyBasis[1].y > 0.9f; }

}

TEST_CASE("A two-bone limb reaches a target in reach exactly, keeps its lengths and bends toward the pole") {
    const glm::vec3 root{0.0f, 1.3f, 0.0f};
    const glm::vec3 target{0.3f, 0.9f, -0.2f};
    const Limb arm = solveTwoBone(root, target, 0.3f, 0.29f, glm::vec3(1.0f, -1.0f, 0.0f));
    CHECK(glm::distance(arm.end, target) == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(glm::distance(arm.root, arm.joint) == doctest::Approx(0.3f).epsilon(1e-3));
    CHECK(glm::distance(arm.joint, arm.end) == doctest::Approx(0.29f).epsilon(1e-3));
    const glm::vec3 mid = (root + target) * 0.5f;
    CHECK(glm::dot(arm.joint - mid, glm::vec3(1.0f, -1.0f, 0.0f)) > 0.0f);

    const glm::vec3 far{2.0f, 1.3f, 0.0f};
    const Limb straight = solveTwoBone(root, far, 0.3f, 0.29f, glm::vec3(0.0f, -1.0f, 0.0f));
    CHECK(glm::distance(straight.root, straight.end) == doctest::Approx(0.59f).epsilon(1e-3));
    CHECK(glm::normalize(straight.end - root).x == doctest::Approx(1.0f).epsilon(1e-3));

    const Limb folded = solveTwoBone(root, root, 0.3f, 0.29f, glm::vec3(0.0f, -1.0f, 0.0f));
    CHECK(finite(folded.joint));
    CHECK(finite(folded.end));
}

TEST_CASE("Standing still the feet stay planted; walking they step in turn, further when running") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    run(rig, in, 1.0f);
    const glm::vec3 left = rig.pose().legs[0].end;
    in.feet.x = 0.05f;
    run(rig, in, 0.5f);
    CHECK(glm::distance(rig.pose().legs[0].end, left) < 0.01f);

    auto walk = [](float speed) {
        BodyRig walker;
        BodyInput w = standingAt({0.0f, 0.0f, 0.0f});
        w.velocity = {0.0f, 0.0f, -speed};
        int alternations = 0;
        int lastFoot = walker.lastStepFoot();
        int steps = 0;
        float longest = 0.0f;
        for (int i = 0; i < 180; ++i) {
            w.feet += w.velocity * kDt;
            CHECK(finite(walker.update(kDt, w)));
            if (walker.stepsTaken() != steps) {
                steps = walker.stepsTaken();
                alternations += walker.lastStepFoot() != lastFoot ? 1 : 0;
                lastFoot = walker.lastStepFoot();
                longest = std::max(longest, walker.lastStepLength());
            }
        }
        return std::make_tuple(steps, alternations, longest);
    };
    const auto [walkSteps, walkAlternations, walkStride] = walk(3.2f);
    CHECK(walkSteps >= 6);
    CHECK(walkAlternations == walkSteps);
    const auto [runSteps, runAlternations, runStride] = walk(5.8f);
    CHECK(runStride > walkStride);
    CHECK(runSteps >= walkSteps);
    CHECK(runAlternations == runSteps);
}

TEST_CASE("The gun arm is jointed: it keeps its length and its hand on the grip wherever it is aimed") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    run(rig, in, 1.0f);
    const float length = rig.shape().upperArm + rig.shape().forearm;
    for (const glm::vec3 grip : {glm::vec3(0.15f, 1.15f, -0.4f), glm::vec3(0.0f, 1.2f, -0.38f), glm::vec3(0.3f, 0.95f, -0.3f)}) {
        in.gunGrip = grip;
        run(rig, in, 0.2f);
        const Arm& arm = rig.pose().arms[1];
        CHECK(glm::distance(arm.back(), grip) < 0.01f);
        CHECK(armLength(arm) == doctest::Approx(length).epsilon(0.03));
        CHECK_FALSE(rig.pose().gunFree);
    }
}

TEST_CASE("Walking, the body bobs with the steps and the free arm swings against the legs") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    in.velocity = {0.0f, 0.0f, -3.2f};
    run(rig, in, 1.0f);
    float low = 1e9f;
    float high = -1e9f;
    int opposite = 0;
    int samples = 0;
    for (int i = 0; i < 90; ++i) {
        run(rig, in, kDt);
        const BodyPose& p = rig.pose();
        const float height = p.body.y - in.feet.y;
        low = std::min(low, height);
        high = std::max(high, height);

        const float leftFootAhead = p.legs[1].end.z - p.legs[0].end.z;
        const float leftHandAhead = p.arms[0].front().z - p.arms[0].back().z;
        if (std::abs(leftFootAhead) > 0.1f) {
            ++samples;
            opposite += (leftFootAhead > 0.0f) != (leftHandAhead > 0.0f) ? 1 : 0;
        }
    }
    CHECK(high - low > 0.015f);
    REQUIRE(samples > 10);
    CHECK(opposite > samples * 6 / 10);
}

TEST_CASE("Turning on the spot: a small turn moves only the upper body, a big one makes the feet step round") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    run(rig, in, 1.0f);
    const int before = rig.stepsTaken();
    in.yaw = 0.35f;
    run(rig, in, 1.0f);
    CHECK(rig.stepsTaken() == before);

    in.yaw = 1.57f;
    run(rig, in, 1.5f);
    CHECK(rig.stepsTaken() >= before + 2);

    CHECK(rig.pose().footForward[0].x > 0.9f);
}

TEST_CASE("Thrown into the air it goes floppy and tumbles; it lands hard and gets up, or lands soft and stays up") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    run(rig, in, 1.0f);

    in.grounded = false;
    in.velocity = {6.0f, 9.0f, 0.0f};
    run(rig, in, kDt);
    REQUIRE(rig.mode() == BodyMode::Flung);
    const glm::vec3 upBefore = rig.pose().bodyBasis[1];
    run(rig, in, 0.4f, false);
    CHECK(glm::dot(rig.pose().bodyBasis[1], upBefore) < 0.9f);
    CHECK(rig.pose().gunFree);

    const float length = rig.shape().upperArm + rig.shape().forearm;
    CHECK(armLength(rig.pose().arms[0]) == doctest::Approx(length).epsilon(0.08));
    CHECK(glm::distance(rig.pose().legs[0].root, rig.pose().legs[0].joint) == doctest::Approx(0.23f).epsilon(0.1));

    in.grounded = true;
    in.velocity = glm::vec3(0.0f);
    run(rig, in, 0.2f, false);
    CHECK(rig.mode() == BodyMode::GettingUp);
    run(rig, in, 1.5f, false);
    CHECK(rig.mode() == BodyMode::Grounded);
    run(rig, in, 0.5f, false);
    CHECK(upright(rig.pose()));
    CHECK_FALSE(rig.pose().gunFree);

    BodyRig soft;
    BodyInput s = standingAt({0.0f, 0.0f, 0.0f});
    run(soft, s, 1.0f);
    s.grounded = false;
    s.velocity = {4.0f, 2.5f, 0.0f};
    run(soft, s, kDt, false);
    REQUIRE(soft.mode() == BodyMode::Flung);
    s.velocity = {0.0f, -1.0f, 0.0f};
    run(soft, s, 0.05f, false);
    s.grounded = true;
    s.velocity = glm::vec3(0.0f);
    run(soft, s, kDt, false);
    CHECK(soft.mode() == BodyMode::Grounded);
    run(soft, s, 0.6f, false);
    CHECK(upright(soft.pose()));
}

TEST_CASE("Going down topples over in about half a second; revived, it gets up") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    run(rig, in, 1.0f);
    in.downed = true;
    run(rig, in, 0.2f);
    CHECK(rig.mode() == BodyMode::Toppling);
    CHECK(rig.pose().body.y > 0.4f);
    run(rig, in, 0.5f);
    CHECK(rig.mode() == BodyMode::Down);
    CHECK(rig.pose().body.y < 0.3f);
    CHECK(rig.pose().head.y < 0.45f);
    CHECK(rig.pose().gunFree);

    in.downed = false;
    run(rig, in, 0.3f);
    CHECK(rig.mode() == BodyMode::GettingUp);
    run(rig, in, 1.0f);
    CHECK(rig.mode() == BodyMode::Grounded);
    CHECK(upright(rig.pose()));
    CHECK(rig.pose().head.y > 1.1f);
}

TEST_CASE("Crouching and sliding bring the hips down") {
    BodyRig standing;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    run(standing, in, 1.0f);
    const float hips = standing.pose().legs[0].root.y;
    const float head = standing.pose().head.y;
    CHECK(head > 1.1f);
    CHECK(head < 1.5f);

    BodyRig crouched;
    in.stance = Stance::Crouch;
    run(crouched, in, 1.0f);
    CHECK(crouched.pose().legs[0].root.y < hips - 0.12f);
    CHECK(crouched.pose().head.y < head - 0.2f);

    BodyRig sliding;
    in.stance = Stance::Slide;
    in.velocity = {0.0f, 0.0f, -6.0f};
    run(sliding, in, 1.0f);
    CHECK(sliding.pose().legs[0].root.y < hips - 0.15f);
    CHECK(sliding.pose().legs[1].end.z < in.feet.z - 0.2f);
}

TEST_CASE("A hit knocks the body back and it recovers; a headshot snaps the head harder") {
    auto kick = [](bool head) {
        BodyRig rig;
        BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
        run(rig, in, 1.0f);
        const glm::vec3 rest = rig.pose().head;
        rig.jolt({1.0f, 0.0f, 0.0f}, 1.0f, head);
        float most = 0.0f;
        for (int i = 0; i < 12; ++i) {
            rig.update(kDt, in);
            most = std::max(most, rig.pose().head.x - rest.x);
        }
        run(rig, in, 1.5f);
        CHECK(glm::distance(rig.pose().head, rest) < 0.02f);
        return most;
    };
    const float body = kick(false);
    const float head = kick(true);
    CHECK(body > 0.02f);
    CHECK(head > body * 1.3f);
}

TEST_CASE("Flung limbs keep to what joints can do: bones keep their length, knees and elbows fold one way and not all the way") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    run(rig, in, 1.0f);
    in.grounded = false;
    in.velocity = {7.0f, 10.0f, -3.0f};
    run(rig, in, kDt);
    REQUIRE(rig.mode() == BodyMode::Flung);
    const BodyShape& s = rig.shape();
    for (int frame = 0; frame < 90; ++frame) {
        run(rig, in, kDt, false);
        const BodyPose& p = rig.pose();
        const glm::vec3 right = p.bodyBasis[0];
        const glm::vec3 up = p.bodyBasis[1];
        const glm::vec3 forward = p.bodyBasis[2];
        for (const Limb& leg : p.legs) {
            CHECK(glm::distance(leg.root, leg.joint) == doctest::Approx(s.thigh).epsilon(0.05));
            CHECK(glm::distance(leg.joint, leg.end) == doctest::Approx(s.shin).epsilon(0.08));
            const glm::vec3 thigh = glm::normalize(leg.joint - leg.root);
            const glm::vec3 shin = glm::normalize(leg.end - leg.joint);

            CHECK(glm::dot(thigh, glm::normalize(-up + forward * 0.35f)) > std::cos(1.3f) - 0.05f);

            CHECK(glm::dot(glm::cross(thigh, shin), right) < 0.08f);
        }
        for (int a = 0; a < 2; ++a) {
            const Arm& arm = p.arms[static_cast<std::size_t>(a)];

            float length = 0.0f;
            for (std::size_t i = 0; i + 1 < arm.size(); ++i) {
                length += glm::distance(arm[i], arm[i + 1]);
            }
            CHECK(length == doctest::Approx(s.upperArm + s.forearm).epsilon(0.08));

            CHECK(glm::distance(arm.front(), arm.back()) > (s.upperArm + s.forearm) * 0.12f);
        }
    }
}

TEST_CASE("Down, the eyes are shut; back up, they open") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    run(rig, in, 1.0f);
    in.downed = true;
    run(rig, in, 1.5f);
    for (int i = 0; i < 60; ++i) {
        run(rig, in, kDt);
        CHECK(rig.pose().blink == 1.0f);
    }
    in.downed = false;
    run(rig, in, 2.0f);
    int open = 0;
    for (int i = 0; i < 60; ++i) {
        run(rig, in, kDt);
        open += rig.pose().blink < 0.5f ? 1 : 0;
    }
    CHECK(open > 50);
}

TEST_CASE("Thrown, the loose limbs move smoothly: no snapping from one frame to the next") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    run(rig, in, 1.0f);
    in.grounded = false;
    in.velocity = {7.0f, 10.0f, -3.0f};
    run(rig, in, kDt);
    REQUIRE(rig.mode() == BodyMode::Flung);

    auto local = [&](int limb) {
        const BodyPose& p = rig.pose();
        const glm::vec3 offset = limb < 2 ? p.arms[static_cast<std::size_t>(limb)].back() - p.arms[static_cast<std::size_t>(limb)].front()
                                          : p.legs[static_cast<std::size_t>(limb - 2)].end - p.legs[static_cast<std::size_t>(limb - 2)].root;
        return glm::transpose(p.bodyBasis) * offset;
    };
    std::array<glm::vec3, 4> last{};
    std::array<glm::vec3, 4> lastStep{};
    for (int l = 0; l < 4; ++l) {
        last[static_cast<std::size_t>(l)] = local(l);
    }
    float worstStep = 0.0f;
    float worstJerk = 0.0f;
    for (int frame = 0; frame < 110; ++frame) {
        in.velocity.y -= 9.81f * kDt;
        run(rig, in, kDt);
        for (int l = 0; l < 4; ++l) {
            const glm::vec3 now = local(l);
            const glm::vec3 step = now - last[static_cast<std::size_t>(l)];
            worstStep = std::max(worstStep, glm::length(step));
            if (frame > 0) {
                worstJerk = std::max(worstJerk, glm::length(step - lastStep[static_cast<std::size_t>(l)]));
            }
            last[static_cast<std::size_t>(l)] = now;
            lastStep[static_cast<std::size_t>(l)] = step;
        }
    }

    CHECK(worstStep < 0.25f);
    CHECK(worstJerk < 0.12f);
}

TEST_CASE("Down, it lies splayed out like a star: legs apart, arms out to the sides") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    run(rig, in, 1.0f);
    in.downed = true;
    run(rig, in, 2.0f);
    REQUIRE(rig.mode() == BodyMode::Down);
    const BodyPose& p = rig.pose();
    CHECK(glm::distance(p.legs[0].end, p.legs[1].end) > 0.45f);
    for (int a = 0; a < 2; ++a) {
        const glm::vec3 hand = p.arms[static_cast<std::size_t>(a)].back();
        const float sideways = glm::dot(hand - p.body, p.bodyBasis[0]) * (a == 0 ? -1.0f : 1.0f);
        CHECK(sideways > 0.45f);
        CHECK(hand.y < 0.3f);
    }
}

TEST_CASE("Reaching near full stretch, the elbow moves smoothly with the hand rather than popping straight") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    run(rig, in, 1.0f);

    const glm::vec3 shoulder = rig.pose().arms[1].front();
    const glm::vec3 out = glm::normalize(glm::vec3(0.2f, 0.1f, -1.0f));
    const float reach = rig.shape().upperArm + rig.shape().forearm;
    glm::vec3 lastElbow{0.0f};
    float worst = 0.0f;
    for (int i = 0; i < 200; ++i) {
        in.gunGrip = shoulder + out * (reach * 0.75f + 0.001f * static_cast<float>(i) * reach * 0.003f * 333.0f / 10.0f);
        rig.update(kDt, in);
        const glm::vec3 elbow = rig.pose().arms[1][2];
        if (i > 0) {
            worst = std::max(worst, glm::distance(elbow, lastElbow));
        }
        lastElbow = elbow;
    }
    CHECK(worst < 0.02f);
}

TEST_CASE("Lying dead, the arms and legs lie still: no twitching") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    run(rig, in, 1.0f);
    in.downed = true;
    run(rig, in, 2.0f);
    REQUIRE(rig.mode() == BodyMode::Down);
    std::array<glm::vec3, 4> last{rig.pose().arms[0].back(), rig.pose().arms[1].back(), rig.pose().legs[0].end, rig.pose().legs[1].end};
    float worst = 0.0f;
    for (int i = 0; i < 120; ++i) {
        run(rig, in, kDt);
        const std::array<glm::vec3, 4> now{rig.pose().arms[0].back(), rig.pose().arms[1].back(), rig.pose().legs[0].end, rig.pose().legs[1].end};
        for (std::size_t k = 0; k < 4; ++k) {
            worst = std::max(worst, glm::distance(now[k], last[k]));
        }
        last = now;
    }
    CHECK(worst < 0.004f);
}

TEST_CASE("Walking, running and strafing, the feet keep up under the hips instead of trailing behind") {
    for (const glm::vec3 velocity : {glm::vec3(0.0f, 0.0f, -3.2f), glm::vec3(0.0f, 0.0f, -5.8f), glm::vec3(3.2f, 0.0f, 0.0f), glm::vec3(-3.2f, 0.0f, 0.0f)}) {
        BodyRig rig;
        BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
        in.velocity = velocity;
        run(rig, in, 1.0f);
        const glm::vec3 along = glm::normalize(velocity);
        float sum = 0.0f;
        float worst = -1e9f;
        int n = 0;
        for (int i = 0; i < 120; ++i) {
            run(rig, in, kDt);
            for (const Limb& leg : rig.pose().legs) {
                const float ahead = glm::dot(leg.end - leg.root, along);
                sum += ahead;
                worst = std::max(worst, -ahead);
                ++n;
            }
        }
        INFO("velocity " << velocity.x << ", " << velocity.z);
        CHECK(std::abs(sum / static_cast<float>(n)) < 0.1f);
        CHECK(worst < 0.32f);
    }
}

TEST_CASE("Sprinting, the gun is carried low at the side and the arms pump; stopping, the hand goes back to the grip") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    run(rig, in, 0.5f);
    CHECK(rig.pose().carry == doctest::Approx(0.0f));
    in.velocity = {0.0f, 0.0f, -5.8f};
    in.sprinting = true;
    run(rig, in, 1.0f);
    CHECK(rig.pose().carry > 0.99f);
    CHECK(rig.pose().carryAim.y < -0.5f);
    CHECK(rig.pose().carryAim.z < 0.0f);
    int opposite = 0;
    int ownLeg = 0;
    int samples = 0;
    for (int i = 0; i < 120; ++i) {
        run(rig, in, kDt);
        const BodyPose& p = rig.pose();
        CHECK(p.arms[1].back().y < p.arms[1].front().y - 0.2f);
        CHECK(glm::distance(p.arms[1].back(), in.gunGrip) > 0.2f);
        const float gunHandAhead = p.arms[1].front().z - p.arms[1].back().z;
        const float freeHandAhead = p.arms[0].front().z - p.arms[0].back().z;
        const float rightFootAhead = p.legs[0].end.z - p.legs[1].end.z;
        if (std::abs(rightFootAhead) > 0.1f) {
            ++samples;

            opposite += (gunHandAhead > freeHandAhead) != (rightFootAhead > 0.0f) ? 1 : 0;
            ownLeg += (freeHandAhead > gunHandAhead) == (rightFootAhead > 0.0f) ? 1 : 0;
        }
    }
    REQUIRE(samples > 20);
    CHECK(opposite > samples * 8 / 10);
    CHECK(ownLeg > samples * 8 / 10);

    in.sprinting = false;
    in.velocity = {0.0f, 0.0f, -3.2f};
    run(rig, in, 1.0f);
    CHECK(rig.pose().carry < 0.01f);
    CHECK(glm::distance(rig.pose().arms[1].back(), in.gunGrip) < 0.02f);
}

TEST_CASE("Sprinting, the legs run under the hips: knees lift, nothing trails, the body leans in and barely bobs") {
    BodyRig walker;
    BodyInput w = standingAt({0.0f, 0.0f, 0.0f});
    w.velocity = {0.0f, 0.0f, -3.2f};
    run(walker, w, 1.5f);

    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    in.velocity = {0.0f, 0.0f, -5.8f};
    in.sprinting = true;
    run(rig, in, 1.0f);
    CHECK(-rig.pose().bodyBasis[1].z > -walker.pose().bodyBasis[1].z + 0.1f);
    const BodyShape& s = rig.shape();
    float low = 1e9f;
    float high = -1e9f;
    std::array<float, 2> lifted{0.0f, 0.0f};
    const int steps = rig.stepsTaken();
    for (int i = 0; i < 120; ++i) {
        run(rig, in, kDt);
        const BodyPose& p = rig.pose();
        low = std::min(low, p.body.y);
        high = std::max(high, p.body.y);
        for (std::size_t leg = 0; leg < 2; ++leg) {
            const Limb& l = p.legs[leg];
            CHECK(std::abs(l.end.z - in.feet.z) < 0.3f);
            CHECK(glm::distance(l.root, l.joint) == doctest::Approx(s.thigh).epsilon(0.02));
            CHECK(glm::distance(l.joint, l.end) == doctest::Approx(s.shin).epsilon(0.02));
            lifted[leg] = std::max(lifted[leg], l.end.y - in.feet.y);
        }
    }
    CHECK(lifted[0] > 0.1f);
    CHECK(lifted[1] > 0.1f);
    CHECK(high - low < 0.03f);
    CHECK(rig.stepsTaken() - steps >= 10);
}

TEST_CASE("Between a walk and a sprint the hands and feet move smoothly, and standing still with sprint held is harmless") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    in.velocity = {0.0f, 0.0f, -3.2f};
    run(rig, in, 1.0f);
    BodyPose last = rig.pose();
    glm::vec3 lastFeet = in.feet;
    float worstHand = 0.0f;
    float worstFoot = 0.0f;
    for (int i = 0; i < 360; ++i) {
        in.sprinting = (i / 90) % 2 == 0;
        in.velocity.z = in.sprinting ? -5.8f : -3.2f;
        run(rig, in, kDt);
        const BodyPose& p = rig.pose();
        const glm::vec3 moved = in.feet - lastFeet;
        for (std::size_t side = 0; side < 2; ++side) {
            worstHand = std::max(worstHand, glm::distance(p.arms[side].back() - moved, last.arms[side].back()));
            worstFoot = std::max(worstFoot, glm::distance(p.legs[side].end - moved, last.legs[side].end));
        }
        last = p;
        lastFeet = in.feet;
    }
    CHECK(worstHand < 0.09f);
    CHECK(worstFoot < 0.25f);

    BodyRig still;
    BodyInput s = standingAt({0.0f, 0.0f, 0.0f});
    s.sprinting = true;
    run(still, s, 0.5f);
    CHECK(still.pose().carry < 0.01f);
}

TEST_CASE("Crawling it lies flat and low, head up, and the knees and the free hand take turns as it moves") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    run(rig, in, 0.5f);
    in.stance = Stance::Crawl;
    in.gunGrip = in.feet + glm::vec3(0.15f, 0.3f, -0.6f);
    in.gunFront = in.gunGrip + glm::vec3(0.0f, 0.0f, -0.25f);
    in.cylinder = in.gunGrip + glm::vec3(0.0f, 0.05f, -0.08f);
    run(rig, in, 1.0f);
    const BodyPose& still = rig.pose();
    CHECK(std::abs(still.bodyBasis[1].y) < 0.35f);
    CHECK(still.bodyBasis[1].z < -0.9f);
    CHECK(still.body.y < 0.3f);
    CHECK(still.head.z < still.body.z);
    CHECK(still.head.y > in.feet.y + 0.2f);
    for (const Limb& leg : still.legs) {
        CHECK(leg.end.z > still.body.z);
        CHECK(leg.end.y < 0.2f);
    }

    in.velocity = {0.0f, 0.0f, -1.0f};
    const BodyShape& s = rig.shape();
    std::array<float, 2> nearest{1e9f, 1e9f};
    std::array<float, 2> farthest{-1e9f, -1e9f};
    for (int i = 0; i < 120; ++i) {
        run(rig, in, kDt);
        const BodyPose& p = rig.pose();
        for (std::size_t leg = 0; leg < 2; ++leg) {
            const float back = p.legs[leg].end.z - p.legs[leg].root.z;
            nearest[leg] = std::min(nearest[leg], back);
            farthest[leg] = std::max(farthest[leg], back);
            CHECK(glm::distance(p.legs[leg].root, p.legs[leg].joint) == doctest::Approx(s.thigh).epsilon(0.02));
        }
        CHECK(armLength(p.arms[0]) == doctest::Approx(s.upperArm + s.forearm).epsilon(0.05));
    }
    CHECK(farthest[0] - nearest[0] > 0.12f);
    CHECK(farthest[1] - nearest[1] > 0.12f);
}

TEST_CASE("Diving it flies flat along its way with the arms out in front, and getting up from a crawl is smooth") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    run(rig, in, 0.5f);
    in.stance = Stance::Dive;
    in.grounded = false;
    in.velocity = {0.0f, 0.5f, -6.5f};
    run(rig, in, 0.4f, false);
    const BodyPose& p = rig.pose();
    CHECK(p.mode == BodyMode::Grounded);
    CHECK(p.bodyBasis[1].z < -0.85f);
    CHECK(p.arms[0].back().z < p.body.z - 0.2f);

    in.grounded = true;
    in.stance = Stance::Crawl;
    in.velocity = glm::vec3(0.0f);
    run(rig, in, 1.0f, false);
    in.stance = Stance::Stand;
    BodyPose last = rig.pose();
    float worst = 0.0f;
    for (int i = 0; i < 90; ++i) {
        run(rig, in, kDt, false);
        const BodyPose& now = rig.pose();
        for (std::size_t k = 0; k < 2; ++k) {
            worst = std::max(worst, glm::distance(now.legs[k].end, last.legs[k].end));
            worst = std::max(worst, glm::distance(now.arms[k].back(), last.arms[k].back()));
        }
        last = now;
    }
    CHECK(worst < 0.12f);
    CHECK(upright(rig.pose()));
}

TEST_CASE("A jump or a dive never goes floppy; diving out of a throw takes the body back in hand") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    in.velocity = {0.0f, 0.0f, -5.8f};
    run(rig, in, 0.5f);
    in.grounded = false;
    in.velocity.y = 4.2f;
    run(rig, in, 0.2f);
    CHECK(rig.mode() == BodyMode::Grounded);
    in.stance = Stance::Dive;
    in.velocity = {0.0f, 1.5f, -6.5f};
    run(rig, in, 0.3f);
    CHECK(rig.mode() == BodyMode::Grounded);

    BodyRig thrown;
    BodyInput t = standingAt({0.0f, 0.0f, 0.0f});
    run(thrown, t, 0.5f);
    t.grounded = false;
    t.velocity = {6.0f, 9.0f, 0.0f};
    run(thrown, t, 0.2f);
    REQUIRE(thrown.mode() == BodyMode::Flung);
    t.stance = Stance::Dive;
    run(thrown, t, 0.5f);
    CHECK(thrown.mode() == BodyMode::Grounded);
    CHECK_FALSE(thrown.pose().gunFree);
}

TEST_CASE("Crawling, the head turns to look round rather than rolling") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    in.stance = Stance::Crawl;
    run(rig, in, 1.0f, false);
    for (const float yaw : {0.0f, 0.4f, -0.6f}) {
        in.yaw = yaw;
        run(rig, in, 0.3f, false);
        const BodyPose& p = rig.pose();
        CHECK(p.headUp.y > 0.9f);
        CHECK(glm::dot(p.headForward, glm::vec3(std::sin(yaw), 0.0f, -std::cos(yaw))) > 0.95f);
    }
}

TEST_CASE("Aiming lying down it stays flat, the head where it is, and both hands reach a gun held under the chin") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    in.stance = Stance::Crawl;
    in.aiming = true;
    run(rig, in, 1.0f, false);

    for (int i = 0; i < 60; ++i) {
        const BodyPose& p = rig.pose();
        in.gunGrip = p.head + glm::vec3(0.0f, -(rig.shape().headRadius + 0.06f), -rig.shape().headRadius * 0.4f);
        in.gunFront = in.gunGrip + glm::vec3(0.0f, 0.0f, -0.25f);
        in.cylinder = in.gunGrip + glm::vec3(0.0f, 0.05f, -0.08f);
        run(rig, in, kDt, false);
    }
    const BodyPose& p = rig.pose();
    CHECK(std::abs(in.gunGrip.x - p.head.x) < 0.01f);
    CHECK(in.gunGrip.y < p.head.y - rig.shape().headRadius);
    CHECK(glm::distance(p.arms[1].back(), in.gunGrip) < 0.03f);
    CHECK(glm::distance(p.arms[0].back(), in.gunGrip) < 0.12f);
    CHECK(p.head.z < p.arms[1].front().z);
    CHECK(std::abs(p.bodyBasis[1].y) < 0.25f);
}

TEST_CASE("Reloading standing, crouched and crawling, the free hand fetches from its own hips and works at the cylinder") {
    for (const Stance stance : {Stance::Stand, Stance::Crouch, Stance::Crawl}) {
        CAPTURE(static_cast<int>(stance));
        BodyRig rig;
        BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
        in.stance = stance;
        run(rig, in, 1.0f, false);

        const BodyPose& settled = rig.pose();
        const glm::vec3 mid = (settled.arms[0][0] + settled.arms[1][0]) * 0.5f;
        in.gunGrip = stance == Stance::Crawl ? settled.head + glm::vec3(-0.04f, -(rig.shape().headRadius + 0.08f), -rig.shape().headRadius * 0.3f)
                                             : mid + glm::vec3(0.05f, -0.15f, -0.32f);
        in.cylinder = in.gunGrip + glm::vec3(-0.02f, 0.05f, -0.08f);
        in.gunFront = in.gunGrip + glm::vec3(0.0f, 0.05f, -0.25f);
        in.crane = 1.0f;
        const float length = rig.shape().upperArm + rig.shape().forearm;
        for (int i = 0; i <= 60; ++i) {
            in.loading = static_cast<float>(i) / 60.0f;
            run(rig, in, kDt, false);
            const BodyPose& p = rig.pose();
            CHECK(armLength(p.arms[0]) < length * 1.26f);
            if (stance == Stance::Crawl) {
                CHECK(p.arms[0].back().y < p.head.y);
            }
        }
        in.loading = -1.0f;
        run(rig, in, 0.4f, false);
        CHECK(glm::distance(rig.pose().arms[0].back(), in.cylinder) < 0.12f);
        CHECK(glm::distance(rig.pose().arms[1].back(), in.gunGrip) < 0.03f);
    }
}

TEST_CASE("Holstered, the gun hand takes the gun to the right hip, then swings free with the walk; drawn, it is back on the grip") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    run(rig, in, 0.5f, false);
    const float length = rig.shape().upperArm + rig.shape().forearm;
    for (int i = 0; i <= 30; ++i) {
        in.holster = static_cast<float>(i) / 30.0f;
        run(rig, in, kDt, false);
        CHECK(armLength(rig.pose().arms[1]) < length * 1.26f);
    }
    run(rig, in, 0.1f, false);
    const BodyPose& away = rig.pose();
    CHECK(away.holsterGrip.x > away.body.x + 0.15f);
    CHECK(away.holsterGrip.y < away.body.y);
    CHECK(away.holsterAim.y < -0.8f);
    run(rig, in, 0.6f, false);
    CHECK(rig.pose().arms[1].back().y < rig.pose().arms[1].front().y - 0.35f);

    in.velocity = {0.0f, 0.0f, -3.2f};
    run(rig, in, 1.0f);
    std::vector<std::pair<float, float>> ahead;
    for (int i = 0; i < 120; ++i) {
        run(rig, in, kDt);
        const BodyPose& p = rig.pose();
        ahead.emplace_back(p.arms[0].front().z - p.arms[0].back().z, p.arms[1].front().z - p.arms[1].back().z);
    }
    float meanL = 0.0f;
    float meanR = 0.0f;
    for (const auto& [l, r] : ahead) {
        meanL += l / static_cast<float>(ahead.size());
        meanR += r / static_cast<float>(ahead.size());
    }
    float covariance = 0.0f;
    float spreadR = 0.0f;
    for (const auto& [l, r] : ahead) {
        covariance += (l - meanL) * (r - meanR);
        spreadR = std::max(spreadR, std::abs(r - meanR));
    }
    CHECK(covariance < 0.0f);
    CHECK(spreadR > 0.05f);

    in.velocity = glm::vec3(0.0f);
    for (int i = 30; i >= 0; --i) {
        in.holster = static_cast<float>(i) / 30.0f;
        run(rig, in, kDt, false);
    }
    run(rig, in, 0.3f, false);
    CHECK(glm::distance(rig.pose().arms[1].back(), in.gunGrip) < 0.03f);
    CHECK(rig.pose().holster == doctest::Approx(0.0f));
}

TEST_CASE("Crawling with the gun holstered, both hands crawl, in turn") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    in.stance = Stance::Crawl;
    in.holster = 1.0f;
    run(rig, in, 1.0f, false);
    in.velocity = {0.0f, 0.0f, -1.0f};
    run(rig, in, 0.5f);
    float covariance = 0.0f;
    float spread = 0.0f;
    std::vector<std::pair<float, float>> reach;
    for (int i = 0; i < 120; ++i) {
        run(rig, in, kDt);
        const BodyPose& p = rig.pose();
        reach.emplace_back(p.arms[0].front().z - p.arms[0].back().z, p.arms[1].front().z - p.arms[1].back().z);
        CHECK(p.arms[1].back().y < 0.25f);
    }
    float meanL = 0.0f;
    float meanR = 0.0f;
    for (const auto& [l, r] : reach) {
        meanL += l / static_cast<float>(reach.size());
        meanR += r / static_cast<float>(reach.size());
    }
    for (const auto& [l, r] : reach) {
        covariance += (l - meanL) * (r - meanR);
        spread = std::max(spread, std::abs(r - meanR));
    }
    CHECK(covariance < 0.0f);
    CHECK(spread > 0.05f);
}

TEST_CASE("Lying, the roll turns it over along its length: belly toward the view on a side, up on the back, smoothly") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    in.stance = Stance::Crawl;
    run(rig, in, 1.0f, false);
    BodyPose last = rig.pose();
    float worst = 0.0f;
    for (int i = 0; i <= 120; ++i) {
        in.roll = glm::two_pi<float>() * static_cast<float>(i) / 120.0f;
        in.yaw = in.roll;
        run(rig, in, kDt, false);
        const BodyPose& p = rig.pose();
        for (std::size_t k = 0; k < 2; ++k) {
            worst = std::max(worst, glm::distance(p.legs[k].end, last.legs[k].end));
            CHECK(glm::distance(p.legs[k].root, p.legs[k].joint) == doctest::Approx(rig.shape().thigh).epsilon(0.02));
            CHECK(armLength(p.arms[k]) < (rig.shape().upperArm + rig.shape().forearm) * 1.26f);
        }
        CHECK(p.bodyBasis[1].z < -0.8f);
        last = p;
    }
    CHECK(worst < 0.15f);
    in.roll = glm::half_pi<float>();
    in.yaw = in.roll;
    run(rig, in, 0.5f, false);
    CHECK(rig.pose().bodyBasis[2].x > 0.8f);
    in.roll = glm::pi<float>();
    in.yaw = in.roll;
    run(rig, in, 0.5f, false);
    CHECK(rig.pose().bodyBasis[2].y > 0.8f);
}

TEST_CASE("The head only looks so far up or down: not into its own body, not straight up") {
    for (const Stance stance : {Stance::Stand, Stance::Crawl}) {
        BodyRig rig;
        BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
        in.stance = stance;
        for (const float pitch : {-1.5f, 1.5f}) {
            in.pitch = pitch;
            run(rig, in, 0.5f, false);
            CHECK(std::abs(rig.pose().headForward.y) < 0.9f);
        }
        in.pitch = -1.5f;
        run(rig, in, 0.3f, false);
        if (stance == Stance::Crawl) {
            CHECK(rig.pose().headForward.y > -0.45f);
        }
    }
}

TEST_CASE("Lying, the head turns over with the body on a side, and is upright on the belly and the back") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    in.stance = Stance::Crawl;
    run(rig, in, 1.0f, false);
    CHECK(rig.pose().headUp.y > 0.9f);
    glm::vec3 last = rig.pose().headUp;
    float worst = 0.0f;
    for (int i = 0; i <= 120; ++i) {
        in.roll = glm::two_pi<float>() * static_cast<float>(i) / 120.0f;
        in.yaw = in.roll;
        run(rig, in, kDt, false);
        worst = std::max(worst, glm::distance(rig.pose().headUp, last));
        last = rig.pose().headUp;
    }
    CHECK(worst < 0.2f);
    in.roll = glm::half_pi<float>();
    in.yaw = in.roll;
    run(rig, in, 0.5f, false);
    const BodyPose& side = rig.pose();
    CHECK(std::abs(side.headUp.y) < 0.4f);
    CHECK(glm::dot(side.headUp, side.bodyBasis[1]) > 0.8f);
    in.roll = glm::pi<float>();
    in.yaw = in.roll;
    run(rig, in, 0.5f, false);
    CHECK(rig.pose().headUp.y > 0.5f);
}

TEST_CASE("On a side the head stays on its neck: lifted the body's way, not pushed off sideways") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    in.stance = Stance::Crawl;
    in.roll = glm::half_pi<float>();
    in.yaw = in.roll;
    run(rig, in, 1.0f, false);
    const BodyPose& p = rig.pose();
    const glm::vec3 fromNeck = glm::normalize(p.head - p.neck);
    CHECK(std::abs(glm::dot(fromNeck, p.bodyBasis[0])) < 0.3f);
    CHECK(glm::dot(fromNeck, p.bodyBasis[1]) > 0.5f);
}

TEST_CASE("On the belly or back the head lifts off the ground; on a side it stays in line") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    in.stance = Stance::Crawl;
    in.roll = glm::pi<float>();
    in.yaw = in.roll;
    run(rig, in, 1.0f, false);
    const BodyPose& back = rig.pose();
    CHECK(glm::dot(glm::normalize(back.head - back.neck), glm::vec3(0.0f, 1.0f, 0.0f)) > 0.4f);
}

TEST_CASE("On the belly the head lifts to look ahead") {
    BodyRig rig;
    BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
    in.stance = Stance::Crawl;
    run(rig, in, 1.0f, false);
    const BodyPose& p = rig.pose();
    CHECK(glm::dot(glm::normalize(p.head - p.neck), glm::vec3(0.0f, 1.0f, 0.0f)) > 0.4f);
}

TEST_CASE("Taking something, a free hand reaches for it: the off hand with the gun drawn, the gun hand once holstered") {
    const glm::vec3 material{0.3f, 0.3f, -0.9f};
    auto handGap = [&](float holster, int arm) {
        BodyRig rig;
        BodyInput in = standingAt({0.0f, 0.0f, 0.0f});
        in.holster = holster;
        run(rig, in, 1.0f, false);
        const float before = glm::distance(rig.pose().arms[static_cast<std::size_t>(arm)].back(), material);
        in.reach = true;
        in.reachTo = material;
        run(rig, in, 1.0f, false);
        const float reaching = glm::distance(rig.pose().arms[static_cast<std::size_t>(arm)].back(), material);
        in.reach = false;
        run(rig, in, 1.0f, false);
        const float after = glm::distance(rig.pose().arms[static_cast<std::size_t>(arm)].back(), material);
        return std::array<float, 3>{before, reaching, after};
    };
    const auto offDrawn = handGap(0.0f, 0);
    CHECK(offDrawn[1] < offDrawn[0] - 0.2f);
    CHECK(offDrawn[2] > offDrawn[1] + 0.2f);
    const auto gunDrawn = handGap(0.0f, 1);
    CHECK(gunDrawn[1] == doctest::Approx(gunDrawn[0]).epsilon(0.05));
    const auto gunHolstered = handGap(1.0f, 1);
    CHECK(gunHolstered[1] < gunHolstered[0] - 0.2f);
    const auto offHolstered = handGap(1.0f, 0);
    CHECK(offHolstered[1] > offHolstered[0] - 0.1f);
}
