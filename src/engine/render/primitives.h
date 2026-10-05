#pragma once

#include "engine/render/mesh.h"

#include <glm/glm.hpp>

namespace ghost::engine {
Mesh makeBox(const glm::vec3& halfExtents);

Mesh makeCylinder(int segments = 24);

Mesh makeQuad();

Mesh makeRing(float innerRadius, float outerRadius, int segments = 32);

Mesh makePlane(float height, float halfSize);

}
