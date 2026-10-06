#include "game/render/materials.h"

#include "engine/assets/asset_path.h"
#include "engine/render/shader.h"
#include "engine/render/texture.h"

#include <glad/glad.h>

#include <string>

namespace ghost::game {
TexturedMaterial loadMaterial(const char* name, float scale, const glm::vec3& tint, float roughScale, float normalStrength) {
    const std::string dir = std::string("textures/materials/") + name + "/";
    TexturedMaterial m;
    m.color = engine::loadTexture(engine::assetPath(dir + "color.png"), true);
    m.normal = engine::loadTexture(engine::assetPath(dir + "normal.png"), false);
    m.rough = engine::loadTexture(engine::assetPath(dir + "rough.png"), false);
    m.scale = scale;
    m.tint = tint;
    m.roughScale = roughScale;
    m.normalStrength = normalStrength;
    if (m.color) {
        GLint w = 0;
        GLint levels = 0;
        glGetTextureParameteriv(m.color.id(), GL_TEXTURE_IMMUTABLE_LEVELS, &levels);
        glGetTextureLevelParameteriv(m.color.id(), levels - 1, GL_TEXTURE_WIDTH, &w);
        if (w == 1) {
            float rgba[4] = {};
            glGetTextureImage(m.color.id(), levels - 1, GL_RGBA, GL_FLOAT, sizeof(rgba), rgba);
            m.average = glm::pow(glm::vec3(rgba[0], rgba[1], rgba[2]), glm::vec3(2.2f));
        }
    }
    return m;
}

void bindMaterial(const engine::Shader& shader, const TexturedMaterial* material, const glm::mat4& model,
                  const glm::mat4& mapSpace, const glm::vec3& tint) {
    const bool maps = material && material->color && material->normal && material->rough;
    shader.set("uUseMaps", maps ? 1 : 0);
    if (!maps) {
        return;
    }
    glBindTextureUnit(3, material->color.id());
    glBindTextureUnit(4, material->normal.id());
    glBindTextureUnit(5, material->rough.id());
    shader.set("uColorMap", 3);
    shader.set("uNormalMap", 4);
    shader.set("uRoughMap", 5);
    shader.set("uMapScale", material->scale);
    shader.set("uMapTint", material->tint * tint);
    shader.set("uMapRoughScale", material->roughScale);
    shader.set("uMapSpace", mapSpace);
    shader.set("uMapNormalMatrix", glm::transpose(glm::inverse(glm::mat3(model * glm::inverse(mapSpace)))));
    shader.set("uNormalStrength", material->normalStrength);
    shader.set("uMapContrast", material->contrast);
    shader.set("uBlendSharpness", material->blendSharpness);
    shader.set("uSoot", material->soot);
    shader.set("uSootFrom", material->sootFrom);
    shader.set("uSootTo", material->sootTo);
}

MaterialLibrary::MaterialLibrary()
    : concrete(loadMaterial("concrete", 0.6f, glm::vec3(1.0f), 1.0f, 1.0f)),
      floor(loadMaterial("floor", 0.35f, glm::vec3(1.0f), 1.0f, 1.0f)),
      planks(loadMaterial("planks", 0.9f, glm::vec3(1.0f), 1.0f, 1.5f)),
      paint(loadMaterial("paint", 2.0f, glm::vec3(1.0f), 0.8f, 1.0f)),
      stone(loadMaterial("stone", 6.0f, glm::vec3(1.0f), 1.0f, 1.5f)) {
    concrete.blendSharpness = 6.0f;
    floor.blendSharpness = 8.0f;
    paint.contrast = 0.6f;
}

}
