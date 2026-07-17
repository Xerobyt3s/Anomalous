#pragma once

#include "core/types.h"

#define FS_DRIVE_COUNT 3
#define FS_DRIVE_A 0
#define FS_DRIVE_B 1
#define FS_DRIVE_C 2
#define FS_NAME_MAX 12
#define FS_DRIVE_NODES 96
#define FS_POOL_SIZE 24576
#define FS_PATH_MAX 64
#define FS_LABEL_MAX 15

typedef enum FsExe {
    FS_EXE_NONE,
    FS_EXE_STATUS,
    FS_EXE_MAP,
    FS_EXE_LINK,
    FS_EXE_COMMS,
    FS_EXE_AV,
    FS_EXE_TOY,
    FS_EXE_VIDEO,
} FsExe;

typedef enum FsError {
    FS_OK,
    FS_ERR_NOT_FOUND,
    FS_ERR_NOT_READY,
    FS_ERR_IS_DIR,
    FS_ERR_BAD_NAME,
    FS_ERR_FULL,
    FS_ERR_NO_SPACE,
    FS_ERR_SELF,
} FsError;

typedef struct FsNode {
    char name[FS_NAME_MAX + 1];
    b32 used;
    b32 is_dir;
    i32 parent;
    u32 size;
    i32 exe;
    i32 pic;
    b32 infected;
    b32 corrupted;
    const char* run_text;
    const char* rom_text;
    u32 pool_off;
    u32 pool_len;
} FsNode;

typedef struct FsDrive {
    b32 mounted;
    char label[FS_LABEL_MAX + 1];
    u32 capacity;
    FsNode nodes[FS_DRIVE_NODES];
    char pool[FS_POOL_SIZE];
    u32 pool_used;
} FsDrive;

typedef struct Fs {
    FsDrive drives[FS_DRIVE_COUNT];
} Fs;

typedef struct FsRef {
    i32 drive;
    i32 node;
} FsRef;

void fs_init(Fs* fs);
b32  fs_drive_mounted(const Fs* fs, i32 drive);
u32  fs_used_bytes(const Fs* fs, i32 drive);
u32  fs_free_bytes(const Fs* fs, i32 drive);
FsRef fs_root(i32 drive);
b32  fs_ref_valid(const Fs* fs, FsRef ref);
const FsNode* fs_node(const Fs* fs, FsRef ref);
FsNode* fs_node_mut(Fs* fs, FsRef ref);
FsRef fs_resolve(const Fs* fs, FsRef cwd, const char* path);
i32  fs_path_drive(const char* path);
void fs_path_string(const Fs* fs, FsRef ref, char* buf, u32 size);
const char* fs_text(const Fs* fs, FsRef ref);
b32  fs_name_valid(const char* name);
FsRef fs_mkdir(Fs* fs, FsRef dir, const char* name);
FsRef fs_mkfile_rom(Fs* fs, FsRef dir, const char* name, const char* text, u32 size, i32 exe);
FsRef fs_mkfile_data(Fs* fs, FsRef dir, const char* name, const char* text, u32 len);
FsError fs_copy(Fs* fs, FsRef src, FsRef dst_dir, const char* dst_name);
FsError fs_delete(Fs* fs, FsRef ref);
void fs_format(Fs* fs, i32 drive, const char* label);
void fs_mount(Fs* fs, i32 drive, const char* label, u32 capacity);
void fs_unmount(Fs* fs, i32 drive);
