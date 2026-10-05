#include "game/player/player.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace ghost::game {
namespace {
constexpr float kGroundHeight = 0.0f;

glm::vec3 approach(const glm::vec3& current, const glm::vec3& target, float maxDelta) {
    const glm::vec3 delta = target - current;
    const float distance = glm::length(delta);
    if (distance <= maxDelta || distance < 1e-6f) {
        return target;
    }
    return current + delta / distance * maxDelta;
}

}

glm::vec3 flatForward(float yaw) { return {std::sin(yaw), 0.0f, -std::cos(yaw)}; }

glm::vec3 flatRight(float yaw) { return {std::cos(yaw), 0.0f, std::sin(yaw)}; }

glm::vec3 viewForward(float yaw, float pitch) {
    return {std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)};
}

Player::Player(const glm::vec3& spawn) {
    m_state.position = spawn;
    m_previous = m_state;
}

void Player::addImpulse(const glm::vec3& deltaVelocity) {
    m_state.velocity += deltaVelocity;
    if (deltaVelocity.y > 0.0f) {
        m_state.grounded = false;
    }
}

void Player::applyHaste(float duration, float speedScale) {
    m_state.hasteTime = duration;
    m_state.hasteScale = speedScale;
}

void Player::applyShroud(float duration) { m_state.shroudTime = duration; }

bool Player::hurt(float amount) {
    m_state.health = std::fmax(m_state.health - amount, 0.0f);
    m_state.sinceHurt = 0.0f;
    return m_state.health <= 0.0f;
}

void Player::setVitals(float health, float sinceHurt, bool downed) {
    m_state.health = health;
    m_state.sinceHurt = sinceHurt;
    m_state.downed = downed;
}

void Player::respawn(const glm::vec3& position) {
    const float yaw = m_state.yaw;
    m_state = PlayerState{};
    m_state.position = position;
    m_state.yaw = yaw;
    m_previous = m_state;
}

void Player::teleport(const glm::vec3& position) {
    m_state.position = position;
    m_state.position.y = std::fmax(position.y, kGroundHeight);
    m_state.velocity.y = 0.0f;
    m_state.grounded = m_state.position.y <= kGroundHeight;
    m_previous.position = m_state.position;
}

