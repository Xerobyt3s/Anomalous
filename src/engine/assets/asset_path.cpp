#include "engine/assets/asset_path.h"

#include "engine/assets/embedded_assets.h"

#include <fstream>
#include <sstream>

namespace ghost::engine {
namespace {
constexpr const char* kEmbeddedRoot = "embedded";

}

bool assetsEmbedded() { return embedded::kFileCount > 0; }

std::filesystem::path assetPath(std::string_view relative) {
    if (assetsEmbedded()) {
        return std::filesystem::path(kEmbeddedRoot) / relative;
    }
    return std::filesystem::path(GHOST_ASSET_DIR) / relative;
}

std::filesystem::path looseAssetPath(std::string_view name) {
    if (assetsEmbedded()) {
        return std::filesystem::current_path() / name;
    }
    return std::filesystem::path(GHOST_ASSET_DIR) / name;
}

std::optional<std::string> readAsset(const std::filesystem::path& path) {
    if (assetsEmbedded()) {
        const std::string name = path.lexically_normal().lexically_relative(kEmbeddedRoot).generic_string();
        const std::uint64_t key = embedded::nameKey(name);
        for (std::size_t i = 0; i < embedded::kFileCount; ++i) {
            const embedded::File& file = embedded::kFiles[i];
            if (file.key == key) {
                std::string bytes(reinterpret_cast<const char*>(file.data), file.size);
                embedded::scramble(reinterpret_cast<unsigned char*>(bytes.data()), bytes.size(), key);
                return bytes;
            }
        }
        return std::nullopt;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

}
