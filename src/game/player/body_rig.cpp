#include "game/player/body_rig.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace ghost::game {
namespace {
constexpr glm::vec3 kUp{0.0f, 1.0f, 0.0f};
constexpr float kRopeStep = 1.0f / 120.0f;
constexpr float kToppleTime = 0.5f;
constexpr float kGetUpTime = 0.6f;
constexpr float kFlopTime = 0.5f;

float ease(float rate, float dt) { return 1.0f - std::exp(-rate * dt); }

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float length = glm::length(v);
    return length > 1e-5f ? v / length : fallback;
}

float wrapAngle(float a) { return std::remainder(a, glm::two_pi<float>()); }

float stepDuration(float speed) { return std::clamp(std::min(0.24f, 0.42f / std::max(speed, 0.1f)), 0.075f, 0.24f); }

constexpr float kStride = 1.2f;
constexpr float kCrawlStride = 0.8f;
constexpr float kStance = 0.3f;

glm::vec2 runFoot(float phase) {
    const float front = kStride * kStance * 0.47f;
    const float back = front - kStride * kStance;
    if (phase < kStance) {
        return {glm::mix(front, back, phase / kStance), 0.0f};
    }
    const float u = (phase - kStance) / (1.0f - kStance);
    return {glm::mix(back, front, u * u * (3.0f - 2.0f * u)), std::sin(u * glm::pi<float>()) * 0.17f};
}

glm::quat orientation(const glm::vec3& right, const glm::vec3& up) {
    return glm::normalize(glm::quat_cast(glm::mat3(right, up, glm::cross(right, up))));
}

Arm alongArm(const glm::vec3& shoulder, const glm::vec3& elbow, const glm::vec3& hand) {
    const float upper = glm::distance(shoulder, elbow);
    const float lower = glm::distance(elbow, hand);
    const float total = std::max(upper + lower, 1e-5f);
    Arm arm{};
    for (int k = 0; k < kArmPoints; ++k) {
        const float d = total * static_cast<float>(k) / static_cast<float>(kArmPoints - 1);
        arm[static_cast<std::size_t>(k)] = d <= upper ? glm::mix(shoulder, elbow, upper > 1e-5f ? d / upper : 0.0f)
                                                      : glm::mix(elbow, hand, lower > 1e-5f ? (d - upper) / lower : 1.0f);
    }
    return arm;
}

Limb reachFor(const glm::vec3& shoulder, const glm::vec3& target, float upper, float lower, const glm::vec3& pole) {
    const float reach = upper + lower;
    const float distance = glm::distance(shoulder, target);
    const float softStart = 0.85f * reach;
    float eased = distance;
    if (distance > softStart) {
        const float span = reach - softStart;
        eased = softStart + span * (1.0f - std::exp(-(distance - softStart) / span));
    }
    const float scale = std::clamp(distance / std::max(eased, 1e-4f), 1.0f, 1.25f);
    return solveTwoBone(shoulder, target, upper * scale, lower * scale, pole);
}

}

Limb solveTwoBone(const glm::vec3& root, const glm::vec3& target, float upper, float lower, const glm::vec3& pole) {
    Limb limb;
    limb.root = root;
    const glm::vec3 toTarget = target - root;
    const float distance = glm::length(toTarget);

    const glm::vec3 dir = distance > 1e-5f ? toTarget / distance : safeNormalize(pole, kUp);
    glm::vec3 bend = pole - dir * glm::dot(pole, dir);
    if (glm::length(bend) < 1e-5f) {
        bend = std::abs(dir.y) < 0.99f ? glm::cross(dir, kUp) : glm::vec3(1.0f, 0.0f, 0.0f);
    }
    bend = glm::normalize(bend);
    const float reach = upper + lower;
    if (distance >= reach - 1e-4f) {
        limb.joint = root + dir * upper;
        limb.end = root + dir * reach;
        return limb;
    }
    const float d = std::max(distance, std::abs(upper - lower) + 1e-4f);

    const float cosA = std::clamp((upper * upper + d * d - lower * lower) / (2.0f * upper * d), -1.0f, 1.0f);
    const float sinA = std::sqrt(std::max(0.0f, 1.0f - cosA * cosA));
    limb.joint = root + (dir * cosA + bend * sinA) * upper;
    limb.end = limb.joint + safeNormalize(root + dir * d - limb.joint, dir) * lower;
    return limb;
}

void BodyRig::setMode(BodyMode mode) {
    m_mode = mode;
    m_modeTime = 0.0f;
}

void BodyRig::jolt(const glm::vec3& direction, float strength, bool head) {
    const glm::vec3 away = safeNormalize(glm::vec3(direction.x, 0.0f, direction.z), glm::vec3(0.0f, 0.0f, 1.0f));
    strength = std::clamp(strength, 0.0f, 3.0f);
    m_lean.velocity += away * (3.0f * strength);
    m_squash.velocity.x -= 0.6f * strength;
    m_headKick.velocity += away * ((head ? 3.2f : 1.0f) * strength) + kUp * (head ? 0.8f : 0.3f);
    if (strength > 1.4f) {
        m_stagger.velocity += away * (1.4f * (strength - 1.0f));
    }

    for (Chain& arm : m_armsBefore) {
        for (std::size_t i = 1; i < arm.size(); ++i) {
            arm[i] -= away * (0.01f * strength * static_cast<float>(i));
        }
    }
    m_stir = std::max(m_stir, std::min(0.5f * strength, 1.0f));
}

