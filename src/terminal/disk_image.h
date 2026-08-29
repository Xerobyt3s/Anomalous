#pragma once

#include "core/types.h"
#include "terminal/fs.h"

#include <span>
#include <string_view>

namespace anom {

struct RomFile {
    std::string_view name;
    std::string_view text;
    std::string_view run_text;
    FsExe exe = FsExe::None;
    u32 size = 0;
    bool infected = false;
};

struct RomDir {
    std::string_view name;
    std::span<const RomFile> files;
};

struct DiskImage {
    std::string_view id;
    std::string_view label;
    u32 capacity = 0;
    std::span<const RomFile> files;
    std::span<const RomDir> dirs;
};

std::span<const DiskImage> disk_images();
const DiskImage* disk_image(std::string_view id);

void fs_mount_image(Fs& fs, i32 drive, const DiskImage& image);

} // namespace anom
