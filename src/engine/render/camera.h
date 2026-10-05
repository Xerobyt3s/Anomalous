#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace ghost::engine {
struct Camera {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    float fovY = glm::radians(60.0f);
    float nearZ = 0.01f;
    float farZ = 100.0f;
    glm::quat frame{1.0f, 0.0f, 0.0f, 0.0f};

    glm::vec3 up() const { return frame * glm::vec3(0.0f, 1.0f, 0.0f); }
    glm::vec3 forward() const {
        return frame * glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw));
    }
    glm::vec3 right() const { return glm::normalize(glm::cross(forward(), up())); }

    glm::mat4 view() const {
        const glm::mat4 look = glm::lookAt(position, position + forward(), up());
        return roll == 0.0f ? look : glm::rotate(glm::mat4(1.0f), -roll, glm::vec3(0.0f, 0.0f, 1.0f)) * look;
    }
    glm::mat4 projection(float aspect) const { return glm::perspective(fovY, aspect, nearZ, farZ); }

    void lookAt(const glm::vec3& target) {
        const glm::vec3 dir = glm::normalize(glm::inverse(frame) * (target - position));
        pitch = std::asin(dir.y);
        yaw = std::atan2(dir.x, -dir.z);
    }
};

}
