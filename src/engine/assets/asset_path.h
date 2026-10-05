#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ghost::engine {
std::filesystem::path assetPath(std::string_view relative);

std::optional<std::string> readAsset(const std::filesystem::path& path);

bool assetsEmbedded();

std::filesystem::path looseAssetPath(std::string_view name);

}