void Player::tick(const PlayerCommand& command, float dt, const PlayerEnvironment& environment) {
    m_previous = m_state;
    PlayerState& s = m_state;

    s.yaw = command.yaw;
    s.pitch = command.pitch;

    glm::vec2 move = command.move;
    if (glm::dot(move, move) > 1.0f) {
        move = glm::normalize(move);
    }
    if (s.downed) {
        move = glm::vec2(0.0f);
    }

    const float speedBefore = glm::length(glm::vec2(s.velocity.x, s.velocity.z));
    const Stance stanceBefore = s.stance;
    const bool pressed = command.crouch && !s.crouchHeld;
    s.crouchHeld = command.crouch;
    s.slideCooldown = std::fmax(s.slideCooldown - dt, 0.0f);
    const bool canStand = !environment.fits || environment.fits(s.position, m_tuning.height);
    auto rise = [&] { s.stance = (command.crouch || !canStand) ? Stance::Crouch : Stance::Stand; };
    if (s.grounded) {
        s.diveUsed = false;
    }

    const bool dive = command.jump && !s.grounded && !s.diveUsed && !s.downed && s.stance != Stance::Crawl && s.stance != Stance::Dive;
    bool jumpTaken = false;
    if (dive) {
        s.stance = Stance::Dive;
        s.diveUsed = true;
        jumpTaken = true;

        const glm::vec3 wished = flatForward(s.yaw) * move.y + flatRight(s.yaw) * move.x;
        const glm::vec3 forward = glm::length(wished) > 0.1f ? glm::normalize(wished) : flatForward(s.yaw);
        glm::vec3 horizontal{s.velocity.x, 0.0f, s.velocity.z};
        const float along = glm::dot(horizontal, forward);
        horizontal = forward * std::fmax(along, m_tuning.diveSpeed) + (horizontal - forward * along) * 0.5f;
        s.velocity = glm::vec3(horizontal.x, std::fmax(s.velocity.y, m_tuning.diveLift), horizontal.z);

        s.lieYaw = std::atan2(forward.x, -forward.z);
        s.roll = std::remainder(s.yaw - s.lieYaw, glm::two_pi<float>());
    } else if (command.crawl && !s.downed && s.stance != Stance::Crawl && s.stance != Stance::Dive) {
        s.stance = Stance::Crawl;
    } else {
        switch (s.stance) {
        case Stance::Stand:
            if (pressed && s.grounded && s.sprinting && speedBefore >= m_tuning.slideMinSpeed && s.slideCooldown <= 0.0f) {
                s.stance = Stance::Slide;
                s.velocity.x *= m_tuning.slideBoost;
                s.velocity.z *= m_tuning.slideBoost;
            } else if (command.crouch) {
                s.stance = Stance::Crouch;
            }
            break;
        case Stance::Crouch:
            rise();
            break;
        case Stance::Slide:
            if (!command.crouch || (s.grounded && speedBefore < m_tuning.slideEndSpeed) || (s.grounded && command.jump)) {
                rise();
                s.slideCooldown = m_tuning.slideCooldown;
            }
            break;
        case Stance::Crawl:

            if (command.crawl || command.jump) {
                jumpTaken = command.jump;
                const bool canCrouch = !environment.fits || environment.fits(s.position, m_tuning.crouchHeight);
                if (canStand || canCrouch) {
                    rise();
                }
            }
            break;
        case Stance::Dive:
            if (s.grounded) {
                s.stance = Stance::Crawl;
            }
            break;
        }
    }
    if (s.downed && (s.stance == Stance::Crawl || s.stance == Stance::Dive)) {
        s.stance = Stance::Stand;
    }

    if (s.stance == Stance::Crawl || s.stance == Stance::Dive) {
        if (stanceBefore != Stance::Crawl && stanceBefore != Stance::Dive && s.stance == Stance::Crawl) {
            s.lieYaw = s.yaw;
            s.roll = 0.0f;
        }

        if (s.grounded && speedBefore > 0.3f) {
            const float travel = std::atan2(s.velocity.x, -s.velocity.z);
            const float turn = std::remainder(travel - s.lieYaw, glm::two_pi<float>());
            const float most = glm::pi<float>() * dt;
            s.lieYaw += std::clamp(turn, -most, most);
        }

        const float target = s.roll + std::remainder(std::remainder(s.yaw - s.lieYaw, glm::two_pi<float>()) - s.roll, glm::two_pi<float>());
        const float rate = glm::half_pi<float>() / std::fmax(m_tuning.rollTime, 0.01f);
        s.roll += std::clamp(target - s.roll, -rate * dt, rate * dt);
    } else {
        s.roll = 0.0f;
        s.lieYaw = s.yaw;
    }
    const bool flat = s.stance == Stance::Crawl || s.stance == Stance::Dive;
    const bool sliding = s.stance == Stance::Slide;
    const bool low = s.stance != Stance::Stand;
    s.height = s.downed ? m_tuning.downedHeight : (flat ? m_tuning.crawlHeight : (low ? m_tuning.crouchHeight : m_tuning.height));
    const float eyeTarget =
        s.downed ? m_tuning.downedEyeHeight
                 : (s.stance == Stance::Dive ? m_tuning.diveEyeHeight
                    : s.stance == Stance::Crawl ? m_tuning.crawlEyeHeight
                    : (sliding ? m_tuning.slideEyeHeight : (low ? m_tuning.crouchEyeHeight : m_tuning.eyeHeight)));
    s.eyeHeight += (eyeTarget - s.eyeHeight) * (1.0f - std::exp(-m_tuning.stanceRate * dt));

    s.sprinting = command.sprint && !command.aim && !command.handsBusy && move.y > 0.5f && !low;

    if (command.holster && !command.handsBusy && !s.downed) {
        s.holstered = !s.holstered;
    }
    if (s.downed) {
        s.holstered = false;
    }
    const float holsterRate = 1.0f / std::fmax(s.holstered ? m_tuning.holsterTime : m_tuning.drawTime, 0.01f);
    s.holster = std::clamp(s.holster + (s.holstered ? dt : -dt) * holsterRate, 0.0f, 1.0f);
    s.aiming = command.aim && !command.handsBusy && s.holster <= 0.0f;

    s.hasteTime = std::fmax(s.hasteTime - dt, 0.0f);
    s.shroudTime = std::fmax(s.shroudTime - dt, 0.0f);
    s.sinceHurt += dt;
    if (s.sinceHurt > m_tuning.regenDelay) {
        s.health = std::fmin(s.health + m_tuning.regenRate * dt, 1.0f);
    }

    float speed = s.sprinting ? m_tuning.sprintSpeed : (s.aiming ? m_tuning.aimSpeed : m_tuning.walkSpeed);
    if (low) {
        speed = std::fmin(speed, s.stance == Stance::Crawl ? m_tuning.crawlSpeed : m_tuning.crouchSpeed);
    }
    if (s.hasteTime > 0.0f) {
        speed *= s.hasteScale;
    }
    if (s.holster >= 1.0f) {
        speed *= m_tuning.holsteredSpeed;
    }
    if (command.handsBusy) {
        speed = std::fmin(speed, m_tuning.busySpeed);
    }
    glm::vec3 wish = (flatForward(s.yaw) * move.y + flatRight(s.yaw) * move.x) * speed;

    const glm::vec3 drift{environment.wind.x, 0.0f, environment.wind.z};
    const bool windy = glm::dot(drift, drift) > 1e-4f;
    wish += drift;

    const bool hasWish = glm::dot(wish, wish) > 0.0f;
    float accel = s.grounded ? (hasWish ? m_tuning.groundAccel : m_tuning.groundDecel) : m_tuning.airAccel;
    if (s.stance == Stance::Dive) {
        accel = 0.0f;
    }
    if (!s.grounded && windy) {
        accel = std::fmax(accel, m_tuning.windAirAccel);
    }

    const bool bellySliding = s.stance == Stance::Crawl && s.grounded && speedBefore > m_tuning.crawlSpeed + 0.3f;
    if ((sliding || bellySliding) && s.grounded) {
        glm::vec3 v{s.velocity.x, 0.0f, s.velocity.z};
        const float sliding0 = glm::length(v);
        if (sliding0 > 1e-4f) {
            const glm::vec3 along = v / sliding0;
            glm::vec3 lean = flatForward(s.yaw) * move.y + flatRight(s.yaw) * move.x;
            lean -= along * glm::dot(lean, along);
            v += lean * (m_tuning.slideSteer * dt);
            const float kept = std::fmax(sliding0 - (bellySliding ? m_tuning.bellyFriction : m_tuning.slideFriction) * dt, 0.0f);
            v = glm::length(v) > 1e-4f ? glm::normalize(v) * kept : glm::vec3(0.0f);
        }
        s.velocity.x = v.x;
        s.velocity.z = v.z;
    } else {
        const glm::vec3 horizontal = approach({s.velocity.x, 0.0f, s.velocity.z}, wish, accel * dt);
        s.velocity.x = horizontal.x;
        s.velocity.z = horizontal.z;
    }

    if (s.grounded && command.jump && !s.downed && !jumpTaken && !flat) {
        s.velocity.y = m_tuning.jumpSpeed;
        s.grounded = false;
    }

    if (environment.wind.y > m_tuning.liftThreshold) {
        s.grounded = false;
        s.velocity.y += (environment.wind.y - s.velocity.y) * m_tuning.liftCoupling * dt;
    }
    if (!s.grounded) {
        s.velocity.y -= m_tuning.gravity * dt;
    }

    if (environment.move) {
        if (s.grounded) {
            s.velocity.y = -1.0f;
        }
        const engine::CapsuleMove moved = environment.move(s.position, s.velocity, dt);
        s.position = moved.position;
        for (int i = 0; i < moved.normalCount; ++i) {
            const glm::vec3& normal = moved.normals[static_cast<std::size_t>(i)];
            const float into = glm::dot(s.velocity, normal);
            if (into < 0.0f) {
                s.velocity -= normal * into;
            }
        }
        s.grounded = moved.grounded && s.velocity.y <= 0.1f;
    } else {
        s.position += s.velocity * dt;
    }

    glm::vec3 push{0.0f};
    for (std::size_t i = 0; i < environment.others.size(); ++i) {
        const PlayerEnvironment::Body& other = environment.others[i];
        if (s.position.y >= other.feet.y + other.height || other.feet.y >= s.position.y + s.height) {
            continue;
        }
        glm::vec2 apart{s.position.x - other.feet.x, s.position.z - other.feet.z};
        const float reach = m_tuning.radius + other.radius;
        const float distance = glm::length(apart);
        if (distance >= reach) {
            continue;
        }

        const float a = 2.4f * static_cast<float>(i + 1);
        const glm::vec2 out = distance > 1e-4f ? apart / distance : glm::vec2(std::cos(a), std::sin(a));
        push += glm::vec3(out.x, 0.0f, out.y) * (reach - distance);
        const float into = -glm::dot(glm::vec2(s.velocity.x, s.velocity.z), out);
        if (into > 0.0f) {
            s.velocity.x += out.x * into;
            s.velocity.z += out.y * into;
        }
    }
    if (glm::dot(push, push) > 1e-10f) {
        if (environment.move && dt > 1e-5f) {
            s.position = environment.move(s.position, push / dt, dt).position;
        } else {
            s.position += push;
        }
    }

    if (s.position.y <= kGroundHeight) {
        s.position.y = kGroundHeight;
        s.velocity.y = std::fmax(s.velocity.y, 0.0f);
        s.grounded = true;
    }
}

}
