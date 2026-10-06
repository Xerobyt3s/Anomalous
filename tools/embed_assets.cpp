#include "engine/assets/embedded_assets.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace ghost::engine;

namespace {

constexpr std::uint32_t kPackMagic = 0x4B504E41u;

template <typename T>
void put(std::ofstream& out, T value)
{
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

}

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::fprintf(stderr, "usage: embed_assets <assets directory> <output.pak> <output.rc>\n");
        return 1;
    }
    const fs::path root = argv[1];
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().filename() != ".gitkeep") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    std::vector<std::vector<unsigned char>> blobs;
    std::vector<std::uint64_t> keys;
    for (const fs::path& file : files) {
        std::ifstream in(file, std::ios::binary);
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const std::uint64_t key = embedded::nameKey(fs::relative(file, root).generic_string());
        embedded::scramble(bytes.data(), bytes.size(), key);
        keys.push_back(key);
        blobs.push_back(std::move(bytes));
    }

    std::ofstream out(argv[2], std::ios::binary);
    if (!out) {
        std::fprintf(stderr, "embed_assets: can't write %s\n", argv[2]);
        return 1;
    }
    const std::uint64_t header = sizeof(std::uint32_t) * 2 + files.size() * sizeof(std::uint64_t) * 3;
    put(out, kPackMagic);
    put(out, static_cast<std::uint32_t>(files.size()));
    std::uint64_t offset = header;
    for (std::size_t i = 0; i < blobs.size(); ++i) {
        put(out, keys[i]);
        put(out, offset);
        put(out, static_cast<std::uint64_t>(blobs[i].size()));
        offset += blobs[i].size();
    }
    for (const auto& blob : blobs) {
        out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
    }

    std::ofstream rc(argv[3]);
    std::string pak = fs::absolute(argv[2]).generic_string();
    rc << "1 RCDATA \"" << pak << "\"\n";
    std::printf("embed_assets: %zu files, %llu bytes\n", files.size(), static_cast<unsigned long long>(offset));
    return files.empty() ? 1 : 0;
}
