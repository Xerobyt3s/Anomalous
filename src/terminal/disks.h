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

void disks_init(struct Fs* fs);
const char* disk_label(i32 disk);
void disk_load(i32 disk, struct FsDrive* dst);
void disk_store(i32 disk, const struct FsDrive* src);
