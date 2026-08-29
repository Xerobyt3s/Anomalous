#include "terminal/disks.h"
#include "core/arena.h"
#include "core/log.h"
#include "math/vmath.h"
#include "terminal/disk_image.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace anom {
namespace {

constexpr const char* kFloppyIds[DISK_COUNT] = {
    "master", "fieldnotes", "scratch", "rescue", "arcade",
};

constexpr std::string_view kTowerLog =
    "RELAY TOWER R-4 -- SERVICE NODE\n"
    "REDLINE INFRASTRUCTURE, FIRMWARE 1.07\n"
    "\n"
    "UNAUTHORIZED ACCESS IS LOGGED AND -- WELL. IT USED\n"
    "TO BE. NO ONE READS THESE LOGS NOW.\n"
    "\n"
    "THIS NODE CARRIES A CACHED WIDE-AREA SURVEY. RUN\n"
    "MAP WHILE LINKED TO PULL IT DOWN TO YOUR UNIT.\n"
    "\n"
    "GATE.EXE ACTUATES THE ACCESS ROAD BARRIER. NO\n"
    "BARRIER IS WIRED TO THIS NODE. IT WILL SAY SO.\n";

u8 photo_luma(const u8* p)
{
    return static_cast<u8>((static_cast<u32>(p[0]) * 54 + static_cast<u32>(p[1]) * 183
                            + static_cast<u32>(p[2]) * 19)
                           >> 8);
}

void photo_tone_map(u8* p)
{
    u32 hist[256] = {};
    for (u32 i = 0; i < kPhotoPixels; i++) {
        hist[photo_luma(&p[i * 3])]++;
    }

    const u32 clip = kPhotoPixels / 250;
    u32 acc = 0;
    i32 lo = 0;
    for (i32 v = 0; v < 256; v++) {
        acc += hist[v];
        if (acc > clip) {
            lo = v;
            break;
        }
    }
    acc = 0;
    i32 hi = 255;
    for (i32 v = 255; v >= 0; v--) {
        acc += hist[v];
        if (acc > clip) {
            hi = v;
            break;
        }
    }
    if (hi - lo < 24) {
        lo = lo > 12 ? lo - 12 : 0;
        hi = lo + 24;
    }

    u8 lut[256];
    for (i32 v = 0; v < 256; v++) {
        const f32 t = f_clamp01((static_cast<f32>(v) - static_cast<f32>(lo))
                                / static_cast<f32>(hi - lo));
        lut[v] = static_cast<u8>(235.0f * std::pow(t, 0.95f) + 0.5f);
    }

    for (u32 i = 0; i < kPhotoPixels; i++) {
        u8* px = &p[i * 3];
        const u32 luma = photo_luma(px);
        const u32 scale = (static_cast<u32>(lut[luma]) << 8) / (luma > 0 ? luma : 1);
        for (u32 c = 0; c < 3; c++) {
            const u32 v = (static_cast<u32>(px[c]) * scale) >> 8;
            px[c] = static_cast<u8>(v > 255 ? 255 : v);
        }
    }
}

void mark_photos(const FsDrive& d, bool* marked)
{
    for (i32 i = 1; i < kFsDriveNodes; i++) {
        const FsNode& n = d.nodes[i];
        if (n.used && n.pic > 0 && n.pic <= static_cast<i32>(kPhotoSlots)) {
            marked[n.pic - 1] = true;
        }
    }
}

} // namespace

std::string_view disk_label(i32 disk)
{
    if (disk < 0 || disk >= DISK_COUNT) {
        return "UNKNOWN MEDIA";
    }
    const DiskImage* image = disk_image(kFloppyIds[disk]);
    return image ? image->label : "NO LABEL";
}

