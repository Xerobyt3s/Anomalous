#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ghost::engine {
inline constexpr int kAudioSampleRate = 48000;

struct SoundBuffer {
    std::vector<float> samples;
    std::vector<float> right;

    bool stereo() const { return !right.empty(); }
    float seconds() const { return static_cast<float>(samples.size()) / static_cast<float>(kAudioSampleRate); }
};
using SoundRef = std::shared_ptr<const SoundBuffer>;

std::optional<SoundBuffer> loadSoundFile(const std::filesystem::path& path);

std::optional<SoundBuffer> decodeSound(std::string_view bytes);

class SoundBank {
public:
    void add(std::string_view name, SoundBuffer buffer);
    void clear(std::string_view name);

    bool has(std::string_view name) const;
    std::span<const SoundRef> variations(std::string_view name) const;

    SoundRef pick(std::string_view name, std::minstd_rand& rng) const;
    std::vector<std::string> names() const;

    int loadOverrides(const std::filesystem::path& directory);

    int loadAssets(const std::filesystem::path& directory);

private:
    using Loader = std::optional<SoundBuffer> (*)(const std::filesystem::path&);
    int replaceFrom(const std::filesystem::path& directory, Loader load);

    std::map<std::string, std::vector<SoundRef>, std::less<>> m_sounds;
};

}
