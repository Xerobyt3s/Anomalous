#pragma once

#include "engine/render/gl_handle.h"

#include <glm/glm.hpp>

namespace ghost::engine {
class Shader;
}

namespace ghost::game {
struct TexturedMaterial {
    engine::GlTexture color;
    engine::GlTexture normal;
    engine::GlTexture rough;
    float scale = 4.0f;
    glm::vec3 tint{1.0f};
    float roughScale = 1.0f;
    float normalStrength = 1.0f;
    float contrast = 1.0f;
    float blendSharpness = 4.0f;

    float soot = 0.0f;
    glm::vec3 sootFrom{0.0f};
    glm::vec3 sootTo{0.0f, 0.0f, 1.0f};
    glm::vec3 average{0.5f};

    glm::vec3 tintFor(const glm::vec3& wanted) const { return wanted / glm::max(average, glm::vec3(0.02f)); }
};

TexturedMaterial loadMaterial(const char* name, float scale, const glm::vec3& tint, float roughScale, float normalStrength);

void bindMaterial(const engine::Shader& shader, const TexturedMaterial* material, const glm::mat4& model,
                  const glm::mat4& mapSpace, const glm::vec3& tint = glm::vec3(1.0f));

struct MaterialLibrary {
    MaterialLibrary();
    TexturedMaterial concrete;
    TexturedMaterial floor;
    TexturedMaterial planks;
    TexturedMaterial paint;
    TexturedMaterial stone;
};

}
