#include "platform/filesystem.h"
#include "core/arena.h"
#include "core/log.h"

#include <cstring>
#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <wchar.h>

namespace anom::fs {
namespace {

constexpr u32 kWidePathMax = 640;

struct WidePath {
    wchar_t buf[kWidePathMax];
    bool ok = false;

    const wchar_t* get() const { return ok ? buf : nullptr; }
};

WidePath to_wide(std::string_view path)
{
    WidePath result;
    result.ok = utf8_to_wide(path, result.buf) > 0 || path.empty();
    return result;
}

} // namespace

u32 utf8_to_wide(std::string_view utf8, std::span<wchar_t> out)
{
    if (out.empty()) {
        return 0;
    }
    u32 n = 0;
    std::size_t i = 0;
    const auto* p = reinterpret_cast<const u8*>(utf8.data());
    const std::size_t len = utf8.size();

    while (i < len && n + 2 < out.size()) {
        u32 cp = 0;
        const u8 c0 = p[i];
        if (c0 < 0x80) {
            cp = c0;
            i += 1;
        } else if ((c0 & 0xE0) == 0xC0 && i + 1 < len && (p[i + 1] & 0xC0) == 0x80) {
            cp = (static_cast<u32>(c0 & 0x1F) << 6) | static_cast<u32>(p[i + 1] & 0x3F);
            i += 2;
        } else if ((c0 & 0xF0) == 0xE0 && i + 2 < len && (p[i + 1] & 0xC0) == 0x80
                   && (p[i + 2] & 0xC0) == 0x80) {
            cp = (static_cast<u32>(c0 & 0x0F) << 12)
               | (static_cast<u32>(p[i + 1] & 0x3F) << 6)
               | static_cast<u32>(p[i + 2] & 0x3F);
            i += 3;
        } else if ((c0 & 0xF8) == 0xF0 && i + 3 < len && (p[i + 1] & 0xC0) == 0x80
                   && (p[i + 2] & 0xC0) == 0x80 && (p[i + 3] & 0xC0) == 0x80) {
            cp = (static_cast<u32>(c0 & 0x07) << 18)
               | (static_cast<u32>(p[i + 1] & 0x3F) << 12)
               | (static_cast<u32>(p[i + 2] & 0x3F) << 6)
               | static_cast<u32>(p[i + 3] & 0x3F);
            i += 4;
        } else {
            cp = 0xFFFD;
            i += 1;
        }

        if (cp >= 0x10000) {
            cp -= 0x10000;
            out[n++] = static_cast<wchar_t>(0xD800 + (cp >> 10));
            out[n++] = static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
        } else {
            out[n++] = static_cast<wchar_t>(cp);
        }
    }
    out[n] = 0;
    return n;
}

u32 wide_to_utf8(const wchar_t* wide, std::span<char> out)
{
    if (out.empty()) {
        return 0;
    }
    u32 n = 0;
    for (std::size_t i = 0; wide[i]; i++) {
        u32 cp = static_cast<u32>(wide[i]);
        if (cp >= 0xD800 && cp < 0xDC00 && wide[i + 1] >= 0xDC00 && wide[i + 1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<u32>(wide[i + 1]) - 0xDC00);
            i++;
        }
        if (cp < 0x80) {
            if (n + 1 >= out.size()) { break; }
            out[n++] = static_cast<char>(cp);
        } else if (cp < 0x800) {
            if (n + 2 >= out.size()) { break; }
            out[n++] = static_cast<char>(0xC0 | (cp >> 6));
            out[n++] = static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            if (n + 3 >= out.size()) { break; }
            out[n++] = static_cast<char>(0xE0 | (cp >> 12));
            out[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out[n++] = static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            if (n + 4 >= out.size()) { break; }
            out[n++] = static_cast<char>(0xF0 | (cp >> 18));
            out[n++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out[n++] = static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    out[n] = 0;
    return n;
}

std::FILE* open(std::string_view path, const char* mode)
{
    const WidePath wpath = to_wide(path);
    if (!wpath.get()) {
        return nullptr;
    }
    wchar_t wmode[16];
    u32 m = 0;
    for (; mode[m] && m < 15; m++) {
        wmode[m] = static_cast<wchar_t>(mode[m]);
    }
    wmode[m] = 0;

    std::FILE* file = nullptr;
    if (_wfopen_s(&file, wpath.get(), wmode) != 0) {
        return nullptr;
    }
    return file;
}

FileData read_entire_file(Arena& arena, std::string_view path)
{
    FileData result;
    std::FILE* file = open(path, "rb");
    if (!file) {
        log_warn("file: could not open %.*s", static_cast<int>(path.size()), path.data());
        return result;
    }
    std::fseek(file, 0, SEEK_END);
    const i64 size = _ftelli64(file);
    std::fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(file);
        return result;
    }
    u8* data = arena.push_array<u8>(static_cast<u64>(size) + 1);
    if (!data) {
        std::fclose(file);
        return result;
    }
    const std::size_t bytes_read = std::fread(data, 1, static_cast<std::size_t>(size), file);
    std::fclose(file);
    if (bytes_read != static_cast<std::size_t>(size)) {
        log_warn("file: short read on %.*s", static_cast<int>(path.size()), path.data());
        return result;
    }
    data[size] = 0;
    result.data = data;
    result.size = static_cast<u64>(size);
    return result;
}

bool exists(std::string_view path)
{
    const WidePath wpath = to_wide(path);
    if (!wpath.get()) {
        return false;
    }
    struct _stat64 info;
    return _wstat64(wpath.get(), &info) == 0;
}

i64 file_mtime(std::string_view path)
{
    const WidePath wpath = to_wide(path);
    if (!wpath.get()) {
        return 0;
    }
    struct _stat64 info;
    if (_wstat64(wpath.get(), &info) != 0) {
        return 0;
    }
    return static_cast<i64>(info.st_mtime);
}

u32 list_dir(std::string_view path, std::span<DirEntry> out)
{
    if (out.empty()) {
        return 0;
    }
    FixedString<512> pattern;
    pattern.format("%.*s/*", static_cast<int>(path.size()), path.data());

    const WidePath wpattern = to_wide(pattern.view());
    if (!wpattern.get()) {
        return 0;
    }

    struct _wfinddata64_t fd;
    const intptr_t handle = _wfindfirst64(wpattern.get(), &fd);
    if (handle == -1) {
        return 0;
    }
    u32 n = 0;
    do {
        if (wcscmp(fd.name, L".") == 0 || wcscmp(fd.name, L"..") == 0) {
            continue;
        }
        if (n >= out.size()) {
            break;
        }
        char name[256];
        wide_to_utf8(fd.name, name);
        out[n].name.assign(name);
        out[n].is_dir = (fd.attrib & _A_SUBDIR) != 0;
        n++;
    } while (_wfindnext64(handle, &fd) == 0);
    _findclose(handle);
    return n;
}

} // namespace anom::fs
