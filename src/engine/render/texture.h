#pragma once

#include "engine/render/gl_handle.h"

#include <filesystem>

namespace ghost::engine {
GlTexture loadTexture(const std::filesystem::path& path, bool srgb);

}