void BodyRig::simulateLimb(Chain& limb, Chain& previous, float upper, float lower, const JointLimits& limits, glm::vec3& hingeMemory, float& bendMemory,
                           float dt, float ground) {
    const int steps = std::max(1, static_cast<int>(std::ceil(dt / kRopeStep)));
    const float h = dt / static_cast<float>(steps);
    const glm::vec3 root = limb[0];
    const glm::vec3 cone = safeNormalize(limits.coneAxis, -kUp);
    const glm::vec3 bodyHinge = safeNormalize(limits.hingeAxis, glm::vec3(1.0f, 0.0f, 0.0f));
    if (glm::length(hingeMemory) < 0.5f) {
        hingeMemory = bodyHinge;
    }
    for (int s = 0; s < steps; ++s) {
        for (int i = 1; i < 3; ++i) {
            const glm::vec3 velocity = (limb[i] - previous[i]) * 0.93f;
            previous[i] = limb[i];
            limb[i] += velocity + glm::vec3(0.0f, -9.81f, 0.0f) * (h * h);
        }
        previous[0] = limb[0];
        const Chain moved = limb;
        for (int iteration = 0; iteration < 6; ++iteration) {
            limb[0] = root;

            limb[1] = root + safeNormalize(limb[1] - root, cone) * upper;
            const glm::vec3 d = limb[2] - limb[1];
            const float length = glm::length(d);
            if (length > 1e-6f) {
                const glm::vec3 fix = d * ((length - lower) / length);
                limb[1] += fix * 0.3f;
                limb[2] -= fix * 0.7f;
            }

            glm::vec3 first = safeNormalize(limb[1] - root, cone);
            const float off = std::acos(std::clamp(glm::dot(first, cone), -1.0f, 1.0f));
            if (off > limits.cone) {
                const glm::vec3 axis = safeNormalize(glm::cross(cone, first) + hingeMemory * 0.25f, bodyHinge);

                first = glm::normalize(glm::mix(first, glm::angleAxis(limits.cone, axis) * cone, 0.5f));
            }
            limb[1] = root + first * upper;

            glm::vec3 axis = hingeMemory - first * glm::dot(hingeMemory, first);
            const glm::vec3 wanted = bodyHinge - first * glm::dot(bodyHinge, first);
            if (glm::length(wanted) > 0.2f) {
                axis = glm::mix(axis, glm::normalize(wanted) * glm::length(axis), 0.01f);
            }
            axis = safeNormalize(axis, safeNormalize(wanted, glm::cross(first, kUp)));
            hingeMemory = axis;
            glm::vec3 second = safeNormalize(limb[2] - limb[1], first);
            glm::vec3 inPlane = second - axis * glm::dot(second, axis);
            if (glm::length(inPlane) < 1e-5f) {
                inPlane = first;
            }
            inPlane = glm::normalize(inPlane);

            float bend = std::atan2(glm::dot(glm::cross(first, inPlane), axis), glm::dot(first, inPlane));
            if (bend < limits.minBend || bend > limits.maxBend) {
                const float toMin = std::abs(bendMemory - limits.minBend);
                const float toMax = std::abs(bendMemory - limits.maxBend);
                bend = toMin < toMax ? limits.minBend : limits.maxBend;
            }
            bendMemory = bend;
            const glm::vec3 allowed = glm::angleAxis(bend, axis) * first;
            second = glm::normalize(glm::mix(second, allowed, 0.6f));
            limb[2] = limb[1] + second * lower;

            for (int i = 1; i < 3; ++i) {
                limb[i].y = std::max(limb[i].y, ground);
            }
        }

        for (int i = 1; i < 3; ++i) {
            previous[i] += (limb[i] - moved[i]) * 0.95f;
        }
    }
}

void BodyRig::stepFeet(float dt, const BodyInput& in, const glm::vec3& right, float speed, const glm::vec3& velocity,
                       const glm::vec3& ahead, float width) {
    const float stepTime = stepDuration(speed);

    const float sideways = speed > 0.1f ? std::abs(glm::dot(velocity / speed, right)) : 0.0f;
    const float stepReach = (0.08f + std::min(speed, 7.0f) * stepTime * 0.45f) * (1.0f - 0.5f * sideways);
    for (int i = 0; i < 2; ++i) {
        Foot& foot = m_feet[static_cast<std::size_t>(i)];
        const glm::vec3 home = in.feet + right * (i == 0 ? -width : width);
        if (!in.grounded) {
            foot.step = -1.0f;
            foot.planted = home;
            continue;
        }
        if (foot.step >= 0.0f) {
            foot.step += dt / stepTime;
            foot.to = home + velocity * (std::max(1.0f - foot.step, 0.0f) * stepTime) + ahead;
            if (foot.step >= 1.0f) {
                foot.step = -1.0f;
                foot.planted = foot.to;
            }
            continue;
        }
        const Foot& other = m_feet[static_cast<std::size_t>(1 - i)];
        const glm::vec3 off = home - foot.planted;
        const float behind = glm::length(glm::vec2(off.x, off.z));
        const bool wants = behind > stepReach || (speed < 0.3f && behind > 0.08f && m_lastStep != i);
        if (wants && other.step < 0.0f && (m_lastStep != i || behind > stepReach * 1.6f)) {
            foot.step = 0.0f;
            foot.from = foot.planted;
            foot.to = home + velocity * stepTime + ahead;
            m_lastStep = i;
            ++m_steps;
            m_lastStepLength = glm::distance(foot.from, foot.to);
        }
    }
}

