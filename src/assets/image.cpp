#include "assets/image.h"
#include "core/arena.h"
#include "core/log.h"
#include "platform/filesystem.h"

#include <cstring>
#include <span>

namespace anom {
namespace {

Arena* g_stb_arena = nullptr;

void* stb_alloc(std::size_t size)
{
    return g_stb_arena ? g_stb_arena->push_bytes(size, 16) : nullptr;
}

void* stb_realloc(void* old_ptr, std::size_t old_size, std::size_t new_size)
{
    void* fresh = stb_alloc(new_size);
    if (fresh && old_ptr && old_size) {
        std::memcpy(fresh, old_ptr, old_size < new_size ? old_size : new_size);
    }
    return fresh;
}

class StbArenaBinding {
public:
    explicit StbArenaBinding(Arena& arena) : previous_(g_stb_arena) { g_stb_arena = &arena; }
    ~StbArenaBinding() { g_stb_arena = previous_; }

    StbArenaBinding(const StbArenaBinding&) = delete;
    StbArenaBinding& operator=(const StbArenaBinding&) = delete;

private:
    Arena* previous_;
};

} // namespace
} // namespace anom

#define STBI_MALLOC(size) ::anom::stb_alloc(size)
#define STBI_REALLOC_SIZED(ptr, old_size, new_size) ::anom::stb_realloc((ptr), (old_size), (new_size))
#define STBI_FREE(ptr) ((void)(ptr))
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ASSERT(x) ASSERT(x)

#pragma warning(push, 0)
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#pragma warning(pop)

namespace anom {
namespace {

Image8 copy_out_8(const u8* pixels, int width, int height, u32 channels, Arena& dst)
{
    Image8 image;
    const u64 count = static_cast<u64>(width) * static_cast<u64>(height) * channels;
    image.pixels = dst.push_array<u8>(count);
    if (!image.pixels) {
        return Image8{};
    }
    std::memcpy(image.pixels, pixels, count);
    image.width = static_cast<u32>(width);
    image.height = static_cast<u32>(height);
    return image;
}

} // namespace

Image8 decode_image_gray(std::span<const u8> bytes, Arena& dst, Arena& scratch)
{
    ArenaScope scope(scratch);
    StbArenaBinding binding(scratch);

    int width = 0;
    int height = 0;
    int channels = 0;
    u8* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                       &width, &height, &channels, 1);
    if (!pixels) {
        return Image8{};
    }
    return copy_out_8(pixels, width, height, 1, dst);
}

Image8 decode_image_rgba(std::span<const u8> bytes, Arena& dst, Arena& scratch)
{
    ArenaScope scope(scratch);
    StbArenaBinding binding(scratch);

    int width = 0;
    int height = 0;
    int channels = 0;
    u8* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                       &width, &height, &channels, 4);
    if (!pixels) {
        return Image8{};
    }
    return copy_out_8(pixels, width, height, 4, dst);
}

Image8 load_image_gray(Arena& dst, Arena& scratch, std::string_view path)
{
    ArenaScope scope(scratch);
    const fs::FileData file = fs::read_entire_file(scratch, path);
    if (!file.valid()) {
        return Image8{};
    }
    const Image8 image = decode_image_gray({file.data, file.size}, dst, scratch);
    if (!image.valid()) {
        log_error("image: failed to decode %.*s", static_cast<int>(path.size()), path.data());
    }
    return image;
}

Image8 load_image_rgba(Arena& dst, Arena& scratch, std::string_view path)
{
    ArenaScope scope(scratch);
    const fs::FileData file = fs::read_entire_file(scratch, path);
    if (!file.valid()) {
        return Image8{};
    }
    const Image8 image = decode_image_rgba({file.data, file.size}, dst, scratch);
    if (!image.valid()) {
        log_error("image: failed to decode %.*s", static_cast<int>(path.size()), path.data());
    }
    return image;
}

Image16 load_image_gray16(Arena& dst, Arena& scratch, std::string_view path)
{
    ArenaScope scope(scratch);
    const fs::FileData file = fs::read_entire_file(scratch, path);
    if (!file.valid()) {
        return Image16{};
    }

    StbArenaBinding binding(scratch);
    int width = 0;
    int height = 0;
    int channels = 0;
    u16* pixels = stbi_load_16_from_memory(file.data, static_cast<int>(file.size),
                                           &width, &height, &channels, 1);
    if (!pixels) {
        log_error("image: failed to decode 16-bit %.*s", static_cast<int>(path.size()),
                  path.data());
        return Image16{};
    }

    Image16 image;
    const u64 count = static_cast<u64>(width) * static_cast<u64>(height);
    image.pixels = dst.push_array<u16>(count);
    if (!image.pixels) {
        return Image16{};
    }
    std::memcpy(image.pixels, pixels, count * sizeof(u16));
    image.width = static_cast<u32>(width);
    image.height = static_cast<u32>(height);
    return image;
}

} // namespace anom
