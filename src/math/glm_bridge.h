#pragma once

#include "math/vmath.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace anom {

inline glm::vec2 to_glm(Vec2 v) { return glm::vec2{v.x, v.y}; }
inline glm::vec3 to_glm(Vec3 v) { return glm::vec3{v.x, v.y, v.z}; }
inline glm::vec4 to_glm(Vec4 v) { return glm::vec4{v.x, v.y, v.z, v.w}; }
inline glm::quat to_glm(Quat q) { return glm::quat{q.w, q.x, q.y, q.z}; }

inline glm::mat4 to_glm(const Mat4& m)
{
    glm::mat4 out;
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            out[c][r] = m.m[c * 4 + r];
        }
    }
    return out;
}

inline Vec2 from_glm(glm::vec2 v) { return Vec2{v.x, v.y}; }
inline Vec3 from_glm(glm::vec3 v) { return Vec3{v.x, v.y, v.z}; }
inline Vec4 from_glm(glm::vec4 v) { return Vec4{v.x, v.y, v.z, v.w}; }
inline Quat from_glm(glm::quat q) { return Quat{q.x, q.y, q.z, q.w}; }

inline Mat4 from_glm(const glm::mat4& m)
{
    Mat4 out;
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            out.m[c * 4 + r] = m[c][r];
        }
    }
    return out;
}

} // namespace anom
