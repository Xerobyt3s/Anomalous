#include "engine/audio/sound_bank.h"

#include "engine/assets/asset_path.h"
#include "engine/debug/log.h"

#include <array>

namespace ghost::engine {
void SoundBank::add(std::string_view name, SoundBuffer buffer) {
    auto it = m_sounds.find(name);
    if (it == m_sounds.end()) {
        it = m_sounds.emplace(std::string(name), std::vector<SoundRef>{}).first;
    }
    it->second.push_back(std::make_shared<const SoundBuffer>(std::move(buffer)));
}

void SoundBank::clear(std::string_view name) {
    if (const auto it = m_sounds.find(name); it != m_sounds.end()) {
        it->second.clear();
    }
}

bool SoundBank::has(std::string_view name) const {
    const auto it = m_sounds.find(name);
    return it != m_sounds.end() && !it->second.empty();
}

std::span<const SoundRef> SoundBank::variations(std::string_view name) const {
    const auto it = m_sounds.find(name);
    return it == m_sounds.end() ? std::span<const SoundRef>{} : std::span<const SoundRef>(it->second);
}

SoundRef SoundBank::pick(std::string_view name, std::minstd_rand& rng) const {
    const std::span<const SoundRef> all = variations(name);
    if (all.empty()) {
        return nullptr;
    }
    return all[std::uniform_int_distribution<std::size_t>(0, all.size() - 1)(rng)];
}

std::vector<std::string> SoundBank::names() const {
    std::vector<std::string> out;
    for (const auto& [name, sounds] : m_sounds) {
        out.push_back(name);
    }
    return out;
}

int SoundBank::loadOverrides(const std::filesystem::path& directory) {
    std::error_code ignored;
    if (!std::filesystem::is_directory(directory, ignored)) {
        return 0;
    }
    return replaceFrom(directory, [](const std::filesystem::path& path) -> std::optional<SoundBuffer> {
        std::error_code none;
        if (!std::filesystem::exists(path, none)) {
            return std::nullopt;
        }
        auto sound = loadSoundFile(path);
        if (!sound) {
            GHOST_WARN("Could not read sound file {}", path.string());
        }
        return sound;
    });
}

int SoundBank::loadAssets(const std::filesystem::path& directory) {
    return replaceFrom(directory, [](const std::filesystem::path& path) -> std::optional<SoundBuffer> {
        const auto bytes = readAsset(path);
        if (!bytes) {
            return std::nullopt;
        }
        auto sound = decodeSound(*bytes);
        if (!sound) {
            GHOST_WARN("Could not read sound asset {}", path.generic_string());
        }
        return sound;
    });
}

int SoundBank::replaceFrom(const std::filesystem::path& directory, Loader load) {
    constexpr std::array<const char*, 3> kExtensions{".wav", ".flac", ".mp3"};
    int replaced = 0;
    for (const std::string& name : names()) {
        std::vector<SoundBuffer> found;
        auto tryLoad = [&](const std::string& stem) {
            for (const char* extension : kExtensions) {
                if (auto sound = load(directory / (stem + extension))) {
                    found.push_back(std::move(*sound));
                    return true;
                }
            }
            return false;
        };
        tryLoad(name);
        for (int k = 1; k <= 16 && tryLoad(name + "." + std::to_string(k)); ++k) {
        }
        if (found.empty()) {
            continue;
        }
        clear(name);
        for (SoundBuffer& sound : found) {
            add(name, std::move(sound));
        }
        ++replaced;
    }
    return replaced;
}

}
