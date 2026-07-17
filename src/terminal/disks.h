#pragma once

#include "core/types.h"

struct Fs;
struct FsDrive;

#define DISK_COUNT 5
#define DISK_MASTER 0
#define DISK_FIELD_NOTES 1
#define DISK_SCRATCH 2
#define DISK_RESCUE 3
#define DISK_ARCADE 4

#define PHOTO_SLOTS 32
#define PHOTO_W 320
#define PHOTO_H 200
#define PHOTO_PIXELS (PHOTO_W * PHOTO_H)
#define PHOTO_BYTES (PHOTO_PIXELS * 3)
#define PHOTO_FILE_BYTES 32000

void disks_init(struct Fs* fs);
const char* disk_label(i32 disk);
void disk_load(i32 disk, struct FsDrive* dst);
void disk_store(i32 disk, const struct FsDrive* src);
void disks_camera_load(struct FsDrive* dst);
void disks_camera_store(const struct FsDrive* src);
b32  disks_camera_capture(struct Fs* live_fs, const u8* rgb);
u32  disks_camera_exposures_left(const struct Fs* live_fs);
const u8* disk_photo_data(i32 pic);
