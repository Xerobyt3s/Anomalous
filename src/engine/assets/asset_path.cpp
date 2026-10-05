#include "engine/assets/asset_path.h"

#include <fstream>
#include <sstream>

namespace ghost::engine {

bool assetsEmbedded() { return false; }

std::filesystem::path assetPath(std::string_view relative) {
    return std::filesystem::path(GHOST_ASSET_DIR) / relative;
}

std::filesystem::path looseAssetPath(std::string_view name) {
    return std::filesystem::path(GHOST_ASSET_DIR) / name;
}

std::optional<std::string> readAsset(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

} // namespace ghost::engine
