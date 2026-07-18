#include "terminal/disks.h"
#include "terminal/fs.h"
#include "core/arena.h"
#include "core/config.h"
#include "core/log.h"
#include "platform/platform.h"

#include <math.h>
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

#define CAMERA_CAPACITY (24 * PHOTO_FILE_BYTES)

#define TOWER_CAPACITY 131072

static const char* TOWER_LOG =
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

static FsDrive s_store[DISK_COUNT];
static FsDrive s_tower;
static Fs s_build;
static Fs s_camfs;
static u8 s_photo[PHOTO_SLOTS][PHOTO_BYTES];
static b32 s_photo_used[PHOTO_SLOTS];
static u32 s_img_counter = 1;

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
    if (strcmp(name, "video") == 0) {
        return FS_EXE_VIDEO;
    }
    if (strcmp(name, "breach") == 0) {
        return FS_EXE_BREACH;
    }
    if (strcmp(name, "gate") == 0) {
        return FS_EXE_GATE;
    }
    if (strcmp(name, "tapes") == 0) {
        return FS_EXE_TAPES;
    }
    if (strcmp(name, "dev") == 0) {
        return FS_EXE_DEV;
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
    memset(&s_camfs, 0, sizeof(s_camfs));
    fs_mount(&s_camfs, FS_DRIVE_C, "CAMERA FILM", CAMERA_CAPACITY);
    memset(s_photo_used, 0, sizeof(s_photo_used));

    memset(&s_build, 0, sizeof(s_build));
    fs_mount(&s_build, FS_DRIVE_D, "RELAY R-4", TOWER_CAPACITY);
    FsRef troot = fs_root(FS_DRIVE_D);
    fs_mkfile_rom(&s_build, troot, "NODELOG.TXT", TOWER_LOG, 0, FS_EXE_NONE);
    fs_mkfile_rom(&s_build, troot, "GATE.EXE", 0, 6144, FS_EXE_GATE);
    fs_mkfile_rom(&s_build, troot, "RELAY.DAT", 0, 96256, FS_EXE_NONE);
    memcpy(&s_tower, &s_build.drives[FS_DRIVE_D], sizeof(FsDrive));
    fs_unmount(&s_build, FS_DRIVE_D);
}

void disks_tower_load(FsDrive* dst)
{
    memcpy(dst, &s_tower, sizeof(FsDrive));
    dst->mounted = 1;
}

static void photo_mark_drive(const FsDrive* d, b32* marked)
{
    for (i32 i = 1; i < FS_DRIVE_NODES; i++) {
        const FsNode* n = &d->nodes[i];
        if (n->used && n->pic > 0 && n->pic <= PHOTO_SLOTS) {
            marked[n->pic - 1] = 1;
        }
    }
}

static i32 photo_alloc(const Fs* live_fs)
{
    for (i32 i = 0; i < PHOTO_SLOTS; i++) {
        if (!s_photo_used[i]) {
            return i;
        }
    }
    b32 marked[PHOTO_SLOTS] = {0};
    photo_mark_drive(&s_camfs.drives[FS_DRIVE_C], marked);
    if (live_fs) {
        for (i32 d = 0; d < FS_DRIVE_COUNT; d++) {
            if (live_fs->drives[d].mounted) {
                photo_mark_drive(&live_fs->drives[d], marked);
            }
        }
    }
    for (i32 i = 0; i < DISK_COUNT; i++) {
        photo_mark_drive(&s_store[i], marked);
    }
    i32 found = -1;
    for (i32 i = 0; i < PHOTO_SLOTS; i++) {
        if (!marked[i]) {
            s_photo_used[i] = 0;
            if (found < 0) {
                found = i;
            }
        }
    }
    return found;
}

void disks_camera_load(FsDrive* dst)
{
    memcpy(dst, &s_camfs.drives[FS_DRIVE_C], sizeof(FsDrive));
    dst->mounted = 1;
}

void disks_camera_store(const FsDrive* src)
{
    memcpy(&s_camfs.drives[FS_DRIVE_C], src, sizeof(FsDrive));
    s_camfs.drives[FS_DRIVE_C].mounted = 1;
}

static u8 photo_luma(const u8* p)
{
    return (u8)((u32)(p[0] * 54 + p[1] * 183 + p[2] * 19) >> 8);
}

static void photo_tone_map(u8* p)
{
    u32 hist[256] = {0};
    for (u32 i = 0; i < PHOTO_PIXELS; i++) {
        hist[photo_luma(&p[i * 3])]++;
    }
    u32 clip = PHOTO_PIXELS / 250;
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
        f32 t = ((f32)v - (f32)lo) / (f32)(hi - lo);
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        lut[v] = (u8)(235.0f * powf(t, 0.95f) + 0.5f);
    }
    for (u32 i = 0; i < PHOTO_PIXELS; i++) {
        u8* px = &p[i * 3];
        u32 luma = photo_luma(px);
        u32 target = lut[luma];
        u32 scale = (target << 8) / (luma > 0 ? luma : 1);
        for (u32 c = 0; c < 3; c++) {
            u32 v = ((u32)px[c] * scale) >> 8;
            px[c] = (u8)(v > 255 ? 255 : v);
        }
    }
}

b32 disks_camera_capture(Fs* live_fs, const u8* rgb)
{
    Fs* fs = live_fs && live_fs->drives[FS_DRIVE_C].mounted ? live_fs : &s_camfs;
    i32 slot = photo_alloc(live_fs);
    if (slot < 0) {
        return 0;
    }
    FsRef dir = fs_root(FS_DRIVE_C);
    char name[FS_NAME_MAX + 1];
    FsRef file = { FS_DRIVE_C, -1 };
    for (u32 attempt = 0; attempt < 100; attempt++) {
        snprintf(name, sizeof(name), "IMG_%02u.PIC", s_img_counter % 100);
        s_img_counter++;
        file = fs_mkfile_rom(fs, dir, name, 0, PHOTO_FILE_BYTES, FS_EXE_NONE);
        if (fs_ref_valid(fs, file)) {
            break;
        }
        if (fs_free_bytes(fs, FS_DRIVE_C) < PHOTO_FILE_BYTES) {
            return 0;
        }
    }
    if (!fs_ref_valid(fs, file)) {
        return 0;
    }
    fs_node_mut(fs, file)->pic = slot + 1;
    memcpy(s_photo[slot], rgb, PHOTO_BYTES);
    photo_tone_map(s_photo[slot]);
    s_photo_used[slot] = 1;
    return 1;
}

u32 disks_camera_exposures_left(const Fs* live_fs)
{
    const Fs* fs = live_fs && live_fs->drives[FS_DRIVE_C].mounted ? live_fs : &s_camfs;
    return fs_free_bytes(fs, FS_DRIVE_C) / PHOTO_FILE_BYTES;
}

const u8* disk_photo_data(i32 pic)
{
    if (pic <= 0 || pic > PHOTO_SLOTS || !s_photo_used[pic - 1]) {
        return 0;
    }
    return s_photo[pic - 1];
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
