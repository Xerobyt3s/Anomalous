#include "engine/assets/embedded_assets.h"

#include <windows.h>

#include <cstring>
#include <vector>

namespace ghost::engine::embedded {

namespace {

constexpr std::uint32_t kPackMagic = 0x4B504E41u;

const std::vector<File>& loadPack() {
    static const std::vector<File> files = [] {
        std::vector<File> out;
        HRSRC found = FindResourceW(nullptr, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(10));
        if (!found) {
            return out;
        }
        HGLOBAL loaded = LoadResource(nullptr, found);
        const auto* base = static_cast<const unsigned char*>(loaded ? LockResource(loaded) : nullptr);
        const DWORD size = SizeofResource(nullptr, found);
        if (!base || size < 8) {
            return out;
        }
        std::uint32_t magic = 0;
        std::uint32_t count = 0;
        std::memcpy(&magic, base, 4);
        std::memcpy(&count, base + 4, 4);
        if (magic != kPackMagic) {
            return out;
        }
        out.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint64_t entry[3];
            std::memcpy(entry, base + 8 + static_cast<std::size_t>(i) * sizeof(entry), sizeof(entry));
            if (entry[1] + entry[2] > size) {
                continue;
            }
            out.push_back({entry[0], base + entry[1], static_cast<std::size_t>(entry[2])});
        }
        return out;
    }();
    return files;
}

}

const File* const kFiles = loadPack().data();
const std::size_t kFileCount = loadPack().size();

}