bool DiskStore::init(Arena& storage)
{
    photos_ = storage.push_array<u8>(static_cast<u64>(kPhotoSlots) * kPhotoBytes);
    if (!photos_) {
        log_error("disks: no arena space for photo slots");
        return false;
    }

    Fs build;
    for (i32 i = 0; i < DISK_COUNT; i++) {
        const DiskImage* image = disk_image(kFloppyIds[i]);
        if (!image) {
            log_warn("disks: missing image %s", kFloppyIds[i]);
            continue;
        }
        fs_mount_image(build, kFsDriveB, *image);
        floppies_[i] = build.drive(kFsDriveB);
    }
    build.unmount(kFsDriveB);

    camera_.mount(kFsDriveC, "CAMERA FILM", kCameraCapacity);

    build.mount(kFsDriveD, "RELAY R-4", kTowerCapacity);
    const FsRef troot = Fs::root(kFsDriveD);
    build.mkfile_rom(troot, "NODELOG.TXT", kTowerLog, 0, FsExe::None);
    build.mkfile_rom(troot, "GATE.EXE", {}, 6144, FsExe::Gate);
    build.mkfile_rom(troot, "RELAY.DAT", {}, 96256, FsExe::None);
    tower_ = build.drive(kFsDriveD);

    log_info("disks: %u floppies + tower baked in", static_cast<u32>(DISK_COUNT));
    return true;
}

void DiskStore::mount_system(Fs& fs) const
{
    const DiskImage* image = disk_image("internal");
    if (!image) {
        fs.mount(kFsDriveA, "DR-OS SYSTEM", kFsSystemCapacity);
        return;
    }
    fs_mount_image(fs, kFsDriveA, *image);
}

void DiskStore::load(i32 disk, FsDrive& dst) const
{
    dst = floppies_[disk];
    dst.mounted = true;
}

void DiskStore::store(i32 disk, const FsDrive& src)
{
    floppies_[disk] = src;
}

void DiskStore::load_camera(FsDrive& dst) const
{
    dst = camera_.drive(kFsDriveC);
    dst.mounted = true;
}

void DiskStore::store_camera(const FsDrive& src)
{
    camera_.drive(kFsDriveC) = src;
    camera_.drive(kFsDriveC).mounted = true;
}

void DiskStore::load_tower(FsDrive& dst) const
{
    dst = tower_;
    dst.mounted = true;
}

i32 DiskStore::photo_alloc(const Fs* live_fs)
{
    bool marked[kPhotoSlots] = {};
    mark_photos(camera_.drive(kFsDriveC), marked);
    if (live_fs) {
        for (i32 d = 0; d < kFsDriveCount; d++) {
            if (live_fs->drive(d).mounted) {
                mark_photos(live_fs->drive(d), marked);
            }
        }
    }
    for (const FsDrive& d : floppies_) {
        mark_photos(d, marked);
    }

    i32 found = -1;
    for (u32 i = 0; i < kPhotoSlots; i++) {
        if (!marked[i]) {
            photo_used_[i] = false;
            if (found < 0) {
                found = static_cast<i32>(i);
            }
        }
    }
    return found;
}

bool DiskStore::camera_capture(Fs* live_fs, const u8* rgb)
{
    if (!photos_) {
        return false;
    }
    Fs& fs = (live_fs && live_fs->drive(kFsDriveC).mounted) ? *live_fs : camera_;
    const i32 slot = photo_alloc(live_fs);
    if (slot < 0) {
        return false;
    }

    const FsRef dir = Fs::root(kFsDriveC);
    FsRef file{kFsDriveC, -1};
    for (u32 attempt = 0; attempt < 100; attempt++) {
        char name[kFsNameMax + 1];
        std::snprintf(name, sizeof(name), "IMG_%02u.PIC", image_counter_ % 100);
        image_counter_++;

        file = fs.mkfile_rom(dir, name, {}, kPhotoFileBytes, FsExe::None);
        if (fs.valid(file)) {
            break;
        }
        if (fs.free_bytes(kFsDriveC) < kPhotoFileBytes) {
            return false;
        }
    }
    if (!fs.valid(file)) {
        return false;
    }

    fs.node_mut(file)->pic = slot + 1;
    u8* dst = photos_ + static_cast<u64>(slot) * kPhotoBytes;
    std::memcpy(dst, rgb, kPhotoBytes);
    photo_tone_map(dst);
    photo_used_[slot] = true;
    return true;
}

u32 DiskStore::camera_exposures_left(const Fs* live_fs) const
{
    const Fs& fs = (live_fs && live_fs->drive(kFsDriveC).mounted) ? *live_fs : camera_;
    return fs.free_bytes(kFsDriveC) / kPhotoFileBytes;
}

const u8* DiskStore::photo(i32 pic) const
{
    if (!photos_ || pic <= 0 || pic > static_cast<i32>(kPhotoSlots) || !photo_used_[pic - 1]) {
        return nullptr;
    }
    return photos_ + static_cast<u64>(pic - 1) * kPhotoBytes;
}

} // namespace anom
