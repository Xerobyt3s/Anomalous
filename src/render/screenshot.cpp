#include "render/screenshot.h"
#include "core/arena.h"
#include "core/log.h"
#include "platform/filesystem.h"
#include "platform/gl_loader.h"

#include <cstring>

namespace anom {
namespace {

#pragma pack(push, 1)
struct BmpHeader {
    u16 magic;
    u32 file_size;
    u16 reserved0;
    u16 reserved1;
    u32 pixel_offset;
    u32 dib_size;
    i32 width;
    i32 height;
    u16 planes;
    u16 bits_per_pixel;
    u32 compression;
    u32 image_size;
    i32 x_ppm;
    i32 y_ppm;
    u32 palette_colors;
    u32 important_colors;
};
#pragma pack(pop)

static_assert(sizeof(BmpHeader) == 54);

} // namespace

bool capture_frame(Arena& scratch, i32 width, i32 height, std::string_view bmp_path,
                   FrameStats* out_stats)
{
    if (width <= 0 || height <= 0) {
        return false;
    }

    ArenaScope scope(scratch);
    const u32 row_stride = static_cast<u32>(width) * 3;
    const u32 padded_stride = (row_stride + 3) & ~3u;
    const u64 pixel_bytes = static_cast<u64>(padded_stride) * height;

    u8* pixels = scratch.push_array<u8>(pixel_bytes);
    if (!pixels) {
        return false;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    if (out_stats) {
        f64 sum_r = 0.0;
        f64 sum_g = 0.0;
        f64 sum_b = 0.0;
        u64 nonblack = 0;
        bool bucket_seen[512] = {};

        for (i32 y = 0; y < height; y++) {
            const u8* row = pixels + static_cast<u64>(y) * padded_stride;
            for (i32 x = 0; x < width; x++) {
                const u8 r = row[x * 3 + 0];
                const u8 g = row[x * 3 + 1];
                const u8 b = row[x * 3 + 2];
                sum_r += r;
                sum_g += g;
                sum_b += b;
                if (r > 8 || g > 8 || b > 8) {
                    nonblack++;
                }
                bucket_seen[((r >> 5) << 6) | ((g >> 5) << 3) | (b >> 5)] = true;
            }
        }

        const f64 count = static_cast<f64>(width) * height;
        out_stats->mean_r = sum_r / count;
        out_stats->mean_g = sum_g / count;
        out_stats->mean_b = sum_b / count;
        out_stats->nonblack_fraction = static_cast<f32>(static_cast<f64>(nonblack) / count);
        out_stats->distinct_buckets = 0;
        for (bool seen : bucket_seen) {
            if (seen) {
                out_stats->distinct_buckets++;
            }
        }
    }

    if (bmp_path.empty()) {
        return true;
    }

    for (i32 y = 0; y < height; y++) {
        u8* row = pixels + static_cast<u64>(y) * padded_stride;
        for (i32 x = 0; x < width; x++) {
            const u8 r = row[x * 3 + 0];
            row[x * 3 + 0] = row[x * 3 + 2];
            row[x * 3 + 2] = r;
        }
    }

    BmpHeader header{};
    header.magic = 0x4D42;
    header.pixel_offset = sizeof(BmpHeader);
    header.file_size = static_cast<u32>(sizeof(BmpHeader) + pixel_bytes);
    header.dib_size = 40;
    header.width = width;
    header.height = height;
    header.planes = 1;
    header.bits_per_pixel = 24;
    header.image_size = static_cast<u32>(pixel_bytes);

    std::FILE* file = fs::open(bmp_path, "wb");
    if (!file) {
        log_error("screenshot: cannot open %.*s", static_cast<int>(bmp_path.size()),
                  bmp_path.data());
        return false;
    }
    std::fwrite(&header, sizeof(header), 1, file);
    std::fwrite(pixels, 1, pixel_bytes, file);
    std::fclose(file);

    log_info("screenshot: wrote %.*s (%dx%d)", static_cast<int>(bmp_path.size()),
             bmp_path.data(), width, height);
    return true;
}

} // namespace anom