const BodyPose& BodyRig::update(float dt, const BodyInput& in) {
    dt = std::clamp(dt, 0.0f, 0.1f);
    m_time += dt;
    m_modeTime += dt;
    const BodyShape& s = m_shape;
    const glm::vec3 flatVelocity{in.velocity.x, 0.0f, in.velocity.z};
    const float speed = glm::length(flatVelocity);
    const bool first = !m_started;
    if (first) {
        m_started = true;
        m_hipYaw = m_torsoYaw = in.yaw;
        for (int i = 0; i < 2; ++i) {
            const glm::vec3 home = in.feet + flatRight(in.yaw) * (i == 0 ? -s.hipWidth : s.hipWidth);
            m_feet[static_cast<std::size_t>(i)] = {home, home, home, -1.0f};
        }
        m_lastVelocity = in.velocity;
    }
    const glm::vec3 change = in.velocity - m_lastVelocity;
    const float landingSpeed = glm::length(m_lastVelocity);
    m_lastVelocity = in.velocity;

    if (in.downed) {
        if (m_mode != BodyMode::Toppling && m_mode != BodyMode::Down) {
            setMode(BodyMode::Toppling);
        }
    } else if (m_mode == BodyMode::Toppling || m_mode == BodyMode::Down) {
        setMode(BodyMode::GettingUp);
        m_flop = 0.0f;
    }
    const bool canFling = m_mode == BodyMode::Grounded || m_mode == BodyMode::GettingUp;

    if (!first && canFling && !in.downed && !in.grounded && in.stance != Stance::Dive &&
        (glm::length(glm::vec2(change.x, change.z)) > 3.5f || change.y > 6.0f || glm::length(in.velocity) > 7.5f)) {
        setMode(BodyMode::Flung);
        m_ik = 0.0f;
        m_orient = orientation(m_pose.bodyBasis[0], m_pose.bodyBasis[1]);
        const glm::vec3 shove{change.x, 0.0f, change.z};
        const glm::vec3 along = glm::length(shove) > 0.3f ? glm::normalize(shove) : safeNormalize(flatVelocity, flatForward(in.yaw));

        m_spin = glm::cross(kUp, along) * (2.5f + std::min(glm::length(change), 10.0f) * 0.35f) +
                 kUp * (std::fmod(m_time * 7.3f, 1.0f) > 0.5f ? 1.5f : -1.5f);
        for (int i = 0; i < 2; ++i) {
            const Limb& leg = m_pose.legs[static_cast<std::size_t>(i)];
            m_legRope[static_cast<std::size_t>(i)] = {leg.root, leg.joint, leg.end};
            m_legRopeBefore[static_cast<std::size_t>(i)] = m_legRope[static_cast<std::size_t>(i)];
        }
    } else if (m_mode == BodyMode::Flung && in.stance == Stance::Dive) {
        setMode(BodyMode::Grounded);
        m_settleFrom = m_orient;
        m_settle = 1.0f;
    } else if (m_mode == BodyMode::Flung && in.grounded) {
        const float upright = glm::dot(m_orient * kUp, kUp);
        m_settleFrom = m_orient;
        if (landingSpeed > 5.0f || upright < 0.5f) {
            setMode(BodyMode::GettingUp);
            m_flop = kFlopTime;
        } else {
            setMode(BodyMode::Grounded);
            m_settle = 1.0f;
        }
    }
    if (m_mode == BodyMode::Toppling && m_modeTime >= kToppleTime) {
        setMode(BodyMode::Down);
    }
    if (m_mode == BodyMode::GettingUp) {
        if (m_flop > 0.0f) {
            m_flop -= dt;
            m_modeTime = 0.0f;
        } else if (m_modeTime >= kGetUpTime) {
            setMode(BodyMode::Grounded);
        }
    }
    const bool grounded = m_mode == BodyMode::Grounded;

    m_crouch += ((in.stance == Stance::Crouch ? 1.0f : 0.0f) - m_crouch) * ease(10.0f, dt);
    m_slide += ((in.stance == Stance::Slide ? 1.0f : 0.0f) - m_slide) * ease(10.0f, dt);
    m_prone += ((in.stance == Stance::Crawl ? 1.0f : 0.0f) - m_prone) * ease(8.0f, dt);
    m_roll = in.stance == Stance::Crawl || in.stance == Stance::Dive ? in.roll : m_roll * (1.0f - ease(8.0f, dt));
    const float backed = 0.5f - 0.5f * std::cos(m_roll);
    m_dive += ((in.stance == Stance::Dive ? 1.0f : 0.0f) - m_dive) * ease(12.0f, dt);

    const float flat = grounded ? std::max(m_prone, m_dive) : 0.0f;
    const float bellySlide = m_prone * std::clamp(speed - 1.3f, 0.0f, 1.0f);
    const float crawling = m_prone * std::clamp(speed / 0.5f, 0.0f, 1.0f) * (1.0f - std::clamp(speed - 1.3f, 0.0f, 1.0f));
    if (speed < 2.3f) {
        m_crawlPhase = std::fmod(m_crawlPhase + speed * dt / kCrawlStride, 1.0f);
    }

    m_torsoYaw += wrapAngle(in.yaw - m_torsoYaw) * ease(18.0f, dt);
    const float hipDiff = wrapAngle(in.yaw - m_hipYaw);
    if (speed > 0.6f || std::abs(hipDiff) > 0.78f) {
        m_turning = true;
    }
    if (m_turning) {
        m_hipYaw += hipDiff * ease(9.0f, dt);
        if (std::abs(hipDiff) < 0.05f && speed <= 0.6f) {
            m_turning = false;
        }
    }
    const glm::vec3 hipForward = flatForward(m_hipYaw);
    const glm::vec3 hipRight = flatRight(m_hipYaw);

    const glm::vec3 acceleration = dt > 1e-5f ? change / dt : glm::vec3(0.0f);
    const glm::vec3 aimRight = flatRight(m_torsoYaw);
    const glm::vec3 aimForward = flatForward(m_torsoYaw);
    const glm::vec3 leanTarget = glm::clamp(
        glm::vec3(glm::dot(flatVelocity, aimRight) * 0.03f - glm::dot(acceleration, aimRight) * 0.004f, 0.0f,
                  glm::dot(flatVelocity, aimForward) * 0.04f - glm::dot(acceleration, aimForward) * 0.004f),
        glm::vec3(-0.35f), glm::vec3(0.35f));
    m_lean.value -= leanTarget - m_lastLeanTarget;
    m_lastLeanTarget = leanTarget;
    m_lean.update(dt, 80.0f, 0.6f);
    const glm::vec3 lean = leanTarget + m_lean.value;
    m_stir = std::max(m_stir * std::exp(-2.5f * dt), std::min(glm::length(acceleration) * 0.03f, 1.0f));

    if (in.grounded && !m_wasGrounded && m_fallSpeed > 1.0f) {
        m_squash.velocity.x -= std::min(m_fallSpeed, 8.0f) * 0.35f;
        m_stir = std::max(m_stir, 0.5f);
    }
    m_wasGrounded = in.grounded;
    m_fallSpeed = in.grounded ? 0.0f : std::max(0.0f, -in.velocity.y);
    if (!in.grounded) {
        m_squash.velocity.x += std::clamp(in.velocity.y, -4.0f, 4.0f) * 0.35f * dt;
    }
    m_squash.update(dt, 160.0f, 0.55f);
    m_headKick.update(dt, 220.0f, 0.5f);
    m_stagger.update(dt, 60.0f, 0.75f);
    const float stretch = std::clamp(m_squash.value.x, -0.35f, 0.35f);

    const float width = s.hipWidth * (1.0f + 0.3f * m_crouch);
    for (Foot& foot : m_feet) {
        if (glm::distance(foot.planted, in.feet) > 1.5f) {
            foot = {in.feet, in.feet, in.feet, -1.0f};
        }
    }
    const float stepTime = stepDuration(speed);

    const bool running = grounded && in.grounded && in.sprinting && speed > 3.0f && in.stance == Stance::Stand;
    m_run += ((running ? 1.0f : 0.0f) - m_run) * ease(9.0f, dt);
    const float phaseBefore = m_runPhase;
    m_runPhase = std::fmod(m_runPhase + speed * dt / kStride, 1.0f);
    const glm::vec3 moveDir = safeNormalize(flatVelocity, hipForward);
    std::array<glm::vec3, 2> runFeet{};

    const float runLead = -std::cos(glm::two_pi<float>() * (m_runPhase + 0.08f));
    for (int i = 0; i < 2; ++i) {
        const glm::vec2 at = runFoot(std::fmod(m_runPhase + 0.5f * static_cast<float>(i), 1.0f));
        runFeet[static_cast<std::size_t>(i)] = in.feet + moveDir * at.x + hipRight * ((i == 0 ? -width : width) * 0.8f) + kUp * at.y;
    }
    if (grounded && m_run > 0.99f) {
        for (int i = 0; i < 2; ++i) {
            Foot& foot = m_feet[static_cast<std::size_t>(i)];
            const glm::vec3& at = runFeet[static_cast<std::size_t>(i)];
            foot.step = -1.0f;
            foot.planted = {at.x, in.feet.y, at.z};
        }
        const bool leftLanded = m_runPhase < phaseBefore;
        if (leftLanded || (phaseBefore < 0.5f && m_runPhase >= 0.5f)) {
            m_lastStep = leftLanded ? 0 : 1;
            ++m_steps;
            m_lastStepLength = kStride * 0.5f;
        }
    } else if (grounded && flat > 0.5f) {
        for (int i = 0; i < 2; ++i) {
            Foot& foot = m_feet[static_cast<std::size_t>(i)];
            const glm::vec3& end = m_pose.legs[static_cast<std::size_t>(i)].end;
            foot.step = -1.0f;
            foot.planted = {end.x, in.feet.y, end.z};
        }
    } else if (grounded) {
        const float ahead = std::min(speed * stepTime * 0.5f, 0.2f);
        stepFeet(dt, in, hipRight, speed, flatVelocity, safeNormalize(flatVelocity, glm::vec3(0.0f)) * ahead, width);
    }

    float lift = 0.0f;
    int stepping = -1;
    for (int i = 0; i < 2; ++i) {
        const Foot& foot = m_feet[static_cast<std::size_t>(i)];
        if (foot.step >= 0.0f) {
            lift = std::max(lift, std::sin(foot.step * glm::pi<float>()));
            stepping = i;
        }
    }
    const float speedK = std::clamp(speed / 3.0f, 0.0f, 1.6f);
    const float moving = std::min(speed, 1.0f);

    const float bob = glm::mix((lift - 0.5f) * 0.024f * std::min(speedK, 1.2f),
                               0.011f * std::cos(2.0f * glm::two_pi<float>() * (m_runPhase - kStance - 0.05f)), m_run);
    const float sway = stepping < 0 ? 0.0f : (stepping == 0 ? 1.0f : -1.0f) * 0.022f * std::min(speedK, 1.0f) * lift * (1.0f - m_run);
    const float idle = 1.0f - moving;

    m_lead += (std::clamp(glm::dot(m_pose.legs[1].end - m_pose.legs[0].end, hipForward) / 0.35f, -1.0f, 1.0f) - m_lead) * ease(30.0f, dt);
    const float leading = glm::mix(m_lead, runLead, m_run);
    const float twist = -leading * 0.12f * std::min(speedK, 1.0f) * (1.0f - flat);
    const float breath = std::sin(m_time * 1.7f) * 0.015f;

    const float hipHeight = s.hip - m_crouch * 0.24f - m_slide * 0.3f + std::min(stretch, 0.0f) * 0.25f + bob -
                            glm::mix(0.015f * moving, 0.04f, m_run);
    const float core = s.core * (1.0f - m_crouch * 0.33f - m_slide * 0.3f) * (1.0f + stretch * 0.5f);
    const glm::vec3 pelvis = in.feet + kUp * hipHeight + hipRight * (sway + std::sin(m_time * 0.45f) * 0.02f * idle) + m_stagger.value;

    const glm::vec3 torsoRight = flatRight(m_torsoYaw + twist);
    const glm::vec3 torsoForward = flatForward(m_torsoYaw + twist);
    const glm::vec3 tilt = safeNormalize(kUp + torsoForward * (lean.z + m_run * 0.25f + m_crouch * 0.22f - m_slide * 0.35f) + torsoRight * lean.x, kUp);
    const glm::quat standing = orientation(safeNormalize(torsoRight - tilt * glm::dot(torsoRight, tilt), torsoRight), tilt);
    const glm::quat upright = orientation(hipRight, kUp);

    glm::quat q = standing;
    glm::vec3 body = pelvis + tilt * (core - 0.02f);
    float getUp = 1.0f;
    switch (m_mode) {
    case BodyMode::Grounded:
        if (m_settle > 0.0f) {
            q = glm::slerp(standing, m_settleFrom, m_settle);
            m_settle = std::max(0.0f, m_settle - dt / 0.25f);
        }
        body = pelvis + (q * kUp) * (core - 0.02f);
        break;
    case BodyMode::Flung:

        m_orient = glm::normalize(m_orient + (glm::quat(0.0f, m_spin) * m_orient) * (0.5f * dt));
        m_spin *= std::exp(-0.7f * dt);
        q = m_orient;
        body = in.feet + kUp * 0.78f;
        break;
    case BodyMode::Toppling:
    case BodyMode::Down:
    case BodyMode::GettingUp: {
        float angle = glm::half_pi<float>();
        float height = s.hip + s.core;
        if (m_mode == BodyMode::Toppling) {
            const float u = std::min(m_modeTime / kToppleTime, 1.0f);
            angle *= u * u;
        } else if (m_mode == BodyMode::GettingUp && m_flop <= 0.0f) {
            const float t = std::min(m_modeTime / kGetUpTime, 1.0f);
            getUp = t;
            if (t < 0.55f) {
                angle = glm::mix(glm::half_pi<float>(), -0.55f, glm::smoothstep(0.0f, 1.0f, t / 0.55f));
                height = s.hip * 0.5f + s.core;
            } else {
                angle = glm::mix(-0.55f, 0.0f, glm::smoothstep(0.0f, 1.0f, (t - 0.55f) / 0.45f));
                height = glm::mix(s.hip * 0.5f, s.hip, glm::smoothstep(0.55f, 1.0f, t)) + s.core;
            }
        }
        const glm::quat r = glm::angleAxis(angle, hipRight);
        q = r * upright;
        body = in.feet + r * (kUp * (height - 0.02f)) + kUp * (s.depth * std::max(std::sin(angle), 0.0f));
        if (m_mode == BodyMode::GettingUp && m_flop > 0.0f) {
            const float k = std::clamp(1.0f - m_flop / kFlopTime, 0.0f, 1.0f);
            q = glm::slerp(m_settleFrom, q, k);
            body = glm::mix(in.feet + kUp * 0.5f, body, k);
            getUp = 0.0f;
        } else if (m_mode != BodyMode::GettingUp) {
            getUp = 0.0f;
        }
        break;
    }
    }
    if (flat > 0.001f) {
        const glm::vec3 lieForward = in.stance == Stance::Crawl || in.stance == Stance::Dive ? flatForward(in.lieYaw) : hipForward;
        glm::vec3 along = lieForward;
        if (m_dive > 0.01f && glm::length(in.velocity) > 1.0f) {
            const glm::vec3 flight = safeNormalize(in.velocity, hipForward);
            along = safeNormalize(glm::mix(lieForward, flight * glm::vec3(1.0f, 0.4f, 1.0f), m_dive / (m_dive + m_prone + 1e-4f)), lieForward);
        }
        const glm::vec3 lyingUp = safeNormalize(along + kUp * (0.18f * m_prone), along);
        const glm::vec3 lyingRight = safeNormalize(glm::cross(lyingUp, kUp), hipRight);

        const glm::quat lying = glm::angleAxis(-m_roll, lyingUp) * orientation(lyingRight, lyingUp);
        const float breathe = std::sin(m_time * 1.7f) * 0.008f * (1.0f - crawling);
        const glm::vec3 onGround =
            in.feet + kUp * (s.depth + 0.04f + breathe + (s.radius - s.depth) * std::abs(std::sin(m_roll))) + along * 0.1f;
        const glm::vec3 inAir = in.feet + kUp * 0.32f;
        const glm::vec3 lyingAt = in.grounded ? onGround : glm::mix(onGround, inAir, m_dive);
        q = glm::slerp(q, lying, flat);
        body = glm::mix(body, lyingAt, flat);
    }
    const glm::vec3 right = q * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 up = q * kUp;
    const glm::vec3 forward = -(q * glm::vec3(0.0f, 0.0f, 1.0f));

    BodyPose& p = m_pose;
    p.mode = m_mode;
    p.body = body;
    p.bodyBasis = glm::mat3(right, up, forward);
    p.bodyRadii = glm::vec3(s.radius * (1.0f - stretch * 0.2f) * (1.0f + breath), core, s.depth * (1.0f - stretch * 0.2f) * (1.0f + breath));
    p.stir = std::max(m_stir, m_mode == BodyMode::Flung ? 0.6f : 0.0f);
    const float blinkPhase = std::fmod(m_time + std::sin(m_time * 0.37f) * 1.3f + 10.0f, 3.7f);
    p.blink = blinkPhase > 3.55f ? 1.0f : 0.0f;
    if (m_mode == BodyMode::Down || (m_mode == BodyMode::Toppling && m_modeTime > kToppleTime * 0.6f) ||
        (m_mode == BodyMode::GettingUp && (m_flop > 0.0f || m_modeTime < kGetUpTime * 0.3f))) {
        p.blink = 1.0f;
    }

    const glm::vec3 neck = body + up * (core - 0.03f);
    glm::vec3 headDir = grounded ? safeNormalize(up - forward * (in.pitch * 0.22f), up) : up;

    const float onBackHead = std::abs(std::cos(m_roll));
    headDir = safeNormalize(glm::mix(headDir, glm::mix(up, up * 0.75f + kUp * 0.65f, onBackHead), flat), up);
    p.head = neck + headDir * (s.neck + s.headRadius) + m_headKick.value;
    p.neck = neck;
    p.head.y = std::max(p.head.y, in.feet.y + s.headRadius * 0.85f);

    const float lookPitch = std::clamp(in.pitch, glm::mix(-0.95f, -0.4f, flat), glm::mix(1.05f, 0.75f, flat));
    p.headForward = grounded ? viewForward(in.yaw, lookPitch) : forward;

    const float alongLook = std::abs(glm::dot(p.headForward, up));
    glm::vec3 lyingHeadUp = safeNormalize(glm::mix(up, -forward, alongLook * alongLook), kUp);
    lyingHeadUp = safeNormalize(glm::mix(lyingHeadUp, kUp, std::max(0.0f, -std::cos(m_roll))), kUp);
    const glm::vec3 headUpWanted = safeNormalize(glm::mix(up, lyingHeadUp, flat), up);
    p.headUp = safeNormalize(headUpWanted - p.headForward * glm::dot(headUpWanted, p.headForward), -forward);

    const float star = m_mode == BodyMode::Down ? 1.0f : (m_mode == BodyMode::Toppling ? glm::smoothstep(0.4f, 1.0f, m_modeTime / kToppleTime) : 0.0f);

    const glm::vec3 hips = body - up * (core - 0.02f);
    for (int i = 0; i < 2; ++i) {
        const float side = i == 0 ? -1.0f : 1.0f;
        const glm::vec3 hip = hips + right * (side * width);
        Chain& rope = m_legRope[static_cast<std::size_t>(i)];
        Chain& before = m_legRopeBefore[static_cast<std::size_t>(i)];
        if (m_mode == BodyMode::Flung) {
            rope[0] = hip;

            const JointLimits knee{-up + forward * 0.35f, 1.3f, right, -2.4f, 0.0f};
            simulateLimb(rope, before, s.thigh, s.shin, knee, m_hinge[static_cast<std::size_t>(2 + i)], m_bend[static_cast<std::size_t>(2 + i)], dt, in.feet.y + 0.05f);
            p.legs[static_cast<std::size_t>(i)] = {rope[0], rope[1], rope[2]};
            p.footForward[static_cast<std::size_t>(i)] = forward;
            continue;
        }
        const Foot& foot = m_feet[static_cast<std::size_t>(i)];
        glm::vec3 target = foot.planted;
        if (star > 0.0f) {
            const glm::vec3 spread = glm::normalize(-up + right * (side * 0.75f));
            glm::vec3 splayed = hip + spread * ((s.thigh + s.shin) * 0.97f);
            splayed.y = in.feet.y + 0.12f;
            target = glm::mix(target, splayed, star);
        }
        if (grounded && !in.grounded) {
            target = hip - kUp * (s.thigh + s.shin) * 0.72f + hipForward * (0.06f * side);
        } else if (grounded && foot.step >= 0.0f) {
            const float t = foot.step * foot.step * (3.0f - 2.0f * foot.step);
            target = glm::mix(foot.from, foot.to, t) + kUp * (std::sin(foot.step * glm::pi<float>()) * (0.05f + speed * 0.006f));
        }
        if (grounded && in.grounded && m_run > 0.001f) {
            target = glm::mix(target, runFeet[static_cast<std::size_t>(i)], m_run);
        }
        if (grounded && m_slide > 0.01f) {
            const glm::vec3 slid = i == 1 ? in.feet + hipForward * 0.42f + hipRight * 0.08f : in.feet + hipForward * 0.08f - hipRight * 0.2f;
            target = glm::mix(target, slid, m_slide);
        }
        glm::vec3 pole = forward + right * (side * 0.3f);
        if (flat > 0.001f) {
            const float reachBack = (s.thigh + s.shin) * glm::mix(0.9f, 0.97f, std::max(m_dive, bellySlide));
            const float draw = std::max(0.0f, std::sin(glm::two_pi<float>() * (m_crawlPhase + 0.5f * static_cast<float>(i)))) * crawling;
            glm::vec3 lie = hip - up * (reachBack * (1.0f - 0.5f * draw)) + right * (side * (0.05f + 0.2f * draw));
            if (in.grounded) {
                lie.y = glm::mix(in.feet.y + 0.07f, std::max(lie.y, in.feet.y + 0.07f), std::abs(std::sin(m_roll)));
            }
            target = glm::mix(target, lie, flat);
            pole = glm::mix(pole, glm::mix(right * side - kUp * 0.4f, right * (side * 0.3f) + kUp, backed), flat);
        }
        const Limb leg = solveTwoBone(hip, target, s.thigh, s.shin, pole);
        p.legs[static_cast<std::size_t>(i)] = leg;
        p.footForward[static_cast<std::size_t>(i)] = grounded ? hipForward : safeNormalize(forward, hipForward);
        if (flat > 0.001f) {
            const glm::vec3 toes = safeNormalize(glm::mix(-kUp * 0.8f, kUp * 0.8f, backed) - up * 0.6f, -kUp);
            p.footForward[static_cast<std::size_t>(i)] = safeNormalize(glm::mix(p.footForward[static_cast<std::size_t>(i)], toes, flat), toes);
        }
        if (star > 0.0f) {
            const glm::vec3 slack = safeNormalize(kUp * 0.8f + right * (side * 0.5f) - up * 0.25f, kUp);
            p.footForward[static_cast<std::size_t>(i)] = safeNormalize(glm::mix(p.footForward[static_cast<std::size_t>(i)], slack, star), slack);
        }
        before = rope;
        rope = {leg.root, leg.joint, leg.end};
        m_hinge[static_cast<std::size_t>(2 + i)] = right;
    }

    const float ikTarget = grounded ? 1.0f : (m_mode == BodyMode::GettingUp && getUp > 0.6f ? 1.0f : 0.0f);
    m_ik += (ikTarget - m_ik) * ease(grounded ? 10.0f : 14.0f, dt);
    if (first) {
        m_ik = 1.0f;
    }
    const glm::vec3 shoulderBase = body + up * (core - 0.09f);
    const glm::vec3 shoulders[2] = {shoulderBase - right * s.shoulderWidth, shoulderBase + right * s.shoulderWidth};

    const glm::vec3& left = shoulders[0];
    glm::vec3 reach = left - kUp * 0.49f - aimRight * 0.04f + aimForward * 0.03f;

    glm::vec3 swing = hipForward * (leading * 0.18f * std::min(speedK, 1.2f));

    reach = glm::mix(reach, left - kUp * 0.3f + hipForward * 0.1f - hipRight * 0.03f, m_run);
    swing = glm::mix(swing, hipForward * (leading * 0.11f) + kUp * (leading * 0.04f), m_run);
    float swingTarget = 1.0f;
    if (!in.grounded) {
        reach = left - kUp * 0.15f - aimRight * 0.25f;
        swingTarget = 0.0f;
    } else if (m_slide > 0.5f) {
        reach = left - aimRight * 0.42f + kUp * 0.05f + aimForward * 0.05f;
        swingTarget = 0.0f;
    }
    if (flat > 0.001f) {
        const float ahead = 0.5f + 0.5f * std::sin(glm::two_pi<float>() * (m_crawlPhase + 0.5f)) * (crawling > 0.0f ? 1.0f : 0.0f);
        glm::vec3 crawlHand = left + up * (0.2f + 0.22f * ahead) - right * 0.06f;
        if (in.grounded) {
            crawlHand.y = in.feet.y + 0.07f;
        }
        const glm::vec3 stretched = left + up * 0.48f - right * 0.04f;
        reach = glm::mix(reach, glm::mix(crawlHand, stretched, std::max(m_dive, bellySlide)), flat);
        swingTarget = 0.0f;
    }
    if (backed > 0.001f) {
        const glm::vec3 belly = body + forward * (s.depth + 0.04f);
        reach = glm::mix(reach, in.holster > 0.5f ? belly - right * 0.1f : in.gunGrip - aimRight * 0.045f - kUp * 0.02f, backed);
        swingTarget = 0.0f;
    }
    if (in.loading >= 0.0f || in.ejector > 0.05f || in.crane > 0.05f || in.aiming) {
        swingTarget = 0.0f;
    }
    if (in.loading >= 0.0f) {
        const glm::vec3 belt = hips - right * 0.22f + aimForward * 0.1f + kUp * (0.06f * flat);
        const float t = std::sin(std::clamp(in.loading, 0.0f, 1.0f) * glm::pi<float>());
        reach = glm::mix(belt, in.cylinder - aimRight * 0.04f, 1.0f - t * 0.8f);
    } else if (in.ejector > 0.05f) {
        reach = in.gunFront;
    } else if (in.crane > 0.05f) {
        reach = in.cylinder - aimRight * 0.05f;
    } else if (in.aiming) {
        reach = in.gunGrip - aimRight * 0.045f - kUp * 0.02f;
    }

    const bool busy = in.loading >= 0.0f || in.ejector > 0.05f || in.crane > 0.05f || in.aiming;
    m_carry += ((running && !busy && in.holster < 0.5f ? 1.0f : 0.0f) - m_carry) * ease(7.0f, dt);
    const float carry = m_carry * m_carry * (3.0f - 2.0f * m_carry);
    const glm::vec3 carryHand = shoulders[1] - kUp * (0.3f + leading * 0.04f) + hipForward * (0.1f - leading * 0.11f) + hipRight * 0.03f;
    glm::vec3 gunHand = glm::mix(in.gunGrip, carryHand, carry);
    glm::vec3 gunPole = glm::mix(glm::mix(-kUp + right * 0.7f - forward * 0.15f, -forward * 0.8f + right * 0.35f - kUp * 0.2f, carry),
                                       -kUp * 0.5f + right * 0.8f, flat);

    const glm::vec3 holsterGrip = hips + right * (s.radius + 0.04f) + up * 0.16f - forward * 0.07f;
    const glm::vec3 holsterAim = safeNormalize(-up + forward * 0.07f, -up);
    const float toHip = glm::smoothstep(0.0f, 0.6f, in.holster);
    m_handFree += ((in.holster >= 0.999f ? 1.0f : 0.0f) - m_handFree) * ease(8.0f, dt);
    glm::vec3 hang = glm::mix(shoulders[1] - kUp * 0.49f + aimRight * 0.04f + aimForward * 0.03f - swing, carryHand, m_run);
    if (flat > 0.001f) {
        const float aheadR = 0.5f + 0.5f * std::sin(glm::two_pi<float>() * m_crawlPhase) * (crawling > 0.0f ? 1.0f : 0.0f);
        glm::vec3 crawlR = shoulders[1] + up * (0.2f + 0.22f * aheadR) + right * 0.06f;
        if (in.grounded) {
            crawlR.y = in.feet.y + 0.07f;
        }
        hang = glm::mix(hang, glm::mix(crawlR, shoulders[1] + up * 0.48f + right * 0.04f, std::max(m_dive, bellySlide)), flat);
        hang = glm::mix(hang, body + forward * (s.depth + 0.04f) + right * 0.1f, backed);
    }
    gunHand = glm::mix(glm::mix(gunHand, holsterGrip, toHip), hang, m_handFree);
    gunPole = glm::mix(gunPole, -forward * 0.8f + right * 0.35f - kUp * 0.2f, toHip * (1.0f - flat));
    p.holster = in.holster;
    p.holsterGrip = holsterGrip;
    p.holsterAim = holsterAim;
    p.holsterUp = forward;
    p.carry = carry;
    p.carryAim = glm::normalize(hipForward * (0.64f - leading * 0.2f) - kUp * 0.77f);
    p.feet = in.feet;
    if (first) {
        m_lastFreeTarget = reach - left;
    }

    m_freeHand.value -= (reach - left) - m_lastFreeTarget;
    m_lastFreeTarget = reach - left;
    m_freeHand.update(dt, 300.0f, 0.75f);
    m_swing += (swingTarget - m_swing) * ease(10.0f, dt);
    glm::vec3 freeHand = reach + m_freeHand.value + swing * m_swing;

    m_reach += ((in.reach ? 1.0f : 0.0f) - m_reach) * ease(7.0f, dt);
    if (m_reach > 0.001f) {
        const float reachSmooth = m_reach * m_reach * (3.0f - 2.0f * m_reach);
        const float armLength = (s.upperArm + s.forearm) * 0.95f;
        for (int a = 0; a < 2; ++a) {
            const glm::vec3 toward = in.reachTo - shoulders[static_cast<std::size_t>(a)];
            const float distance = glm::length(toward);
            const glm::vec3 out = shoulders[static_cast<std::size_t>(a)] + (distance > 1e-4f ? toward / distance * std::min(distance, armLength) : glm::vec3(0.0f));
            if (a == 0) {
                freeHand = glm::mix(freeHand, out, reachSmooth * (1.0f - m_handFree));
            } else {
                gunHand = glm::mix(gunHand, out, reachSmooth * m_handFree);
            }
        }
    }

    for (int a = 0; a < 2; ++a) {
        Chain& rope = m_armRope[static_cast<std::size_t>(a)];
        Chain& before = m_armsBefore[static_cast<std::size_t>(a)];
        const glm::vec3& shoulder = shoulders[a];
        const float side = a == 0 ? -1.0f : 1.0f;
        const Limb ik = a == 1 ? reachFor(shoulder, gunHand, s.upperArm, s.forearm, gunPole)
                               : reachFor(shoulder, freeHand, s.upperArm, s.forearm, glm::mix(-forward * 0.8f - right * 0.35f - kUp * 0.2f, -kUp * 0.5f - right * 0.8f, flat));
        const Chain placed{ik.root, ik.joint, ik.end};
        if (first) {
            rope = placed;
            before = placed;
        }
        if (m_ik > 0.999f) {
            before = rope;
            m_hinge[static_cast<std::size_t>(a)] = right;
            rope = placed;
            p.arms[static_cast<std::size_t>(a)] = alongArm(ik.root, ik.joint, ik.end);
            continue;
        }

        rope[0] = shoulder;

        glm::vec3 starHand = shoulder + glm::normalize(right * side + up * 0.45f) * ((s.upperArm + s.forearm) * 0.92f);
        starHand.y = std::max(starHand.y, in.feet.y + 0.06f);
        const Limb starArm = reachFor(shoulder, starHand, s.upperArm, s.forearm, -up + kUp * 0.3f);
        if (star >= 1.0f) {
            before = {starArm.root, starArm.joint, starArm.end};
            rope = before;
        } else {
            const JointLimits elbow{-up * 0.6f + right * (side * 0.5f) + forward * 0.3f, 1.9f, right, 0.0f, 2.5f};
            simulateLimb(rope, before, s.upperArm, s.forearm, elbow, m_hinge[static_cast<std::size_t>(a)], m_bend[static_cast<std::size_t>(a)], dt, in.feet.y + 0.05f);
        }

        glm::vec3 hand = glm::mix(rope[2], starArm.end, star);
        glm::vec3 elbowAt = glm::mix(rope[1], starArm.joint, star);
        hand = glm::mix(hand, ik.end, m_ik);
        elbowAt = glm::mix(elbowAt, ik.joint, m_ik);
        const Limb blended = solveTwoBone(shoulder, hand, s.upperArm, s.forearm, elbowAt - (shoulder + hand) * 0.5f);
        p.arms[static_cast<std::size_t>(a)] = alongArm(blended.root, blended.joint, blended.end);
    }
    p.gunFree = m_ik < 0.5f;
    return p;
}

}
