#pragma once

#include "core/types.h"
#include "terminal/fs.h"

#include <string_view>

namespace anom {

class Arena;

enum DiskId : i32 {
    DISK_MASTER = 0,
    DISK_FIELD_NOTES,
    DISK_SCRATCH,
    DISK_RESCUE,
    DISK_ARCADE,
    DISK_COUNT,
};

inline constexpr u32 kPhotoSlots = 32;
inline constexpr u32 kPhotoWidth = 320;
inline constexpr u32 kPhotoHeight = 200;
inline constexpr u32 kPhotoPixels = kPhotoWidth * kPhotoHeight;
inline constexpr u32 kPhotoBytes = kPhotoPixels * 3;
inline constexpr u32 kPhotoFileBytes = 32000;
inline constexpr u32 kCameraCapacity = 24 * kPhotoFileBytes;
inline constexpr u32 kTowerCapacity = 131072;

std::string_view disk_label(i32 disk);

class DiskStore {
public:
    bool init(Arena& storage);

    void mount_system(Fs& fs) const;

    void load(i32 disk, FsDrive& dst) const;
    void store(i32 disk, const FsDrive& src);

    void load_camera(FsDrive& dst) const;
    void store_camera(const FsDrive& src);
    void load_tower(FsDrive& dst) const;

    bool camera_capture(Fs* live_fs, const u8* rgb);
    u32 camera_exposures_left(const Fs* live_fs) const;
    const u8* photo(i32 pic) const;

private:
    i32 photo_alloc(const Fs* live_fs);

    FsDrive floppies_[DISK_COUNT];
    FsDrive tower_;
    Fs camera_;
    u8* photos_ = nullptr;
    bool photo_used_[kPhotoSlots] = {};
    u32 image_counter_ = 1;
};

} // namespace anom
