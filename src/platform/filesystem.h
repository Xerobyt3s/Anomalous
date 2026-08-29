#pragma once

#include "core/fixed_string.h"
#include "core/types.h"

#include <cstdio>
#include <span>
#include <string_view>

namespace anom {

class Arena;

namespace fs {

struct FileData {
    u8* data = nullptr;
    u64 size = 0;

    bool valid() const { return data != nullptr; }
    std::string_view text() const
    {
        return std::string_view(reinterpret_cast<const char*>(data), size);
    }
};

struct DirEntry {
    FixedString<256> name;
    bool is_dir = false;
};

FileData read_entire_file(Arena& arena, std::string_view path);
bool exists(std::string_view path);
bool make_dir(std::string_view path);
i64 file_mtime(std::string_view path);
u32 list_dir(std::string_view path, std::span<DirEntry> out);
std::FILE* open(std::string_view path, const char* mode);

u32 utf8_to_wide(std::string_view utf8, std::span<wchar_t> out);
u32 wide_to_utf8(const wchar_t* wide, std::span<char> out);

} // namespace fs
} // namespace anom
