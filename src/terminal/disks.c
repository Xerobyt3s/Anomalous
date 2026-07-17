#include "terminal/disks.h"
#include "terminal/fs.h"
#include "core/arena.h"
#include "core/config.h"
#include "core/log.h"
#include "platform/platform.h"

#include <stdio.h>
#include <string.h>

#define DISK_DIR "assets/disks"
#define DISK_DEFAULT_CAPACITY 368640
#define DISK_DIR_ENTRIES 32

static const char* s_floppy_dirs[DISK_COUNT] = {
    "master",
    "fieldnotes",
    "scratch",
    "rescue",
    "arcade",
};

static FsDrive s_store[DISK_COUNT];
static Fs s_build;

static i32 exe_prog(const char* name)
{
    if (strcmp(name, "status") == 0) {
        return FS_EXE_STATUS;
    }
    if (strcmp(name, "map") == 0) {
        return FS_EXE_MAP;
    }
    if (strcmp(name, "link") == 0) {
        return FS_EXE_LINK;
    }
    if (strcmp(name, "comms") == 0) {
        return FS_EXE_COMMS;
    }
    if (strcmp(name, "av") == 0) {
        return FS_EXE_AV;
    }
    if (strcmp(name, "toy") == 0) {
        return FS_EXE_TOY;
    }
    return FS_EXE_NONE;
}

static void strip_cr(char* text)
{
    char* dst = text;
    for (char* src = text; *src; src++) {
        if (*src != '\r') {
            *dst++ = *src;
        }
    }
    *dst = 0;
}

static const char* perm_strdup(const char* text)
{
    u64 len = strlen(text);
    char* copy = arena_push_array(&g_perm_arena, char, len + 1);
    memcpy(copy, text, len + 1);
    return copy;
}

static b32 name_to_fs(const char* in, char* out, u32 out_size)
{
    u32 n = 0;
    for (const char* p = in; *p && n + 1 < out_size; p++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        out[n++] = c;
    }
    out[n] = 0;
    return fs_name_valid(out);
}

static void load_exe_stub(Fs* fs, FsRef dir, const char* fsname, char* text)
{
    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    Config cfg;
    if (!config_parse(&cfg, &g_frame_arena, text)) {
        log_warn("disks: bad exe stub %s", fsname);
        arena_temp_end(temp);
        return;
    }
    i32 prog = exe_prog(config_get_str(&cfg, "prog", "none"));
    u32 size = (u32)config_get_i32(&cfg, "size", 16384);
    b32 infected = config_get_i32(&cfg, "infected", 0) != 0;
    const char* run_text = config_get_str(&cfg, "text", 0);
    const char* run_copy = run_text ? perm_strdup(run_text) : 0;
    arena_temp_end(temp);

    FsRef file = fs_mkfile_rom(fs, dir, fsname, 0, size, prog);
    if (!fs_ref_valid(fs, file)) {
        log_warn("disks: could not add %s", fsname);
        return;
    }
    FsNode* node = fs_node_mut(fs, file);
    node->infected = infected;
    node->run_text = run_copy;
}

static void load_dir_tree(Fs* fs, FsRef dir, const char* path)
{
    PlatformDirEntry entries[DISK_DIR_ENTRIES];
    u32 count = platform_list_dir(path, entries, DISK_DIR_ENTRIES);
    for (u32 i = 0; i < count; i++) {
        char child_path[512];
        snprintf(child_path, sizeof(child_path), "%s/%s", path, entries[i].name);
        char fsname[FS_NAME_MAX + 1];
        if (!name_to_fs(entries[i].name, fsname, sizeof(fsname))) {
            log_warn("disks: skipping %s (bad 8.3 name)", child_path);
            continue;
        }
        if (entries[i].is_dir) {
            FsRef sub = fs_mkdir(fs, dir, fsname);
            if (fs_ref_valid(fs, sub)) {
                load_dir_tree(fs, sub, child_path);
            }
            continue;
        }
        if (strcmp(fsname, "DISK.CFG") == 0) {
            continue;
        }
        FileData data = platform_read_entire_file(&g_perm_arena, child_path);
        if (!data.data) {
            continue;
        }
        strip_cr((char*)data.data);
        u32 len = (u32)strlen((const char*)data.data);
        u32 name_len = (u32)strlen(fsname);
        if (name_len > 4 && strcmp(fsname + name_len - 4, ".EXE") == 0) {
            load_exe_stub(fs, dir, fsname, (char*)data.data);
        } else {
            fs_mkfile_rom(fs, dir, fsname, (const char*)data.data, len, FS_EXE_NONE);
        }
    }
}

static void load_disk_cfg(const char* path, char* label, u32 label_size, u32* capacity)
{
    char cfg_path[512];
    snprintf(cfg_path, sizeof(cfg_path), "%s/disk.cfg", path);
    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    FileData data = platform_read_entire_file(&g_frame_arena, cfg_path);
    if (data.data) {
        Config cfg;
        if (config_parse(&cfg, &g_frame_arena, (const char*)data.data)) {
            snprintf(label, label_size, "%s", config_get_str(&cfg, "label", "NO LABEL"));
            *capacity = (u32)config_get_i32(&cfg, "capacity", DISK_DEFAULT_CAPACITY);
        }
    } else {
        log_warn("disks: missing %s", cfg_path);
    }
    arena_temp_end(temp);
}

void disks_init(Fs* fs)
{
    char label[FS_LABEL_MAX + 1];
    u32 capacity;

    snprintf(label, sizeof(label), "DR-OS SYSTEM");
    capacity = 362496;
    load_disk_cfg(DISK_DIR "/internal", label, sizeof(label), &capacity);
    fs_mount(fs, FS_DRIVE_A, label, capacity);
    load_dir_tree(fs, fs_root(FS_DRIVE_A), DISK_DIR "/internal");

    memset(s_store, 0, sizeof(s_store));
    for (i32 i = 0; i < DISK_COUNT; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", DISK_DIR, s_floppy_dirs[i]);
        snprintf(label, sizeof(label), "NO LABEL");
        capacity = DISK_DEFAULT_CAPACITY;
        load_disk_cfg(path, label, sizeof(label), &capacity);
        fs_mount(&s_build, FS_DRIVE_B, label, capacity);
        load_dir_tree(&s_build, fs_root(FS_DRIVE_B), path);
        memcpy(&s_store[i], &s_build.drives[FS_DRIVE_B], sizeof(FsDrive));
    }
    fs_unmount(&s_build, FS_DRIVE_B);
}

const char* disk_label(i32 disk)
{
    if (disk < 0 || disk >= DISK_COUNT) {
        return "UNKNOWN MEDIA";
    }
    return s_store[disk].label;
}

void disk_load(i32 disk, FsDrive* dst)
{
    memcpy(dst, &s_store[disk], sizeof(FsDrive));
    dst->mounted = 1;
}

void disk_store(i32 disk, const FsDrive* src)
{
    memcpy(&s_store[disk], src, sizeof(FsDrive));
}
