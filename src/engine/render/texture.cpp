#include "engine/render/texture.h"

#include "engine/assets/asset_path.h"
#include "engine/debug/log.h"

#include <glad/glad.h>

#pragma warning(push, 0)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#pragma warning(pop)

#include <algorithm>
#include <cmath>

namespace ghost::engine {
GlTexture loadTexture(const std::filesystem::path& path, bool srgb) {
    const auto bytes = readAsset(path);
    if (!bytes) {
        GHOST_ERROR("Missing texture: {}", path.string());
        return {};
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(1);
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes->data()), static_cast<int>(bytes->size()),
                                            &width, &height, &channels, 4);
    if (!pixels) {
        GHOST_ERROR("Unreadable texture {}: {}", path.string(), stbi_failure_reason());
        return {};
    }
    GLuint id = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &id);
    const int levels = 1 + static_cast<int>(std::floor(std::log2(static_cast<float>(std::max(width, height)))));
    glTextureStorage2D(id, levels, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, width, height);
    glTextureSubImage2D(id, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    stbi_image_free(pixels);
    glGenerateTextureMipmap(id);
    glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(id, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(id, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameterf(id, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
    return GlTexture(id);
}

}
