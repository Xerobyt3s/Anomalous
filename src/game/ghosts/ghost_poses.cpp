#include "game/ghosts/ghost_poses.h"

#include <algorithm>

namespace ghost::game {

std::vector<PoseOption> posesOf(GhostBehavior behavior) {
    switch (behavior) {
    case GhostBehavior::Poltergeist: return {{"Idle (unseen)", GhostState::Wander}, {"Lifting", GhostState::Lift}};
    case GhostBehavior::BallLightning: return {{"Waiting", GhostState::Charge}, {"Mid-jump", GhostState::Hop}};
    case GhostBehavior::Necromite:
        return {{"Coming up (loops)", GhostState::Emerge}, {"Crawling", GhostState::Squirm}, {"Going under (loops)", GhostState::Burrow}};
    case GhostBehavior::Mimic:
        return {{"Disguised", GhostState::Disguised}, {"Reveal (loops)", GhostState::Reveal}, {"Standing", GhostState::Pause},
                {"Dashing", GhostState::Hunt},        {"In the air", GhostState::Leap},        {"Lash (loops)", GhostState::Windup},
                {"Folding away (loops)", GhostState::Conceal}};
    case GhostBehavior::Vasskraka:
        return {{"Roosting cluster", GhostState::Roost}, {"Raven", GhostState::Circle},  {"Stoop (loops)", GhostState::Stoop},
                {"Spiked ball", GhostState::Gather},     {"Cloud", GhostState::Scatter}, {"Burst (loops)", GhostState::Reform}};
    case GhostBehavior::Wisp:
    default: return {{"Idle", GhostState::Wander}, {"Luring", GhostState::Lure}, {"Rushing", GhostState::Rush}};
    }
}

GhostWorld::Pose specimenPose(const GhostDef& def, int pose, const glm::vec3& at, const glm::vec3& forward, const glm::vec3& up,
                              float size) {
    const std::vector<PoseOption> poses = posesOf(def.behavior);
    const GhostState state = poses[static_cast<std::size_t>(std::clamp(pose, 0, static_cast<int>(poses.size()) - 1))].state;
    glm::vec3 side = glm::cross(forward, up);
    side = glm::dot(side, side) > 1e-8f ? glm::normalize(side) : glm::vec3(1.0f, 0.0f, 0.0f);
    GhostWorld::Pose out;
    out.state = state;
    out.position = at + up * 1.6f;
    switch (def.behavior) {
    case GhostBehavior::Mimic:
        out.position = at + up * def.mimic.bodyRadius;
        if (state == GhostState::Hunt) {
            out.velocity = side * def.mimic.burstSpeed;
        } else if (state == GhostState::Leap) {
            out.position += up * 1.2f;
            out.velocity = side * 4.0f + up;
            out.attached = false;
        }
        out.loop = state == GhostState::Reveal    ? def.mimic.revealTime + 0.7f
                   : state == GhostState::Conceal ? def.mimic.concealTime + 0.7f
                   : state == GhostState::Windup  ? 1.4f
                                                  : 0.0f;
        break;
    case GhostBehavior::Vasskraka:
        out.position = at + up * 1.5f;
        out.health = def.health * size;
        out.normal = -forward;
        if (state == GhostState::Circle) {
            out.velocity = side * def.vasskraka.circleSpeed;
        } else if (state == GhostState::Stoop) {
            out.velocity = glm::normalize(side - up * 0.5f) * def.vasskraka.diveSpeed;
            out.loop = def.vasskraka.diveWindup + 1.6f;
        } else if (state == GhostState::Reform) {
            out.loop = 2.4f;
        }
        out.attached = state == GhostState::Roost;
        break;
    case GhostBehavior::Wisp:
        out.position = at + up * def.wisp.hoverHeight;
        break;
    case GhostBehavior::Necromite:
        out.position = at + up * def.radius;
        out.velocity = state == GhostState::Squirm ? side * def.necromite.crawlSpeed : glm::vec3(0.0f);
        out.loop = state == GhostState::Squirm ? 0.0f : def.necromite.emergeTime + 0.6f;
        break;
    default:
        break;
    }
    return out;
}

}
