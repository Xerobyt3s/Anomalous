#include "terminal/terminal.h"
#include "terminal/term_font.h"
#include "render/fontchain.h"
#include "terminal/comms.h"
#include "terminal/fs.h"
#include "terminal/disks.h"
#include "terminal/mapdata.h"
#include "audio/tapes.h"
#include "core/log.h"
#include "carsys/carsys.h"
#include "vehicle/vehicle.h"
#include "world/terrain.h"
#include "assets/assets.h"
#include "platform/platform.h"
#include "platform/gl_loader.h"
#include "render/render.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define TERM_CPS 620.0f
#define TERM_PERSIST_DECAY 0.80f
#define TERM_PROMPT_MAX (FS_PATH_MAX + 2)

#define COMMS_CONNECT_CPS 46.0f
#define COMMS_CONNECT_HOLD 0.7f
#define COMMS_SPEECH_CPS 26.0f
#define COMMS_SPRITE_SCAN_TIME 1.1f

#define LIDAR_N 112
#define LIDAR_SPACING 3.6f
#define LIDAR_MAX_POINTS 36928
#define LIDAR_REVEAL_SPEED 95.0f
#define LIDAR_WIDE_REVEAL_SPEED 950.0f
#define MAP_WIDE_REFRESH 10.0f

#define SURVEY_SETTLE_TIME 1.4f
#define SURVEY_PITCH 0.82f
#define SURVEY_DIST 190.0f
#define SURVEY_FOV (50.0f * PI32 / 180.0f)

enum {
    TC_BG,
    TC_GREEN,
    TC_BRIGHT,
    TC_AMBER,
    TC_RED,
    TC_DIM,
};

typedef struct GlyphInst {
    f32 x;
    f32 y;
    f32 glyph;
    f32 color;
    f32 w;
} GlyphInst;

typedef struct TermGpu {
    u32 atlas;
    u32 fbo;
    u32 color_tex;
    u32 depth_tex;
    u32 persist_fbo[2];
    u32 persist_tex[2];
    u32 persist_idx;
    u32 text_vao;
    u32 text_vbo;
    u32 point_vao;
    u32 point_vbo;
    u32 empty_vao;
    u32 pic_tex;
    i32 pic_uploaded;
    b32 clear_pending;
    b32 ready;
} TermGpu;

typedef struct WireDraw {
    const GpuMesh* mesh;
    Mat4 model;
    Vec3 color;
} WireDraw;

#define WIRE_VP_X 392
#define WIRE_VP_Y 116
#define WIRE_VP_W 208
#define WIRE_VP_H 224

static TermGpu s_gpu;
static GlyphInst s_glyph_insts[TERM_ROWS * TERM_COLS * 2];
static f32 s_lidar_pts[LIDAR_MAX_POINTS][4];
static u32 s_lidar_count;
static b32 s_lidar_dirty;
static Vec3 s_lidar_center;
static i32 s_lidar_tier;
static WireDraw s_wires[12];
static u32 s_wire_count;
static Comms s_comms;
static Fs s_fs;
static i32 s_disk_inserted = -1;
static u32 s_video_src_tex;
static u32 s_map_saved_count;
static b32 s_map_file_created;

#define MAP_FILE_BASE 256
#define MAP_FILE_CELL_BYTES 24

static void map_sync_file(Terminal* term)
{
    if (!fs_drive_mounted(&s_fs, FS_DRIVE_A)) {
        return;
    }
    u32 count = mapdata_count();
    FsRef root = fs_root(FS_DRIVE_A);
    FsRef ref = fs_resolve(&s_fs, root, "MAP.DAT");
    if (!fs_ref_valid(&s_fs, ref)) {
        if (s_map_file_created) {
            mapdata_reset();
            count = 0;
        }
        ref = fs_mkfile_rom(&s_fs, root, "MAP.DAT", 0, MAP_FILE_BASE, FS_EXE_NONE);
        if (!fs_ref_valid(&s_fs, ref)) {
            s_map_saved_count = 0;
            term->map_disk_full = 1;
            term->map_corrupt = 0;
            return;
        }
        s_map_file_created = 1;
    }
    FsNode* node = fs_node_mut(&s_fs, ref);
    u32 desired = MAP_FILE_BASE + count * MAP_FILE_CELL_BYTES;
    if (desired > node->size) {
        u32 room = node->size + fs_free_bytes(&s_fs, FS_DRIVE_A);
        node->size = desired < room ? desired : room;
    }
    s_map_saved_count = node->size > MAP_FILE_BASE
                        ? (node->size - MAP_FILE_BASE) / MAP_FILE_CELL_BYTES : 0;
    if (s_map_saved_count > count) {
        s_map_saved_count = count;
    }
    term->map_disk_full = node->size < desired;
    term->map_corrupt = node->corrupted;
}

typedef struct Virus {
    b32 active;
    u32 rng;
    f32 burst;
    f32 next_beep;
    f32 next_corrupt;
} Virus;

static Virus s_virus;

static f32 virus_rand(void)
{
    s_virus.rng = s_virus.rng * 1664525u + 1013904223u;
    return (f32)(s_virus.rng >> 8) / 16777216.0f;
}

static void virus_infect(void)
{
    if (s_virus.active) {
        return;
    }
    s_virus.active = 1;
    s_virus.rng = 0xBADC0DEu;
    s_virus.burst = 0.45f;
    s_virus.next_beep = 5.0f;
    s_virus.next_corrupt = 70.0f;
}

static void virus_cure(void)
{
    Virus zero = {0};
    s_virus = zero;
}

static f32 virus_lie(const Terminal* term, f32 v, f32 salt)
{
    if (!s_virus.active) {
        return v;
    }
    f32 w = sinf(term->blink * (1.7f + salt * 0.9f) + salt * 13.7f);
    return v * (0.25f + 1.3f * f_abs(w));
}

static void comms_enter_phase(Terminal* term, CommsPhase phase);
static void comms_key_char(Terminal* term, char c);
static void comms_key_enter(Terminal* term);
static void term_print(Terminal* term, const char* text);
static void term_printf(Terminal* term, const char* fmt, ...);
static void grid_clear(Terminal* term);
static void grid_text(Terminal* term, i32 row, i32 col, u8 color, const char* fmt, ...);
static void grid_title(Terminal* term, const char* title);


b32 terminal_init(Terminal* term)
{
    Terminal zero = {0};
    *term = zero;
    term->history_pos = -1;
    term->format_drive = -1;
    term->tapes_write_track = -1;
    term->tapes_dl_track = -1;
    term->tapes_prompt_track = -1;

    comms_init(&s_comms);
    fs_init(&s_fs);
    disks_init(&s_fs);
    if (!term_font_init()) {
        return 0;
    }
    s_gpu.atlas = term_font_texture();

    glCreateTextures(GL_TEXTURE_2D, 1, &s_gpu.color_tex);
    glTextureStorage2D(s_gpu.color_tex, 1, GL_RGBA8, TERM_TEX_W, TERM_TEX_H);
    glTextureParameteri(s_gpu.color_tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(s_gpu.color_tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(s_gpu.color_tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(s_gpu.color_tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glCreateTextures(GL_TEXTURE_2D, 1, &s_gpu.depth_tex);
    glTextureStorage2D(s_gpu.depth_tex, 1, GL_DEPTH_COMPONENT24, TERM_TEX_W, TERM_TEX_H);

    glCreateFramebuffers(1, &s_gpu.fbo);
    glNamedFramebufferTexture(s_gpu.fbo, GL_COLOR_ATTACHMENT0, s_gpu.color_tex, 0);
    glNamedFramebufferTexture(s_gpu.fbo, GL_DEPTH_ATTACHMENT, s_gpu.depth_tex, 0);
    if (glCheckNamedFramebufferStatus(s_gpu.fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        log_error("terminal: framebuffer incomplete");
        return 0;
    }

    for (u32 i = 0; i < 2; i++) {
        glCreateTextures(GL_TEXTURE_2D, 1, &s_gpu.persist_tex[i]);
        glTextureStorage2D(s_gpu.persist_tex[i], 5, GL_RGBA8, TERM_TEX_W, TERM_TEX_H);
        glTextureParameteri(s_gpu.persist_tex[i], GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTextureParameteri(s_gpu.persist_tex[i], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(s_gpu.persist_tex[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(s_gpu.persist_tex[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glCreateFramebuffers(1, &s_gpu.persist_fbo[i]);
        glNamedFramebufferTexture(s_gpu.persist_fbo[i], GL_COLOR_ATTACHMENT0, s_gpu.persist_tex[i], 0);
    }

    glCreateBuffers(1, &s_gpu.text_vbo);
    glNamedBufferStorage(s_gpu.text_vbo, sizeof(s_glyph_insts), 0, GL_DYNAMIC_STORAGE_BIT);
    glCreateVertexArrays(1, &s_gpu.text_vao);
    glVertexArrayVertexBuffer(s_gpu.text_vao, 0, s_gpu.text_vbo, 0, sizeof(GlyphInst));
    glVertexArrayBindingDivisor(s_gpu.text_vao, 0, 1);
    glEnableVertexArrayAttrib(s_gpu.text_vao, 0);
    glVertexArrayAttribFormat(s_gpu.text_vao, 0, 2, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(s_gpu.text_vao, 0, 0);
    glEnableVertexArrayAttrib(s_gpu.text_vao, 1);
    glVertexArrayAttribFormat(s_gpu.text_vao, 1, 1, GL_FLOAT, GL_FALSE, 8);
    glVertexArrayAttribBinding(s_gpu.text_vao, 1, 0);
    glEnableVertexArrayAttrib(s_gpu.text_vao, 2);
    glVertexArrayAttribFormat(s_gpu.text_vao, 2, 1, GL_FLOAT, GL_FALSE, 12);
    glVertexArrayAttribBinding(s_gpu.text_vao, 2, 0);
    glEnableVertexArrayAttrib(s_gpu.text_vao, 3);
    glVertexArrayAttribFormat(s_gpu.text_vao, 3, 1, GL_FLOAT, GL_FALSE, 16);
    glVertexArrayAttribBinding(s_gpu.text_vao, 3, 0);

    glCreateBuffers(1, &s_gpu.point_vbo);
    glNamedBufferStorage(s_gpu.point_vbo, sizeof(s_lidar_pts), 0, GL_DYNAMIC_STORAGE_BIT);
    glCreateVertexArrays(1, &s_gpu.point_vao);
    glVertexArrayVertexBuffer(s_gpu.point_vao, 0, s_gpu.point_vbo, 0, 4 * sizeof(f32));
    glEnableVertexArrayAttrib(s_gpu.point_vao, 0);
    glVertexArrayAttribFormat(s_gpu.point_vao, 0, 4, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(s_gpu.point_vao, 0, 0);

    glCreateVertexArrays(1, &s_gpu.empty_vao);

    glCreateTextures(GL_TEXTURE_2D, 1, &s_gpu.pic_tex);
    glTextureStorage2D(s_gpu.pic_tex, 1, GL_RGB8, PHOTO_W, PHOTO_H);
    glTextureParameteri(s_gpu.pic_tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(s_gpu.pic_tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(s_gpu.pic_tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(s_gpu.pic_tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    s_gpu.pic_uploaded = -1;

    s_gpu.clear_pending = 1;
    s_gpu.ready = 1;
    return 1;
}

u32 terminal_texture(const Terminal* term)
{
    (void)term;
    return s_gpu.persist_tex[s_gpu.persist_idx];
}

b32 terminal_camera_capture(const u8* gray)
{
    return disks_camera_capture(&s_fs, gray);
}

b32 terminal_video_active(const Terminal* term)
{
    return term->powered && term->mode == TERM_VIDEO
           && term->coax_state == 2 && term->coax_camera;
}

void terminal_video_set(u32 texture)
{
    s_video_src_tex = texture;
}

u32 terminal_camera_exposures(void)
{
    return disks_camera_exposures_left(&s_fs);
}

static void terminal_camera_mount(Terminal* term, b32 want)
{
    b32 mounted = s_fs.drives[FS_DRIVE_C].mounted;
    if (want && !mounted) {
        disks_camera_load(&s_fs.drives[FS_DRIVE_C]);
        if (term->mode == TERM_SHELL) {
            term_print(term, "DRIVE C: CAMERA FILM MOUNTED\n");
        }
    } else if (!want && mounted) {
        disks_camera_store(&s_fs.drives[FS_DRIVE_C]);
        fs_unmount(&s_fs, FS_DRIVE_C);
        if (term->cwd_drive == FS_DRIVE_C) {
            term->cwd_drive = FS_DRIVE_A;
            term->cwd_node = 0;
        }
        if (term->mode == TERM_SHELL) {
            term_print(term, "DRIVE C: DISCONNECTED\n");
        }
    }
}

static void terminal_tower_mount(Terminal* term, b32 want)
{
    b32 mounted = s_fs.drives[FS_DRIVE_D].mounted;
    if (want && !mounted) {
        disks_tower_load(&s_fs.drives[FS_DRIVE_D]);
        if (term->mode == TERM_SHELL) {
            term_print(term, "DRIVE D: RELAY R-4 LINKED\n");
            term_print(term, "RUN MAP TO PULL THE SURVEY CACHE.\n");
        }
    } else if (!want && mounted) {
        fs_unmount(&s_fs, FS_DRIVE_D);
        if (term->cwd_drive == FS_DRIVE_D) {
            term->cwd_drive = FS_DRIVE_A;
            term->cwd_node = 0;
        }
        if (term->mode == TERM_SHELL) {
            term_print(term, "DRIVE D: DISCONNECTED\n");
        }
    }
}

void terminal_disk_set(Terminal* term, i32 disk)
{
    if (disk == s_disk_inserted) {
        return;
    }
    if (s_disk_inserted >= 0) {
        disk_store(s_disk_inserted, &s_fs.drives[FS_DRIVE_B]);
        fs_unmount(&s_fs, FS_DRIVE_B);
    }
    s_disk_inserted = disk;
    if (disk >= 0) {
        disk_load(disk, &s_fs.drives[FS_DRIVE_B]);
    } else if (term->cwd_drive == FS_DRIVE_B) {
        term->cwd_drive = FS_DRIVE_A;
        term->cwd_node = 0;
    }
    if (term->powered && term->mode == TERM_SHELL) {
        if (disk >= 0) {
            term_printf(term, "DRIVE B: MEDIA INSERTED (%s)\n", disk_label(disk));
        } else {
            term_print(term, "DRIVE B: MEDIA REMOVED\n");
        }
    }
}

static void term_commit_line(Terminal* term)
{
    memcpy(term->lines[term->line_head], term->out_line, sizeof(term->out_line));
    term->line_head = (term->line_head + 1) % TERM_LINES;
    if (term->line_count < TERM_LINES) {
        term->line_count++;
    }
    term->out_line[0] = 0;
    term->out_len = 0;
}

static void term_print(Terminal* term, const char* text)
{
    for (const char* c = text; *c; c++) {
        u32 next = (term->pend_head + 1) % TERM_PENDING_MAX;
        if (next == term->pend_tail) {
            return;
        }
        term->pending[term->pend_head] = *c;
        term->pend_head = next;
    }
}

static void term_printf(Terminal* term, const char* fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    term_print(term, buf);
}

static b32 term_reveal_idle(const Terminal* term)
{
    return term->pend_head == term->pend_tail;
}

static void term_reveal(Terminal* term, f32 dt)
{
    term->reveal_accum += TERM_CPS * dt;
    while (term->reveal_accum >= 1.0f && term->pend_tail != term->pend_head) {
        term->reveal_accum -= 1.0f;
        char c = term->pending[term->pend_tail];
        term->pend_tail = (term->pend_tail + 1) % TERM_PENDING_MAX;
        if (c == '\n' || term->out_len >= TERM_COLS) {
            term_commit_line(term);
        }
        if (c != '\n') {
            term->out_line[term->out_len++] = c;
            term->out_line[term->out_len] = 0;
            if (c != ' ') {
                term->click_pending = 1;
            }
        }
    }
}

static void term_run_command(Terminal* term, const char* cmd);

static void term_prompt(const Terminal* term, char* buf, u32 size)
{
    FsRef cwd = { term->cwd_drive, term->cwd_node };
    char path[FS_PATH_MAX];
    fs_path_string(&s_fs, cwd, path, sizeof(path));
    snprintf(buf, size, "%s>", path);
}

static void term_submit(Terminal* term)
{
    char cmd[TERM_INPUT_MAX + 1];
    char prompt[TERM_PROMPT_MAX];
    term_prompt(term, prompt, sizeof(prompt));
    snprintf(cmd, sizeof(cmd), "%s", term->input);
    term_printf(term, "%s%s\n", prompt, cmd);
    if (term->input_len > 0) {
        snprintf(term->history[term->history_count % TERM_HISTORY], TERM_INPUT_MAX + 1,
                 "%s", term->input);
        term->history_count++;
    }
    term->history_pos = -1;
    term->input[0] = 0;
    term->input_len = 0;
    term->input_cursor = 0;
    term_run_command(term, cmd);
}

static b32 cmd_is(const char* cmd, const char* name)
{
    for (u32 i = 0;; i++) {
        char a = cmd[i];
        char b = name[i];
        if (a >= 'a' && a <= 'z') {
            a = (char)(a - 'a' + 'A');
        }
        if (a != b) {
            return 0;
        }
        if (a == 0) {
            return 1;
        }
    }
}

static i32 term_tokenize(const char* cmd, char tokens[3][TERM_INPUT_MAX + 1])
{
    i32 n = 0;
    const char* p = cmd;
    while (*p && n < 3) {
        while (*p == ' ') {
            p++;
        }
        if (!*p) {
            break;
        }
        u32 len = 0;
        while (*p && *p != ' ') {
            if (len < TERM_INPUT_MAX) {
                tokens[n][len++] = *p;
            }
            p++;
        }
        tokens[n][len] = 0;
        n++;
    }
    return n;
}

static b32 shell_check_path(Terminal* term, const char* path, FsRef* out)
{
    i32 pd = fs_path_drive(path);
    if (pd == -2) {
        term_print(term, "INVALID DRIVE SPECIFICATION\n");
        return 0;
    }
    if (pd >= 0 && !fs_drive_mounted(&s_fs, pd)) {
        term_printf(term, "DRIVE %c: NOT READY\n", 'A' + pd);
        return 0;
    }
    FsRef cwd = { term->cwd_drive, term->cwd_node };
    *out = fs_resolve(&s_fs, cwd, path);
    return 1;
}

static void path_split(const char* path, char* head, u32 head_size, char* tail, u32 tail_size)
{
    const char* cut = 0;
    for (const char* p = path; *p; p++) {
        if (*p == '\\' || *p == '/') {
            cut = p;
        }
    }
    if (!cut && path[0] && path[1] == ':') {
        cut = path + 1;
    }
    if (cut) {
        u32 hlen = (u32)(cut - path) + 1;
        if (hlen >= head_size) {
            hlen = head_size - 1;
        }
        memcpy(head, path, hlen);
        head[hlen] = 0;
        snprintf(tail, tail_size, "%s", cut + 1);
    } else {
        head[0] = 0;
        snprintf(tail, tail_size, "%s", path);
    }
}

static void shell_dir(Terminal* term, const char* path)
{
    FsRef dir;
    if (path[0]) {
        if (!shell_check_path(term, path, &dir)) {
            return;
        }
    } else {
        dir.drive = term->cwd_drive;
        dir.node = term->cwd_node;
    }
    if (!fs_drive_mounted(&s_fs, dir.drive)) {
        term_printf(term, "DRIVE %c: NOT READY\n", 'A' + dir.drive);
        return;
    }
    if (!fs_ref_valid(&s_fs, dir) || !fs_node(&s_fs, dir)->is_dir) {
        term_print(term, "PATH NOT FOUND\n");
        return;
    }
    const FsDrive* d = &s_fs.drives[dir.drive];
    char pathstr[FS_PATH_MAX];
    fs_path_string(&s_fs, dir, pathstr, sizeof(pathstr));
    term_printf(term, " VOLUME IN DRIVE %c IS %s\n", 'A' + dir.drive, d->label);
    term_printf(term, " DIRECTORY OF %s\n\n", pathstr);
    u32 files = 0;
    u32 bytes = 0;
    for (i32 pass = 0; pass < 2; pass++) {
        for (i32 i = 1; i < FS_DRIVE_NODES; i++) {
            const FsNode* n = &d->nodes[i];
            if (!n->used || n->parent != dir.node || (pass == 0) != (n->is_dir != 0)) {
                continue;
            }
            if (n->is_dir) {
                term_printf(term, " %-12s   <DIR>\n", n->name);
            } else {
                char base[9] = "";
                char ext[4] = "";
                const char* dot = strrchr(n->name, '.');
                if (dot) {
                    u32 blen = (u32)(dot - n->name);
                    memcpy(base, n->name, blen > 8 ? 8 : blen);
                    base[blen > 8 ? 8 : blen] = 0;
                    snprintf(ext, sizeof(ext), "%s", dot + 1);
                } else {
                    snprintf(base, sizeof(base), "%s", n->name);
                }
                term_printf(term, " %-8s %-3s %10u\n", base, ext, n->size);
                files++;
                bytes += n->size;
            }
        }
    }
    term_printf(term, "%9u FILE(S) %10u BYTES\n", files, bytes);
    term_printf(term, "%20u BYTES FREE\n", fs_free_bytes(&s_fs, dir.drive));
}

static void shell_cd(Terminal* term, const char* path)
{
    if (!path[0]) {
        char pathstr[FS_PATH_MAX];
        FsRef cwd = { term->cwd_drive, term->cwd_node };
        fs_path_string(&s_fs, cwd, pathstr, sizeof(pathstr));
        term_printf(term, "%s\n", pathstr);
        return;
    }
    FsRef dir;
    if (!shell_check_path(term, path, &dir)) {
        return;
    }
    if (!fs_ref_valid(&s_fs, dir) || !fs_node(&s_fs, dir)->is_dir) {
        term_print(term, "INVALID DIRECTORY\n");
        return;
    }
    term->cwd_drive = dir.drive;
    term->cwd_node = dir.node;
}

static void shell_type_binary(Terminal* term, const char* name)
{
    u32 h = 2166136261u;
    for (const char* c = name; *c; c++) {
        h = (h ^ (u32)(u8)*c) * 16777619u;
    }
    char line[60];
    term_print(term, "MZ");
    for (i32 r = 0; r < 3; r++) {
        for (i32 i = 0; i < 58; i++) {
            h = h * 1664525u + 1013904223u;
            line[i] = (char)(33 + (h >> 20) % 92);
        }
        line[58] = 0;
        term_printf(term, "%s\n", line);
    }
}

static void shell_type(Terminal* term, const char* path)
{
    FsRef ref;
    if (!shell_check_path(term, path, &ref)) {
        return;
    }
    if (!fs_ref_valid(&s_fs, ref)) {
        term_print(term, "FILE NOT FOUND\n");
        return;
    }
    const FsNode* n = fs_node(&s_fs, ref);
    if (n->is_dir) {
        term_print(term, "ACCESS DENIED\n");
        return;
    }
    const char* text = fs_text(&s_fs, ref);
    if (!text || n->corrupted) {
        shell_type_binary(term, n->name);
        return;
    }
    term_print(term, text);
    term_print(term, "\n");
}

static void shell_view(Terminal* term, const char* path)
{
    FsRef ref;
    if (!shell_check_path(term, path, &ref)) {
        return;
    }
    if (!fs_ref_valid(&s_fs, ref)) {
        term_print(term, "FILE NOT FOUND\n");
        return;
    }
    const FsNode* n = fs_node(&s_fs, ref);
    if (n->is_dir || n->pic <= 0) {
        term_print(term, "NOT AN IMAGE FILE\n");
        return;
    }
    if (n->corrupted) {
        term_print(term, "IMAGE DATA CORRUPTED\n");
        return;
    }
    if (!disk_photo_data(n->pic)) {
        term_print(term, "IMAGE DATA MISSING\n");
        return;
    }
    term->mode = TERM_VIEW;
    term->view_pic = n->pic;
    term->map_materialize = 0.0f;
    snprintf(term->view_name, sizeof(term->view_name), "%s", n->name);
}

static void draw_view(Terminal* term)
{
    grid_clear(term);
    grid_title(term, "IMAGE VIEWER");
    grid_text(term, TERM_ROWS - 1, 1, TC_DIM, "%s   320X200 VC-6   [Q] BACK", term->view_name);
}

static void draw_video(Terminal* term)
{
    grid_clear(term);
    grid_title(term, "VIDEO FEED");
    if (fmodf(term->blink, 1.2f) < 0.7f) {
        grid_text(term, 1, TERM_COLS - 7, TC_RED, "\x7f LIVE");
    }
    grid_text(term, TERM_ROWS - 1, 1, TC_DIM, "SRC: COAX CAM-1   320X200 10FPS   [Q] BACK");
}

#define TAPES_LIST_MAX 160
#define TAPES_LIST_ROWS 14
#define TAPE_WRITE_TIME 7.0f
#define TAPE_DL_TIME 11.0f

typedef struct TapeEntry {
    i32 drive;
    i32 node;
    i32 track;
    b32 archive;
} TapeEntry;

static b32 tapes_relay_linked(const Terminal* term)
{
    return term->bus_state == 2 && term->bus_tower && term->tower_breached;
}

static i32 tapes_build_list(const Terminal* term, TapeEntry* out, i32 max)
{
    i32 n = 0;
    for (i32 d = 0; d < FS_DRIVE_COUNT && n < max; d++) {
        const FsDrive* drive = &s_fs.drives[d];
        if (!drive->mounted) {
            continue;
        }
        for (i32 i = 1; i < FS_DRIVE_NODES && n < max; i++) {
            const FsNode* node = &drive->nodes[i];
            if (!node->used || node->is_dir || node->trk <= 0) {
                continue;
            }
            out[n].drive = d;
            out[n].node = i;
            out[n].track = node->trk - 1;
            out[n].archive = 0;
            n++;
        }
    }
    if (tapes_relay_linked(term)) {
        for (i32 t = 0; t < (i32)tapes_count() && n < max; t++) {
            if (!tapes_on_relay(t)) {
                continue;
            }
            out[n].drive = -1;
            out[n].node = -1;
            out[n].track = t;
            out[n].archive = 1;
            n++;
        }
    }
    return n;
}

static void tapes_status_set(Terminal* term, const char* msg)
{
    snprintf(term->tapes_status, sizeof(term->tapes_status), "%s", msg);
    term->tapes_status_until = term->mode_timer + 3.2f;
}

static void tapes_trk_name(i32 track, char* out, u32 size)
{
    const char* name = tapes_name(track);
    u32 n = 0;
    for (const char* p = name; *p && n < 8 && n + 5 < size; p++) {
        char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            out[n++] = c;
        }
    }
    if (n == 0) {
        out[n++] = 'T';
    }
    snprintf(out + n, size - n, ".TRK");
}

static void tapes_dl_finish(Terminal* term)
{
    i32 track = term->tapes_dl_track;
    term->tapes_dl_track = -1;
    FsRef dir = { term->tapes_dest_drive, term->tapes_dest_node };
    if (!fs_ref_valid(&s_fs, dir) || !fs_node(&s_fs, dir)->is_dir) {
        tapes_status_set(term, "TARGET PATH LOST - DOWNLOAD LOST");
        return;
    }
    FsRef file = fs_mkfile_rom(&s_fs, dir, term->tapes_dest_name, 0, tapes_file_bytes(track),
                               FS_EXE_NONE);
    if (!fs_ref_valid(&s_fs, file)) {
        tapes_status_set(term, "WRITE FAULT - DOWNLOAD LOST");
        return;
    }
    fs_node_mut(&s_fs, file)->trk = track + 1;
    tapes_status_set(term, "DOWNLOAD COMPLETE");
}

static b32 tapes_resolve_dest(Terminal* term, i32 track)
{
    const char* path = term->tapes_dest;
    i32 pd = fs_path_drive(path);
    if (path[0] && path[1] == ':' && (pd < 0 || !fs_drive_mounted(&s_fs, pd))) {
        tapes_status_set(term, "DRIVE NOT READY");
        return 0;
    }
    FsRef cwd = { term->cwd_drive, term->cwd_node };
    FsRef dst = fs_resolve(&s_fs, cwd, path);
    FsRef dir;
    char name[32];
    if (fs_ref_valid(&s_fs, dst) && fs_node(&s_fs, dst)->is_dir) {
        dir = dst;
        tapes_trk_name(track, name, sizeof(name));
        FsRef ex = fs_resolve(&s_fs, dir, name);
        for (u32 digit = 0; digit < 10 && fs_ref_valid(&s_fs, ex); digit++) {
            name[strlen(name) - 5] = (char)('0' + digit);
            ex = fs_resolve(&s_fs, dir, name);
        }
        if (fs_ref_valid(&s_fs, ex)) {
            tapes_status_set(term, "TOO MANY COPIES IN TARGET DIR");
            return 0;
        }
    } else if (fs_ref_valid(&s_fs, dst)) {
        tapes_status_set(term, "FILE EXISTS");
        return 0;
    } else {
        char head[FS_PATH_MAX];
        path_split(path, head, sizeof(head), name, sizeof(name));
        if (head[0]) {
            dir = fs_resolve(&s_fs, cwd, head);
            if (!fs_ref_valid(&s_fs, dir) || !fs_node(&s_fs, dir)->is_dir) {
                tapes_status_set(term, "PATH NOT FOUND");
                return 0;
            }
        } else {
            dir = cwd;
        }
        if (strlen(name) > FS_NAME_MAX || !fs_name_valid(name)) {
            tapes_status_set(term, "BAD FILE NAME (8.3)");
            return 0;
        }
    }
    if (fs_free_bytes(&s_fs, dir.drive) < tapes_file_bytes(track)) {
        tapes_status_set(term, "DISK FULL");
        return 0;
    }
    term->tapes_dest_drive = dir.drive;
    term->tapes_dest_node = dir.node;
    snprintf(term->tapes_dest_name, sizeof(term->tapes_dest_name), "%s", name);
    return 1;
}

static void tapes_progress_bar(Terminal* term, i32 row, f32 frac)
{
    i32 width = 46;
    grid_text(term, row, 6, TC_DIM, "[");
    i32 filled = (i32)(frac * (f32)width);
    for (i32 i = 0; i < width; i++) {
        term->glyphs[row][7 + i] = i < filled ? '#' : (i == filled ? '>' : '.');
        term->colors[row][7 + i] = i < filled ? TC_GREEN : TC_DIM;
    }
    grid_text(term, row, 7 + width, TC_DIM, "]");
    grid_text(term, row + 1, (TERM_COLS - 4) / 2, TC_BRIGHT, "%3.0f%%", (f64)(frac * 100.0f));
}

static void draw_tapes_write(Terminal* term)
{
    grid_clear(term);
    grid_title(term, "TAPE ARCHIVE -- WRITING");
    grid_text(term, 3, 4, TC_DIM, "SOURCE  %s", tapes_name(term->tapes_write_track));
    grid_text(term, 4, 4, TC_DIM, "TARGET  DECK CASSETTE");
    f32 frac = f_clamp01(term->tapes_write_t / TAPE_WRITE_TIME);
    tapes_progress_bar(term, 9, frac);
    if (fmodf(term->blink, 0.9f) < 0.55f) {
        grid_text(term, 13, 4, TC_AMBER, "WRITE IN PROGRESS -- DO NOT EJECT");
    }
    grid_text(term, TERM_ROWS - 1, 1, TC_DIM, "[Q] ABORT");
}

static void draw_tapes_dl(Terminal* term)
{
    grid_clear(term);
    grid_title(term, "RELAY LINK -- TRACK TRANSFER");
    char pathstr[FS_PATH_MAX];
    FsRef dir = { term->tapes_dest_drive, term->tapes_dest_node };
    fs_path_string(&s_fs, dir, pathstr, sizeof(pathstr));
    u32 plen = (u32)strlen(pathstr);
    grid_text(term, 3, 4, TC_DIM, "SOURCE  RELAY :: %s", tapes_name(term->tapes_dl_track));
    grid_text(term, 4, 4, TC_DIM, "TARGET  %s%s%s", pathstr,
              plen > 0 && pathstr[plen - 1] == '\\' ? "" : "\\", term->tapes_dest_name);
    f32 frac = f_clamp01(term->tapes_dl_t / TAPE_DL_TIME);
    u32 total = tapes_file_bytes(term->tapes_dl_track);
    grid_text(term, 6, 4, TC_GREEN, "%7u / %7u BYTES", (u32)(frac * (f32)total), total);
    i32 rate = 3200 + (i32)(fmodf(term->blink * 7.3f, 1.0f) * 900.0f);
    grid_text(term, 6, 40, TC_DIM, "%d B/S", rate);
    tapes_progress_bar(term, 9, frac);
    if (fmodf(term->blink, 0.9f) < 0.55f) {
        grid_text(term, 13, 4, TC_AMBER, "TRANSFER IN PROGRESS -- DO NOT DISCONNECT");
    }
    grid_text(term, TERM_ROWS - 1, 1, TC_DIM, "[Q] ABORT");
}

static void draw_tapes_list(Terminal* term)
{
    TapeEntry entries[TAPES_LIST_MAX];
    i32 count = tapes_build_list(term, entries, TAPES_LIST_MAX);
    term->tapes_count = count;
    if (term->tapes_sel >= count) {
        term->tapes_sel = count > 0 ? count - 1 : 0;
    }
    if (term->tapes_sel < 0) {
        term->tapes_sel = 0;
    }

    grid_clear(term);
    grid_title(term, "TAPE ARCHIVE");

    if (!term->deck_docked) {
        grid_text(term, 2, 2, TC_RED, "DECK: TERMINAL NOT DOCKED");
    } else if (term->deck_tape < 0) {
        grid_text(term, 2, 2, TC_DIM, "DECK: NO CASSETTE");
    } else {
        grid_text(term, 2, 2, TC_GREEN, "DECK: \"%s\" (%.0f%%)%s", tape_label(term->deck_tape),
                  (f64)(term->deck_cond * 100.0f),
                  term->deck_play ? "  << ROLLING >>" : "");
    }
    b32 relay = tapes_relay_linked(term);
    grid_text(term, 3, 2, relay ? TC_BRIGHT : TC_DIM,
              relay ? "RELAY ARCHIVE ONLINE" : "NO RELAY LINK -- DOWNLOADS OFFLINE");
    b32 buslink = term->bus_state == 2 && !term->bus_tower;
    grid_text(term, 3, 38, buslink ? TC_GREEN : TC_DIM,
              buslink ? "VEHICLE BUS ONLINE" : "NO VEHICLE BUS -- NO WRITES");

    if (count == 0) {
        grid_text(term, 7, 4, TC_DIM, "NO TRACK FILES ON ANY MOUNTED DRIVE.");
        grid_text(term, 8, 4, TC_DIM, "LINK A BREACHED RELAY NODE TO DOWNLOAD TRACKS.");
    }
    i32 first = term->tapes_sel - (TAPES_LIST_ROWS - 1);
    if (first < 0) {
        first = 0;
    }
    for (i32 i = first; i < count && i - first < TAPES_LIST_ROWS; i++) {
        i32 row = 5 + (i - first);
        b32 sel = i == term->tapes_sel;
        if (sel) {
            grid_text(term, row, 2, TC_BRIGHT, ">");
        }
        const TapeEntry* e = &entries[i];
        if (e->archive) {
            grid_text(term, row, 4, sel ? TC_BRIGHT : TC_AMBER, "RELAY %5uK  %s",
                      tapes_file_bytes(e->track) / 1024, tapes_name(e->track));
        } else {
            const FsNode* node = &s_fs.drives[e->drive].nodes[e->node];
            grid_text(term, row, 4, sel ? TC_BRIGHT : TC_GREEN, "%c:    %5uK  %s%s",
                      'A' + e->drive, node->size / 1024, tapes_name(e->track),
                      node->corrupted ? " [DMG]" : "");
        }
    }
    if (term->tapes_prompt_track >= 0) {
        grid_text(term, TERM_ROWS - 4, 2, TC_BRIGHT, "STORE \"%s\" AS:",
                  tapes_name(term->tapes_prompt_track));
        grid_text(term, TERM_ROWS - 3, 2, TC_GREEN, "%s", term->tapes_dest);
        if (fmodf(term->blink, 1.06f) < 0.53f) {
            i32 ccol = 2 + (i32)term->tapes_dest_cursor;
            if (ccol > TERM_COLS - 1) {
                ccol = TERM_COLS - 1;
            }
            term->glyphs[TERM_ROWS - 3][ccol] = 127;
            term->colors[TERM_ROWS - 3][ccol] = TC_BRIGHT;
        }
        if (term->tapes_status[0] && term->mode_timer < term->tapes_status_until) {
            grid_text(term, TERM_ROWS - 2, 2, TC_AMBER, "%s", term->tapes_status);
        }
        grid_text(term, TERM_ROWS - 1, 1, TC_DIM,
                  "[ENTER] START TRANSFER   [ENTER] ON EMPTY LINE CANCELS");
        return;
    }
    if (term->tapes_status[0] && term->mode_timer < term->tapes_status_until) {
        grid_text(term, TERM_ROWS - 3, 2, TC_AMBER, "%s", term->tapes_status);
    }
    grid_text(term, TERM_ROWS - 1, 1, TC_DIM,
              "[UP/DN] SELECT   [ENTER] WRITE TO CASSETTE / DOWNLOAD   [Q] EXIT");
}

static void tapes_enter(Terminal* term)
{
    TapeEntry entries[TAPES_LIST_MAX];
    i32 count = tapes_build_list(term, entries, TAPES_LIST_MAX);
    if (term->tapes_sel < 0 || term->tapes_sel >= count) {
        return;
    }
    const TapeEntry* e = &entries[term->tapes_sel];
    if (e->archive) {
        char name[16];
        tapes_trk_name(e->track, name, sizeof(name));
        term->tapes_prompt_track = e->track;
        snprintf(term->tapes_dest, sizeof(term->tapes_dest), "A:\\%s", name);
        term->tapes_dest_len = (u32)strlen(term->tapes_dest);
        term->tapes_dest_cursor = term->tapes_dest_len;
        term->click_pending = 1;
        return;
    }
    const FsNode* node = &s_fs.drives[e->drive].nodes[e->node];
    if (node->corrupted) {
        tapes_status_set(term, "TRACK DATA DAMAGED");
        return;
    }
    if (!term->deck_docked) {
        tapes_status_set(term, "TERMINAL NOT DOCKED IN VEHICLE");
        return;
    }
    if (term->bus_tower) {
        tapes_status_set(term, "BUS FEED IS RELAY NODE");
        return;
    }
    if (term->bus_state != 2) {
        tapes_status_set(term, term->bus_state == 1 ? "BUS PORT NOT INITIALIZED - RUN LINK"
                                                    : "NO VEHICLE BUS CABLE");
        return;
    }
    if (term->deck_tape < 0) {
        tapes_status_set(term, "NO CASSETTE IN DECK");
        return;
    }
    term->tapes_write_track = node->trk - 1;
    term->tapes_write_t = 0.0f;
    term->tapes_click_t = 0.0f;
    term->click_pending = 1;
}

static void update_tapes(Terminal* term, f32 dt)
{
    if (term->tapes_write_track >= 0) {
        if (term->bus_state != 2 || term->bus_tower) {
            term->tapes_write_track = -1;
            tapes_status_set(term, "WRITE FAILED -- BUS LINK LOST");
        } else if (!term->deck_docked || term->deck_tape < 0) {
            term->tapes_write_track = -1;
            tapes_status_set(term, "WRITE FAILED -- DECK LOST");
        } else {
            term->tapes_write_t += dt;
            term->tapes_click_t -= dt;
            if (term->tapes_click_t <= 0.0f) {
                term->tapes_click_t = 0.8f;
                term->click_pending = 1;
            }
            if (term->tapes_write_t >= TAPE_WRITE_TIME) {
                term->tape_write_request = 1;
                term->tape_write_value = term->tapes_write_track;
                term->tapes_write_track = -1;
                tapes_status_set(term, "WRITE COMPLETE");
            } else {
                draw_tapes_write(term);
                return;
            }
        }
    }
    if (term->tapes_dl_track >= 0) {
        if (!tapes_relay_linked(term)) {
            term->tapes_dl_track = -1;
            tapes_status_set(term, "RELAY LINK LOST -- TRANSFER ABORTED");
        } else {
            term->tapes_dl_t += dt;
            if (term->tapes_dl_t >= TAPE_DL_TIME) {
                tapes_dl_finish(term);
            } else {
                draw_tapes_dl(term);
                return;
            }
        }
    }
    draw_tapes_list(term);
}

static const char* dev_phase_name(f32 tod)
{
    if (tod < 0.20f || tod >= 0.80f) {
        return "NIGHT";
    }
    if (tod < 0.30f) {
        return "DAWN";
    }
    if (tod < 0.70f) {
        return "DAY";
    }
    return "DUSK";
}

static void draw_dev(Terminal* term)
{
    grid_clear(term);
    grid_title(term, "DEV CONSOLE -- FIELD DIAGNOSTICS");
    f32 hours = term->dev_tod * 24.0f;
    i32 hh = (i32)hours;
    i32 mm = (i32)((hours - (f32)hh) * 60.0f);
    static const char* wmode_names[4] = { "AUTO", "CLEAR", "DRIZZLE", "RAIN" };
    grid_text(term, 3, 4, TC_BRIGHT, "TIME      %02d:%02d  (%s)", hh, mm,
              dev_phase_name(term->dev_tod));
    grid_text(term, 4, 4, TC_GREEN, "WARP      %s", term->dev_warp ? "60X ENGAGED" : "OFF");
    grid_text(term, 6, 4, TC_BRIGHT, "WEATHER   %s   RAIN %3.0f%%   GROUND WET %3.0f%%",
              wmode_names[term->dev_wmode & 3], (f64)(term->dev_rain * 100.0f),
              (f64)(term->dev_wet * 100.0f));

    grid_text(term, 9, 4, TC_GREEN, "[LEFT]/[RIGHT]  TIME -/+ 30 MIN");
    grid_text(term, 10, 4, TC_GREEN, "[1] DAWN   [2] NOON   [3] DUSK   [4] MIDNIGHT");
    grid_text(term, 11, 4, TC_GREEN, "[T] TOGGLE TIME WARP");
    grid_text(term, 12, 4, TC_GREEN, "[5] WX AUTO   [6] CLEAR   [7] DRIZZLE   [8] RAIN");

    if (fmodf(term->blink, 1.4f) < 0.8f) {
        grid_text(term, 14, 4, TC_AMBER, "ENGINEERING BUILD -- NOT FOR FIELD UNITS");
    }
    grid_text(term, TERM_ROWS - 1, 1, TC_DIM, "[Q] EXIT");
}

static void dev_request_time(Terminal* term, f32 tod)
{
    tod -= floorf(tod);
    term->dev_time_value = tod;
    term->dev_time_request = 1;
    term->dev_tod = tod;
    term->click_pending = 1;
}

static void shell_del(Terminal* term, const char* path)
{
    FsRef ref;
    if (!shell_check_path(term, path, &ref)) {
        return;
    }
    if (!fs_ref_valid(&s_fs, ref)) {
        term_print(term, "FILE NOT FOUND\n");
        return;
    }
    FsError err = fs_delete(&s_fs, ref);
    if (err == FS_ERR_IS_DIR) {
        term_print(term, "CANNOT DELETE A DIRECTORY\n");
    }
}

static void shell_copy(Terminal* term, const char* srcp, const char* dstp)
{
    FsRef src;
    if (!shell_check_path(term, srcp, &src)) {
        return;
    }
    if (!fs_ref_valid(&s_fs, src)) {
        term_print(term, "FILE NOT FOUND\n");
        return;
    }
    if (fs_node(&s_fs, src)->is_dir) {
        term_print(term, "CANNOT COPY A DIRECTORY\n");
        return;
    }
    FsRef dst;
    if (!shell_check_path(term, dstp, &dst)) {
        return;
    }
    FsRef dst_dir;
    char dst_name[TERM_INPUT_MAX + 1];
    if (fs_ref_valid(&s_fs, dst) && fs_node(&s_fs, dst)->is_dir) {
        dst_dir = dst;
        snprintf(dst_name, sizeof(dst_name), "%s", fs_node(&s_fs, src)->name);
    } else {
        char head[TERM_INPUT_MAX + 1];
        path_split(dstp, head, sizeof(head), dst_name, sizeof(dst_name));
        if (head[0]) {
            if (!shell_check_path(term, head, &dst_dir)) {
                return;
            }
            if (!fs_ref_valid(&s_fs, dst_dir) || !fs_node(&s_fs, dst_dir)->is_dir) {
                term_print(term, "PATH NOT FOUND\n");
                return;
            }
        } else {
            dst_dir.drive = term->cwd_drive;
            dst_dir.node = term->cwd_node;
        }
    }
    FsError err = fs_copy(&s_fs, src, dst_dir, dst_name);
    switch (err) {
    case FS_OK:
        term_print(term, "        1 FILE(S) COPIED\n");
        break;
    case FS_ERR_NO_SPACE:
        term_print(term, "INSUFFICIENT DISK SPACE\n        0 FILE(S) COPIED\n");
        break;
    case FS_ERR_SELF:
        term_print(term, "FILE CANNOT BE COPIED ONTO ITSELF\n");
        break;
    case FS_ERR_BAD_NAME:
        term_print(term, "BAD FILE NAME\n");
        break;
    case FS_ERR_IS_DIR:
        term_print(term, "ACCESS DENIED\n");
        break;
    case FS_ERR_FULL:
        term_print(term, "DIRECTORY FULL\n");
        break;
    default:
        term_print(term, "COPY FAILED\n");
        break;
    }
}

static void shell_move(Terminal* term, const char* srcp, const char* dstp)
{
    FsRef src;
    if (!shell_check_path(term, srcp, &src)) {
        return;
    }
    if (!fs_ref_valid(&s_fs, src)) {
        term_print(term, "FILE NOT FOUND\n");
        return;
    }
    if (fs_node(&s_fs, src)->is_dir) {
        term_print(term, "CANNOT MOVE A DIRECTORY\n");
        return;
    }
    FsRef dst;
    if (!shell_check_path(term, dstp, &dst)) {
        return;
    }
    FsRef dst_dir;
    char dst_name[TERM_INPUT_MAX + 1];
    if (fs_ref_valid(&s_fs, dst) && fs_node(&s_fs, dst)->is_dir) {
        dst_dir = dst;
        snprintf(dst_name, sizeof(dst_name), "%s", fs_node(&s_fs, src)->name);
    } else {
        char head[TERM_INPUT_MAX + 1];
        path_split(dstp, head, sizeof(head), dst_name, sizeof(dst_name));
        if (head[0]) {
            if (!shell_check_path(term, head, &dst_dir)) {
                return;
            }
            if (!fs_ref_valid(&s_fs, dst_dir) || !fs_node(&s_fs, dst_dir)->is_dir) {
                term_print(term, "PATH NOT FOUND\n");
                return;
            }
        } else {
            dst_dir.drive = term->cwd_drive;
            dst_dir.node = term->cwd_node;
        }
    }
    FsError err = fs_move(&s_fs, src, dst_dir, dst_name);
    switch (err) {
    case FS_OK:
        term_print(term, "        1 FILE(S) MOVED\n");
        break;
    case FS_ERR_NO_SPACE:
        term_print(term, "INSUFFICIENT DISK SPACE\n        0 FILE(S) MOVED\n");
        break;
    case FS_ERR_SELF:
        term_print(term, "FILE CANNOT BE MOVED ONTO ITSELF\n");
        break;
    case FS_ERR_BAD_NAME:
        term_print(term, "BAD FILE NAME\n");
        break;
    case FS_ERR_IS_DIR:
        term_print(term, "ACCESS DENIED\n");
        break;
    case FS_ERR_FULL:
        term_print(term, "DIRECTORY FULL\n");
        break;
    default:
        term_print(term, "MOVE FAILED\n");
        break;
    }
}

static void shell_mkdir(Terminal* term, const char* path)
{
    FsRef ref;
    if (!shell_check_path(term, path, &ref)) {
        return;
    }
    if (fs_ref_valid(&s_fs, ref)) {
        term_print(term, fs_node(&s_fs, ref)->is_dir ? "DIRECTORY ALREADY EXISTS\n"
                                                     : "A FILE BY THAT NAME EXISTS\n");
        return;
    }
    char head[TERM_INPUT_MAX + 1];
    char name[TERM_INPUT_MAX + 1];
    path_split(path, head, sizeof(head), name, sizeof(name));
    FsRef dir;
    if (head[0]) {
        if (!shell_check_path(term, head, &dir)) {
            return;
        }
        if (!fs_ref_valid(&s_fs, dir) || !fs_node(&s_fs, dir)->is_dir) {
            term_print(term, "PATH NOT FOUND\n");
            return;
        }
    } else {
        dir.drive = term->cwd_drive;
        dir.node = term->cwd_node;
    }
    if (strlen(name) > FS_NAME_MAX || !fs_name_valid(name)) {
        term_print(term, "BAD DIRECTORY NAME\n");
        return;
    }
    FsRef made = fs_mkdir(&s_fs, dir, name);
    if (!fs_ref_valid(&s_fs, made)) {
        term_print(term, "CANNOT CREATE DIRECTORY\n");
    }
}

static void shell_rmdir(Terminal* term, const char* path)
{
    FsRef ref;
    if (!shell_check_path(term, path, &ref)) {
        return;
    }
    if (!fs_ref_valid(&s_fs, ref)) {
        term_print(term, "PATH NOT FOUND\n");
        return;
    }
    if (!fs_node(&s_fs, ref)->is_dir) {
        term_print(term, "NOT A DIRECTORY\n");
        return;
    }
    if (ref.drive == term->cwd_drive && ref.node == term->cwd_node) {
        term_print(term, "CANNOT REMOVE CURRENT DIRECTORY\n");
        return;
    }
    FsError err = fs_rmdir(&s_fs, ref);
    if (err == FS_ERR_FULL) {
        term_print(term, "DIRECTORY NOT EMPTY\n");
    } else if (err == FS_ERR_SELF) {
        term_print(term, "CANNOT REMOVE ROOT DIRECTORY\n");
    } else if (err != FS_OK) {
        term_print(term, "RMDIR FAILED\n");
    }
}

static void shell_chkdsk(Terminal* term, const char* arg)
{
    i32 drive = term->cwd_drive;
    if (arg[0]) {
        drive = fs_path_drive(arg);
        if (drive < 0 || arg[2] != 0) {
            term_print(term, "INVALID DRIVE SPECIFICATION\n");
            return;
        }
    }
    if (!fs_drive_mounted(&s_fs, drive)) {
        term_printf(term, "DRIVE %c: NOT READY\n", 'A' + drive);
        return;
    }
    const FsDrive* d = &s_fs.drives[drive];
    u32 files = 0;
    u32 dirs = 0;
    for (i32 i = 1; i < FS_DRIVE_NODES; i++) {
        if (d->nodes[i].used) {
            if (d->nodes[i].is_dir) {
                dirs++;
            } else {
                files++;
            }
        }
    }
    u32 used = fs_used_bytes(&s_fs, drive);
    term_printf(term, "VOLUME %s   DRIVE %c:\n\n", d->label, 'A' + drive);
    term_printf(term, "%10u BYTES TOTAL DISK SPACE\n", d->capacity);
    term_printf(term, "%10u BYTES IN %u FILE(S), %u DIRECTORY(S)\n", used, files, dirs);
    term_printf(term, "%10u BYTES AVAILABLE ON DISK\n", fs_free_bytes(&s_fs, drive));
    term_print(term, "\n    655360 BYTES TOTAL MEMORY\n    598016 BYTES FREE\n");
}

static void shell_format(Terminal* term, const char* arg)
{
    i32 drive = fs_path_drive(arg);
    if (drive < 0 || arg[2] != 0) {
        term_print(term, "INVALID DRIVE SPECIFICATION\n");
        return;
    }
    if (!fs_drive_mounted(&s_fs, drive)) {
        term_printf(term, "DRIVE %c: NOT READY\n", 'A' + drive);
        return;
    }
    term->format_drive = drive;
    term_printf(term, "WARNING: ALL DATA ON %sDRIVE %c: WILL BE LOST!\n",
                drive == FS_DRIVE_A ? "NON-REMOVABLE " : "", 'A' + drive);
    term_print(term, "PROCEED WITH FORMAT (Y/N)?\n");
}

static void term_av_scan(Terminal* term)
{
    u32 scanned = 0;
    u32 cleaned = 0;
    u32 damaged = 0;
    term_print(term, "RC ANTIVIRUS 4.0 (C) ROTCLIFF COMPUTING\n");
    for (i32 drive = 0; drive < FS_DRIVE_COUNT; drive++) {
        if (!fs_drive_mounted(&s_fs, drive)) {
            continue;
        }
        FsDrive* d = &s_fs.drives[drive];
        term_printf(term, "SCANNING DRIVE %c: ...\n", 'A' + drive);
        for (i32 i = 1; i < FS_DRIVE_NODES; i++) {
            FsNode* n = &d->nodes[i];
            if (!n->used || n->is_dir) {
                continue;
            }
            scanned++;
            if (n->infected) {
                n->infected = 0;
                cleaned++;
                term_printf(term, "  %s -- INFECTED. CLEANED.\n", n->name);
            }
            if (n->corrupted) {
                damaged++;
                term_printf(term, "  %s -- DAMAGED. CANNOT REPAIR.\n", n->name);
            }
        }
    }
    term_printf(term, "%u FILES SCANNED. %u CLEANED. %u DAMAGED.\n", scanned, cleaned, damaged);
    if (s_virus.active) {
        virus_cure();
        term_print(term, "MEMORY RESIDENT VIRUS PURGED.\nSYSTEM CLEAN.\n");
    } else {
        term_print(term, "NO RESIDENT THREATS.\n");
    }
}

#define BREACH_GRID 5
#define BREACH_BUF 7
#define BREACH_TARGET 3
#define BREACH_TIME 30.0f

static const u8 BREACH_BYTES[6] = { 0x1C, 0x55, 0xBD, 0xE9, 0x7A, 0xFF };

typedef struct Breach {
    u8 grid[BREACH_GRID][BREACH_GRID];
    b32 used[BREACH_GRID][BREACH_GRID];
    u8 buffer[BREACH_BUF];
    u8 target[BREACH_TARGET];
    u32 buf_len;
    i32 axis;
    i32 line;
    i32 cursor;
    f32 timer;
    i32 result;
    f32 result_time;
    u32 rng;
} Breach;

static Breach s_breach;
static u32 s_breach_seed = 0x1337C0DEu;

static u32 breach_rand(void)
{
    s_breach.rng = s_breach.rng * 1664525u + 1013904223u;
    return s_breach.rng >> 8;
}

static void breach_cell(i32 slot, i32* out_r, i32* out_c)
{
    if (s_breach.axis == 0) {
        *out_r = s_breach.line;
        *out_c = slot;
    } else {
        *out_r = slot;
        *out_c = s_breach.line;
    }
}

static b32 breach_line_unused(i32* out_first)
{
    b32 any = 0;
    for (i32 s = 0; s < BREACH_GRID; s++) {
        i32 r, c;
        breach_cell(s, &r, &c);
        if (!s_breach.used[r][c]) {
            if (!any && out_first) {
                *out_first = s;
            }
            any = 1;
        }
    }
    return any;
}

static b32 breach_target_hit(void)
{
    if (s_breach.buf_len < BREACH_TARGET) {
        return 0;
    }
    for (u32 start = 0; start + BREACH_TARGET <= s_breach.buf_len; start++) {
        b32 ok = 1;
        for (u32 k = 0; k < BREACH_TARGET; k++) {
            if (s_breach.buffer[start + k] != s_breach.target[k]) {
                ok = 0;
                break;
            }
        }
        if (ok) {
            return 1;
        }
    }
    return 0;
}

static void breach_start(void)
{
    Breach zero = {0};
    s_breach = zero;
    s_breach_seed = s_breach_seed * 2654435761u + 40503u;
    s_breach.rng = s_breach_seed;
    for (i32 r = 0; r < BREACH_GRID; r++) {
        for (i32 c = 0; c < BREACH_GRID; c++) {
            s_breach.grid[r][c] = BREACH_BYTES[breach_rand() % 6];
        }
    }
    b32 sim_used[BREACH_GRID][BREACH_GRID] = {0};
    i32 axis = 0;
    i32 line = 0;
    for (u32 t = 0; t < BREACH_TARGET; t++) {
        i32 choices[BREACH_GRID];
        i32 n = 0;
        for (i32 s = 0; s < BREACH_GRID; s++) {
            i32 r = axis == 0 ? line : s;
            i32 c = axis == 0 ? s : line;
            if (!sim_used[r][c]) {
                choices[n++] = s;
            }
        }
        if (n == 0) {
            s_breach.target[t] = BREACH_BYTES[breach_rand() % 6];
            continue;
        }
        i32 pick = choices[breach_rand() % (u32)n];
        i32 r = axis == 0 ? line : pick;
        i32 c = axis == 0 ? pick : line;
        s_breach.target[t] = s_breach.grid[r][c];
        sim_used[r][c] = 1;
        if (axis == 0) {
            axis = 1;
            line = c;
        } else {
            axis = 0;
            line = r;
        }
    }
    s_breach.axis = 0;
    s_breach.line = 0;
    s_breach.cursor = 0;
    s_breach.timer = BREACH_TIME;
    s_breach.result = 0;
}

static void breach_move(i32 dir)
{
    if (s_breach.result != 0) {
        return;
    }
    s_breach.cursor += dir;
    if (s_breach.cursor < 0) {
        s_breach.cursor = 0;
    }
    if (s_breach.cursor >= BREACH_GRID) {
        s_breach.cursor = BREACH_GRID - 1;
    }
}

static void breach_pick(Terminal* term)
{
    if (s_breach.result != 0) {
        return;
    }
    i32 r, c;
    breach_cell(s_breach.cursor, &r, &c);
    if (s_breach.used[r][c]) {
        term->click_pending = 1;
        return;
    }
    if (s_breach.buf_len < BREACH_BUF) {
        s_breach.buffer[s_breach.buf_len++] = s_breach.grid[r][c];
    }
    s_breach.used[r][c] = 1;
    term->click_pending = 1;
    if (breach_target_hit()) {
        s_breach.result = 1;
        s_breach.result_time = 0.0f;
        term->breach_request = 1;
        return;
    }
    if (s_breach.axis == 0) {
        s_breach.axis = 1;
        s_breach.line = c;
    } else {
        s_breach.axis = 0;
        s_breach.line = r;
    }
    i32 first = 0;
    b32 any = breach_line_unused(&first);
    s_breach.cursor = first;
    if (!any || s_breach.buf_len >= BREACH_BUF) {
        s_breach.result = -1;
        s_breach.result_time = 0.0f;
    }
}

static void breach_hex(u8 v, char* out)
{
    static const char* H = "0123456789ABCDEF";
    out[0] = H[(v >> 4) & 0xF];
    out[1] = H[v & 0xF];
    out[2] = 0;
}

static void draw_breach(Terminal* term, f32 dt)
{
    if (s_breach.result == 0) {
        s_breach.timer -= dt;
        if (s_breach.timer <= 0.0f) {
            s_breach.timer = 0.0f;
            s_breach.result = -1;
            s_breach.result_time = 0.0f;
        }
    } else {
        s_breach.result_time += dt;
    }

    grid_clear(term);
    grid_title(term, "ICE BREAK -- RELAY R-4");

    char hex[3];
    grid_text(term, 2, 2, TC_BRIGHT, "REQUIRED SEQUENCE");
    for (u32 i = 0; i < BREACH_TARGET; i++) {
        breach_hex(s_breach.target[i], hex);
        grid_text(term, 2, 22 + (i32)i * 4, TC_AMBER, "%s", hex);
    }
    grid_text(term, 3, 2, TC_DIM, "BUFFER");
    for (u32 i = 0; i < BREACH_BUF; i++) {
        if (i < s_breach.buf_len) {
            breach_hex(s_breach.buffer[i], hex);
            grid_text(term, 3, 22 + (i32)i * 4, TC_GREEN, "%s", hex);
        } else {
            grid_text(term, 3, 22 + (i32)i * 4, TC_DIM, "--");
        }
    }

    i32 gr0 = 6;
    i32 gc0 = 20;
    for (i32 r = 0; r < BREACH_GRID; r++) {
        for (i32 c = 0; c < BREACH_GRID; c++) {
            b32 on_line = s_breach.axis == 0 ? (r == s_breach.line) : (c == s_breach.line);
            b32 is_cursor = (s_breach.axis == 0 ? r == s_breach.line && c == s_breach.cursor
                                                : c == s_breach.line && r == s_breach.cursor);
            i32 row = gr0 + r * 2;
            i32 col = gc0 + c * 5;
            if (s_breach.used[r][c]) {
                grid_text(term, row, col, TC_DIM, "##");
                continue;
            }
            breach_hex(s_breach.grid[r][c], hex);
            u8 color = on_line ? TC_GREEN : TC_DIM;
            if (is_cursor && s_breach.result == 0) {
                if (fmodf(term->blink, 0.5f) < 0.28f) {
                    grid_text(term, row, col - 1, TC_BRIGHT, "[%s]", hex);
                    continue;
                }
                color = TC_BRIGHT;
            }
            grid_text(term, row, col, color, "%s", hex);
        }
    }

    if (s_breach.result == 0) {
        i32 bar = (i32)(s_breach.timer / BREACH_TIME * 30.0f);
        u8 tcol = s_breach.timer < 8.0f ? TC_RED : TC_AMBER;
        grid_text(term, 5, 2, tcol, "TRACE");
        for (i32 i = 0; i < 30; i++) {
            term->glyphs[5][10 + i] = i < bar ? '=' : '.';
            term->colors[5][10 + i] = i < bar ? tcol : TC_DIM;
        }
        grid_text(term, TERM_ROWS - 1, 1, TC_DIM,
                  s_breach.axis == 0 ? "< > MOVE   ENTER SELECT   Q ABORT"
                                     : "^ v MOVE   ENTER SELECT   Q ABORT");
    } else if (s_breach.result == 1) {
        if (fmodf(term->blink, 0.6f) < 0.4f) {
            grid_text(term, TERM_ROWS - 4, TERM_COLS / 2 - 8, TC_BRIGHT, "ACCESS GRANTED");
        }
        grid_text(term, TERM_ROWS - 1, 1, TC_DIM, "LOCK DISENGAGED   ENTER / Q TO EXIT");
    } else {
        grid_text(term, TERM_ROWS - 4, TERM_COLS / 2 - 12, TC_RED,
                  "TRACE DETECTED -- LOCKED OUT");
        grid_text(term, TERM_ROWS - 1, 1, TC_DIM, "RUN BREACH TO RETRY   ENTER / Q TO EXIT");
    }
}

static void term_launch_exe(Terminal* term, const FsNode* node)
{
    if (node->corrupted) {
        term_print(term, "PROGRAM DAMAGED. CANNOT EXECUTE.\n");
        return;
    }
    i32 exe = node->exe;
    switch (exe) {
    case FS_EXE_STATUS:
        if (term->bus_tower) {
            term_print(term, "BUS FEED IS RELAY NODE. NO VEHICLE DIAGNOSTICS.\n");
        } else if (term->bus_state != 2) {
            term_print(term, term->bus_state == 1 ? "BUS PORT NOT INITIALIZED. RUN LINK.\n"
                                                  : "NO VEHICLE BUS CABLE.\n");
        } else {
            term->mode = TERM_STATUS;
        }
        break;
    case FS_EXE_MAP:
        if (term->coax_state != 2 || term->antenna_tier < 0) {
            if (term->coax_state == 2 && term->coax_camera) {
                term_print(term, "COAX FEED IS CAMERA. NO SURVEY ANTENNA.\n");
            } else {
                term_print(term, term->coax_state == 1 ? "COAX PORT NOT INITIALIZED. RUN LINK.\n"
                                                       : "NO ANTENNA FEED.\n");
            }
        } else {
            term->mode = TERM_MAP;
            term->map_materialize = 0.0f;
            term->map_auto = 1;
            term->map_zoom = 1.0f;
            term->map_wide = 0;
            term->map_refresh = 0.0f;
            map_sync_file(term);
            s_lidar_count = 0;
            term->map_downloading = term->bus_state == 2 && term->bus_tower
                                    && term->tower_breached && !term->tower_download_done;
            term->map_dl_t = 0.0f;
        }
        break;
    case FS_EXE_LINK:
        term->mode = TERM_LINK;
        term->link_anim_port = -1;
        term->link_anim_t = 0.0f;
        break;
    case FS_EXE_COMMS:
        term->mode = TERM_COMMS;
        term->comms_open = 0;
        term->comms_page = 0;
        term->comms_status[0] = 0;
        term->comms_status_until = 0.0f;
        term->comms_mat = 10.0f;
        comms_enter_phase(term, COMMS_PHASE_CONNECT);
        break;
    case FS_EXE_AV:
        term_av_scan(term);
        break;
    case FS_EXE_VIDEO:
        if (term->coax_state == 2 && term->coax_camera) {
            term->mode = TERM_VIDEO;
            term->map_materialize = 0.0f;
        } else if (term->coax_state == 1) {
            term_print(term, "COAX PORT NOT INITIALIZED. RUN LINK.\n");
        } else if (term->coax_state == 2) {
            term_print(term, "COAX FEED IS ANTENNA. NO VIDEO SOURCE.\n");
        } else {
            term_print(term, "NO VIDEO SOURCE ON COAX.\n");
        }
        break;
    case FS_EXE_TOY:
        term_printf(term, "%s\n", node->run_text ? node->run_text : "OUT OF MEMORY");
        break;
    case FS_EXE_BREACH:
        if (!(term->bus_state >= 1 && term->bus_tower)) {
            term_print(term, "NO SECURED PORT ON BUS. NOTHING TO BREACH.\n");
        } else if (term->tower_breached) {
            term_print(term, "PORT ALREADY OPEN.\n");
        } else {
            term->mode = TERM_BREACH;
            breach_start();
        }
        break;
    case FS_EXE_GATE:
        term_print(term, "GATE ACTUATOR: NO BARRIER WIRED TO THIS NODE.\n");
        break;
    case FS_EXE_DEV:
        term->mode = TERM_DEV;
        break;
    case FS_EXE_TAPES:
        term->mode = TERM_TAPES;
        term->tapes_sel = 0;
        term->tapes_write_track = -1;
        term->tapes_dl_track = -1;
        term->tapes_prompt_track = -1;
        term->tapes_status[0] = 0;
        term->tapes_status_until = 0.0f;
        break;
    default:
        term_print(term, "PROGRAM DAMAGED. CANNOT EXECUTE.\n");
        break;
    }
    if (node->infected) {
        virus_infect();
    }
}

static b32 term_try_exe(Terminal* term, const char* name)
{
    char full[TERM_INPUT_MAX + 8];
    u32 len = (u32)strlen(name);
    if (len > 4 && strcmp(name + len - 4, ".EXE") == 0) {
        snprintf(full, sizeof(full), "%s", name);
    } else {
        snprintf(full, sizeof(full), "%s.EXE", name);
    }
    FsRef cwd = { term->cwd_drive, term->cwd_node };
    FsRef ref = fs_resolve(&s_fs, cwd, full);
    if (!fs_ref_valid(&s_fs, ref) || fs_node(&s_fs, ref)->is_dir) {
        ref = fs_resolve(&s_fs, fs_root(FS_DRIVE_A), full);
    }
    if (!fs_ref_valid(&s_fs, ref) || fs_node(&s_fs, ref)->is_dir) {
        return 0;
    }
    term_launch_exe(term, fs_node(&s_fs, ref));
    return 1;
}

static void term_run_command(Terminal* term, const char* cmd)
{
    if (term->format_drive >= 0) {
        i32 drive = term->format_drive;
        term->format_drive = -1;
        if (cmd[0] == 'Y' && cmd[1] == 0) {
            fs_format(&s_fs, drive, 0);
            if (drive == FS_DRIVE_A) {
                virus_cure();
            }
            if (term->cwd_drive == drive) {
                term->cwd_node = 0;
            }
            term_printf(term, "FORMAT COMPLETE.\n%u BYTES FREE\n",
                        fs_free_bytes(&s_fs, drive));
        } else {
            term_print(term, "FORMAT ABORTED.\n");
        }
        return;
    }
    if (!fs_drive_mounted(&s_fs, term->cwd_drive)) {
        term->cwd_drive = FS_DRIVE_A;
        term->cwd_node = 0;
    }
    char tok[3][TERM_INPUT_MAX + 1];
    i32 ntok = term_tokenize(cmd, tok);
    if (ntok == 0) {
        return;
    }
    if (strlen(tok[0]) == 2 && tok[0][1] == ':' && ntok == 1) {
        i32 drive = fs_path_drive(tok[0]);
        if (drive == -2) {
            term_print(term, "INVALID DRIVE SPECIFICATION\n");
        } else if (!fs_drive_mounted(&s_fs, drive)) {
            term_printf(term, "DRIVE %c: NOT READY\n", 'A' + drive);
        } else {
            term->cwd_drive = drive;
            term->cwd_node = 0;
        }
        return;
    }
    if (cmd_is(tok[0], "HELP")) {
        term_print(term,
                   "DR-OS 2.2 SHELL COMMANDS:\n"
                   "  LS [PATH]       list directory\n"
                   "  CD PATH         change directory\n"
                   "  TYPE FILE       display file contents\n"
                   "  VIEW FILE       display image file\n"
                   "  COPY SRC DST    copy file\n"
                   "  MOVE SRC DST    move file\n"
                   "  MKDIR PATH      create directory\n"
                   "  RMDIR PATH      remove empty directory\n"
                   "  DEL FILE        delete file\n"
                   "  RUN FILE        run program (or just type its name)\n"
                   "  CHKDSK [X:]     disk space report\n"
                   "  FORMAT X:       erase a drive\n"
                   "  MISSION         current objective\n"
                   "  CLS / VER / OFF\n"
                   "PROGRAMS ARE .EXE FILES ON DISK. USE LS TO SEE THEM.\n");
    } else if (cmd_is(tok[0], "LS")) {
        shell_dir(term, ntok > 1 ? tok[1] : "");
    } else if (cmd_is(tok[0], "CD") || cmd_is(tok[0], "CHDIR")) {
        shell_cd(term, ntok > 1 ? tok[1] : "");
    } else if (cmd_is(tok[0], "TYPE")) {
        if (ntok < 2) {
            term_print(term, "SYNTAX: TYPE FILE\n");
        } else {
            shell_type(term, tok[1]);
        }
    } else if (cmd_is(tok[0], "VIEW")) {
        if (ntok < 2) {
            term_print(term, "SYNTAX: VIEW FILE\n");
        } else {
            shell_view(term, tok[1]);
        }
    } else if (cmd_is(tok[0], "COPY")) {
        if (ntok < 3) {
            term_print(term, "SYNTAX: COPY SRC DST\n");
        } else {
            shell_copy(term, tok[1], tok[2]);
        }
    } else if (cmd_is(tok[0], "DEL") || cmd_is(tok[0], "ERASE")) {
        if (ntok < 2) {
            term_print(term, "SYNTAX: DEL FILE\n");
        } else {
            shell_del(term, tok[1]);
        }
    } else if (cmd_is(tok[0], "MOVE") || cmd_is(tok[0], "MV")) {
        if (ntok < 3) {
            term_print(term, "SYNTAX: MOVE SRC DST\n");
        } else {
            shell_move(term, tok[1], tok[2]);
        }
    } else if (cmd_is(tok[0], "MKDIR") || cmd_is(tok[0], "MD")) {
        if (ntok < 2) {
            term_print(term, "SYNTAX: MKDIR PATH\n");
        } else {
            shell_mkdir(term, tok[1]);
        }
    } else if (cmd_is(tok[0], "RMDIR") || cmd_is(tok[0], "RD")) {
        if (ntok < 2) {
            term_print(term, "SYNTAX: RMDIR PATH\n");
        } else {
            shell_rmdir(term, tok[1]);
        }
    } else if (cmd_is(tok[0], "CHKDSK")) {
        shell_chkdsk(term, ntok > 1 ? tok[1] : "");
    } else if (cmd_is(tok[0], "FORMAT")) {
        if (ntok < 2) {
            term_print(term, "SYNTAX: FORMAT X:\n");
        } else {
            shell_format(term, tok[1]);
        }
    } else if (cmd_is(tok[0], "RUN")) {
        if (ntok < 2) {
            term_print(term, "SYNTAX: RUN FILE\n");
        } else if (!term_try_exe(term, tok[1])) {
            term_print(term, "FILE NOT FOUND\n");
        }
    } else if (cmd_is(tok[0], "MISSION")) {
        term->mode_timer = -1.0f;
    } else if (cmd_is(tok[0], "CLS")) {
        term->line_count = 0;
        term->line_head = 0;
        term->out_line[0] = 0;
        term->out_len = 0;
    } else if (cmd_is(tok[0], "VER")) {
        term_print(term, "DR-OS 2.2 (REDLINE SYSTEMS 1988)\n");
    } else if (cmd_is(tok[0], "OFF")) {
        term_print(term, "SYSTEM HALTED.\n");
        term->wants_off = 1;
    } else if (!term_try_exe(term, tok[0])) {
        term_print(term, "Bad command or file name\n");
    }
}

void terminal_key_char(Terminal* term, char c)
{
    if (c < 32 || c > 126) {
        return;
    }
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    if (term->mode == TERM_COMMS) {
        comms_key_char(term, c);
        return;
    }
    if (term->mode == TERM_LINK) {
        if (term->link_anim_port < 0) {
            if (c == '1' && term->coax_state == 1) {
                term->link_anim_port = 0;
                term->link_anim_t = 0.0f;
            } else if (c == '2' && term->bus_state == 1) {
                term->link_anim_port = 1;
                term->link_anim_t = 0.0f;
            }
        }
        return;
    }
    if (term->mode == TERM_MAP) {
        if (c == 'M' && !term->map_downloading) {
            term->map_wide = !term->map_wide;
            term->map_materialize = 0.0f;
            term->map_refresh = 0.0f;
            term->map_zoom = 1.0f;
            term->map_auto = 1;
            s_lidar_count = 0;
            term->click_pending = 1;
        }
        return;
    }
    if (term->mode == TERM_DEV) {
        if (c == '1') {
            dev_request_time(term, 0.27f);
        } else if (c == '2') {
            dev_request_time(term, 0.50f);
        } else if (c == '3') {
            dev_request_time(term, 0.72f);
        } else if (c == '4') {
            dev_request_time(term, 0.0f);
        } else if (c == 'T') {
            term->dev_warp = !term->dev_warp;
            term->click_pending = 1;
        } else if (c >= '5' && c <= '8') {
            term->dev_weather_request = 1 + (c - '5');
            term->dev_wmode = c - '5';
            term->click_pending = 1;
        }
        return;
    }
    if (term->mode == TERM_TAPES) {
        if (term->tapes_prompt_track >= 0
            && term->tapes_dest_len + 1 < sizeof(term->tapes_dest)) {
            memmove(&term->tapes_dest[term->tapes_dest_cursor + 1],
                    &term->tapes_dest[term->tapes_dest_cursor],
                    term->tapes_dest_len - term->tapes_dest_cursor + 1);
            term->tapes_dest[term->tapes_dest_cursor++] = c;
            term->tapes_dest_len++;
            term->click_pending = 1;
        }
        return;
    }
    if (term->mode != TERM_SHELL) {
        return;
    }
    if (term->input_len < TERM_INPUT_MAX) {
        memmove(&term->input[term->input_cursor + 1], &term->input[term->input_cursor],
                term->input_len - term->input_cursor + 1);
        term->input[term->input_cursor++] = c;
        term->input_len++;
        term->click_pending = 1;
    }
}

void terminal_key_special(Terminal* term, i32 key)
{
    if (term->mode == TERM_COMMS) {
        if (key == KEY_ENTER) {
            comms_key_enter(term);
        } else if (key == KEY_Q || key == KEY_ESCAPE) {
            if (term->comms_phase == COMMS_PHASE_BRIEF) {
                comms_enter_phase(term, COMMS_PHASE_READ);
                term->comms_mat = 10.0f;
            } else if (term->comms_phase == COMMS_PHASE_READ) {
                comms_enter_phase(term, COMMS_PHASE_INBOX);
                term->comms_mat = 10.0f;
            } else {
                term->mode = TERM_SHELL;
            }
        }
        return;
    }
    if (term->mode == TERM_BREACH) {
        if (key == KEY_Q || key == KEY_ESCAPE) {
            term->mode = TERM_SHELL;
        } else if (key == KEY_ENTER) {
            if (s_breach.result != 0) {
                term->mode = TERM_SHELL;
                if (s_breach.result == 1) {
                    term_print(term, "LOCK DISENGAGED. PORT OPEN. RUN LINK.\n");
                }
            } else {
                breach_pick(term);
            }
        } else if (key == KEY_LEFT || key == KEY_UP) {
            breach_move(-1);
        } else if (key == KEY_RIGHT || key == KEY_DOWN) {
            breach_move(1);
        }
        return;
    }
    if (term->mode == TERM_MAP && term->map_downloading) {
        if (key == KEY_Q || key == KEY_ESCAPE) {
            term->map_downloading = 0;
            term->mode = TERM_SHELL;
            term_print(term, "TRANSFER ABORTED.\n");
        }
        return;
    }
    if (term->mode == TERM_DEV) {
        if (key == KEY_LEFT) {
            dev_request_time(term, term->dev_tod - 1.0f / 48.0f);
        } else if (key == KEY_RIGHT) {
            dev_request_time(term, term->dev_tod + 1.0f / 48.0f);
        } else if (key == KEY_ENTER || key == KEY_Q || key == KEY_ESCAPE) {
            term->mode = TERM_SHELL;
        }
        return;
    }
    if (term->mode == TERM_TAPES) {
        if (term->tapes_prompt_track >= 0) {
            if (key == KEY_BACKSPACE) {
                if (term->tapes_dest_cursor > 0) {
                    memmove(&term->tapes_dest[term->tapes_dest_cursor - 1],
                            &term->tapes_dest[term->tapes_dest_cursor],
                            term->tapes_dest_len - term->tapes_dest_cursor + 1);
                    term->tapes_dest_cursor--;
                    term->tapes_dest_len--;
                    term->click_pending = 1;
                }
            } else if (key == KEY_DELETE) {
                if (term->tapes_dest_cursor < term->tapes_dest_len) {
                    memmove(&term->tapes_dest[term->tapes_dest_cursor],
                            &term->tapes_dest[term->tapes_dest_cursor + 1],
                            term->tapes_dest_len - term->tapes_dest_cursor);
                    term->tapes_dest_len--;
                    term->click_pending = 1;
                }
            } else if (key == KEY_LEFT) {
                if (term->tapes_dest_cursor > 0) {
                    term->tapes_dest_cursor--;
                }
            } else if (key == KEY_RIGHT) {
                if (term->tapes_dest_cursor < term->tapes_dest_len) {
                    term->tapes_dest_cursor++;
                }
            } else if (key == KEY_HOME) {
                term->tapes_dest_cursor = 0;
            } else if (key == KEY_END) {
                term->tapes_dest_cursor = term->tapes_dest_len;
            } else if (key == KEY_ENTER) {
                if (term->tapes_dest_len == 0) {
                    term->tapes_prompt_track = -1;
                } else if (!tapes_relay_linked(term)) {
                    tapes_status_set(term, "RELAY LINK LOST");
                    term->tapes_prompt_track = -1;
                } else if (tapes_resolve_dest(term, term->tapes_prompt_track)) {
                    term->tapes_dl_track = term->tapes_prompt_track;
                    term->tapes_prompt_track = -1;
                    term->tapes_dl_t = 0.0f;
                    term->click_pending = 1;
                }
            }
            return;
        }
        if (term->tapes_dl_track >= 0 || term->tapes_write_track >= 0) {
            if (key == KEY_Q || key == KEY_ESCAPE) {
                if (term->tapes_dl_track >= 0) {
                    term->tapes_dl_track = -1;
                    tapes_status_set(term, "TRANSFER ABORTED");
                }
                if (term->tapes_write_track >= 0) {
                    term->tapes_write_track = -1;
                    tapes_status_set(term, "WRITE ABORTED");
                }
            }
            return;
        }
        if (key == KEY_UP) {
            if (term->tapes_sel > 0) {
                term->tapes_sel--;
                term->click_pending = 1;
            }
        } else if (key == KEY_DOWN) {
            if (term->tapes_sel + 1 < term->tapes_count) {
                term->tapes_sel++;
                term->click_pending = 1;
            }
        } else if (key == KEY_ENTER) {
            tapes_enter(term);
        } else if (key == KEY_Q || key == KEY_ESCAPE) {
            term->mode = TERM_SHELL;
        }
        return;
    }
    if (term->mode == TERM_STATUS || term->mode == TERM_MAP || term->mode == TERM_LINK
        || term->mode == TERM_VIEW || term->mode == TERM_VIDEO) {
        if (key == KEY_ENTER || key == KEY_Q || key == KEY_ESCAPE) {
            term->mode = TERM_SHELL;
        }
        return;
    }
    if (term->mode != TERM_SHELL) {
        return;
    }
    if (key == KEY_ENTER) {
        term_submit(term);
    } else if (key == KEY_BACKSPACE) {
        if (term->input_cursor > 0) {
            memmove(&term->input[term->input_cursor - 1], &term->input[term->input_cursor],
                    term->input_len - term->input_cursor + 1);
            term->input_cursor--;
            term->input_len--;
        }
    } else if (key == KEY_DELETE) {
        if (term->input_cursor < term->input_len) {
            memmove(&term->input[term->input_cursor], &term->input[term->input_cursor + 1],
                    term->input_len - term->input_cursor);
            term->input_len--;
        }
    } else if (key == KEY_LEFT) {
        if (term->input_cursor > 0) {
            term->input_cursor--;
        }
    } else if (key == KEY_RIGHT) {
        if (term->input_cursor < term->input_len) {
            term->input_cursor++;
        }
    } else if (key == KEY_HOME) {
        term->input_cursor = 0;
    } else if (key == KEY_END) {
        term->input_cursor = term->input_len;
    } else if (key == KEY_UP && term->history_count > 0) {
        if (term->history_pos < (i32)(term->history_count < TERM_HISTORY
                                      ? term->history_count : TERM_HISTORY) - 1) {
            term->history_pos++;
        }
        u32 slot = (term->history_count - 1 - (u32)term->history_pos) % TERM_HISTORY;
        snprintf(term->input, sizeof(term->input), "%s", term->history[slot]);
        term->input_len = (u32)strlen(term->input);
        term->input_cursor = term->input_len;
    } else if (key == KEY_DOWN) {
        term->history_pos = -1;
        term->input[0] = 0;
        term->input_len = 0;
        term->input_cursor = 0;
    }
}

void terminal_power(Terminal* term, b32 on)
{
    Terminal zero = {0};
    *term = zero;
    term->powered = on;
    term->history_pos = -1;
    term->format_drive = -1;
    term->tapes_write_track = -1;
    term->tapes_dl_track = -1;
    term->tapes_prompt_track = -1;
    term->map_zoom = 1.0f;
    if (on) {
        term->mode = TERM_BOOT;
        s_gpu.clear_pending = 1;
    }
}

static void grid_clear(Terminal* term)
{
    for (i32 row = 0; row < TERM_ROWS; row++) {
        for (i32 col = 0; col < TERM_COLS; col++) {
            term->glyphs[row][col] = ' ';
        }
    }
    memset(term->colors, TC_GREEN, sizeof(term->colors));
}

static void grid_text(Terminal* term, i32 row, i32 col, u8 color, const char* fmt, ...)
{
    char buf[TERM_COLS * 3 + 1];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    const char* p = buf;
    i32 c = col;
    u32 cp;
    while ((cp = utf8_next(&p)) != 0 && c < TERM_COLS) {
        if (cp > 0xFFFDu) {
            cp = 0xFFFDu;
        }
        b32 wide = term_font_cp_wide(cp);
        if (row >= 0 && row < TERM_ROWS && c >= 0) {
            term->glyphs[row][c] = (u16)cp;
            term->colors[row][c] = color;
            if (wide && c + 1 < TERM_COLS) {
                term->glyphs[row][c + 1] = (u16)TERM_FONT_WIDE_CONT;
                term->colors[row][c + 1] = color;
            }
        }
        c += wide ? 2 : 1;
    }
}

static void grid_bar(Terminal* term, i32 row, i32 col, i32 width, f32 frac, u8 color)
{
    grid_text(term, row, col, TC_DIM, "[");
    for (i32 i = 0; i < width; i++) {
        b32 fill = (f32)i / (f32)width < frac;
        term->glyphs[row][col + 1 + i] = fill ? '#' : '.';
        term->colors[row][col + 1 + i] = fill ? color : TC_DIM;
    }
    grid_text(term, row, col + 1 + width, TC_DIM, "]");
}

static void grid_title(Terminal* term, const char* title)
{
    for (i32 c = 0; c < TERM_COLS; c++) {
        term->colors[0][c] = TC_BG;
    }
    grid_text(term, 0, 1, TC_BG, "%s", title);
    grid_text(term, 0, TERM_COLS - 8, TC_BG, "Q: BACK");
}

static void draw_boot(Terminal* term)
{
    grid_clear(term);
    f32 t = term->mode_timer;
    grid_text(term, 3, 26, TC_BRIGHT, "D R - O S   2 . 2");
    grid_text(term, 5, 22, TC_DIM, "REDLINE SYSTEMS  (C) 1988");
    if (t > 1.0f) {
        grid_text(term, 8, 6, TC_GREEN, "MEMORY TEST ........ 640K OK");
    }
    if (t > 1.7f) {
        grid_text(term, 9, 6, TC_GREEN, "BUS 12V ............ OK");
    }
    if (t > 2.3f) {
        grid_text(term, 10, 6, TC_GREEN, "SENSOR LOOM ........ OK");
    }
    if (t > 2.9f) {
        grid_text(term, 11, 6, TC_GREEN, "DRIVE A: ........... 354K FIXED, %uK FREE",
                  fs_free_bytes(&s_fs, FS_DRIVE_A) / 1024);
    }
    if (t > 3.5f) {
        grid_text(term, 12, 6, TC_AMBER, "UPLINK ............. NO CARRIER");
    }
    if (t > 4.1f) {
        grid_text(term, 14, 6, TC_BRIGHT, "READY. TYPE HELP FOR COMMANDS.");
    }
}

static void draw_shell(Terminal* term)
{
    grid_clear(term);
    i32 rows_for_log = TERM_ROWS - 1;
    i32 total = (i32)term->line_count + (term->out_len > 0 ? 1 : 0);
    i32 first = total > rows_for_log ? total - rows_for_log : 0;
    i32 row = 0;
    for (i32 i = first; i < total && row < rows_for_log; i++, row++) {
        const char* text;
        if (i < (i32)term->line_count) {
            u32 slot = (term->line_head + TERM_LINES - term->line_count + (u32)i) % TERM_LINES;
            text = term->lines[slot];
        } else {
            text = term->out_line;
        }
        grid_text(term, row, 0, TC_GREEN, "%s", text);
    }
    char prompt[TERM_PROMPT_MAX];
    term_prompt(term, prompt, sizeof(prompt));
    grid_text(term, row, 0, TC_BRIGHT, "%s%s", prompt, term->input);
    if (term_reveal_idle(term) && fmodf(term->blink, 1.06f) < 0.53f) {
        i32 ccol = (i32)strlen(prompt) + (i32)term->input_cursor;
        if (ccol > TERM_COLS - 1) {
            ccol = TERM_COLS - 1;
        }
        term->glyphs[row][ccol] = 127;
        term->colors[row][ccol] = TC_BRIGHT;
    }
}

static Vec3 cond_color(f32 cond, f32 blink)
{
    if (cond > 0.6f) {
        return v3(0.24f, 0.71f, 0.32f);
    }
    if (cond > 0.3f) {
        return v3(0.86f, 0.67f, 0.24f);
    }
    return vec3_scale(v3(0.98f, 0.33f, 0.25f), 0.65f + 0.35f * sinf(blink * 5.0f));
}

static void wire_push(const GpuMesh* mesh, Mat4 model, Vec3 color)
{
    if (mesh && mesh->loaded && s_wire_count < 12) {
        s_wires[s_wire_count].mesh = mesh;
        s_wires[s_wire_count].model = model;
        s_wires[s_wire_count].color = color;
        s_wire_count++;
    }
}

static void build_status_wires(Terminal* term, const TermView* view)
{
    const CarSys* sys = view->sys;
    const Vehicle* veh = view->veh;
    Vec3 one = v3(1.0f, 1.0f, 1.0f);
    Mat4 ident = mat4_trs(vec3_zero(), quat_identity(), one);

    s_wire_count = 0;
    wire_push(asset_mesh(veh->cfg.body_mesh), ident, v3(0.09f, 0.28f, 0.12f));

    for (u32 i = 0; i < VEHICLE_WHEEL_COUNT && i < 4; i++) {
        const PartSlot* slot = &sys->parts[PART_TIRE_FL + i];
        if (!slot->installed) {
            continue;
        }
        Quat q = veh->cfg.wheels[i].pos.x > 0.0f
                 ? quat_from_axis_angle(v3(0.0f, 1.0f, 0.0f), PI32) : quat_identity();
        wire_push(asset_mesh(veh->cfg.wheel_mesh),
                  mat4_trs(veh->cfg.wheels[i].pos, q, one),
                  cond_color(slot->condition, term->blink));
    }

    static const PartKind bay[4] = { PART_ENGINE, PART_BATTERY, PART_ALTERNATOR, PART_RADIATOR };
    for (u32 i = 0; i < 4; i++) {
        const PartSlot* slot = &sys->parts[bay[i]];
        const PartDef* def = part_def(bay[i]);
        if (!slot->installed || !def->mesh[0]) {
            continue;
        }
        wire_push(asset_mesh(def->mesh),
                  mat4_trs(def->socket_pos, quat_identity(), one),
                  cond_color(slot->condition, term->blink));
    }
    if (sys->parts[PART_COMPUTER].installed) {
        wire_push(asset_mesh(part_def(PART_COMPUTER)->mesh),
                  mat4_trs(part_def(PART_COMPUTER)->socket_pos, part_computer_rest_rot(), one),
                  cond_color(sys->parts[PART_COMPUTER].condition, term->blink));
    }
    if (sys->parts[PART_ANTENNA].installed) {
        static const char* ant_meshes[3] = { "antenna_whip", "antenna_std", "antenna_array" };
        i32 variant = sys->parts[PART_ANTENNA].variant;
        if (variant < 0 || variant > 2) {
            variant = 1;
        }
        wire_push(asset_mesh(ant_meshes[variant]),
                  mat4_trs(part_def(PART_ANTENNA)->socket_pos, quat_identity(), one),
                  cond_color(sys->parts[PART_ANTENNA].condition, term->blink));
    }

    f32 pitch = 0.30f;
    f32 dist = 8.8f;
    Vec3 eye = v3(sinf(term->status_spin) * cosf(pitch) * dist,
                  sinf(pitch) * dist,
                  cosf(term->status_spin) * cosf(pitch) * dist);
    Mat4 vmat = mat4_look_at(eye, vec3_zero(), v3(0.0f, 1.0f, 0.0f));
    Mat4 proj = mat4_perspective(30.0f * PI32 / 180.0f,
                                 (f32)WIRE_VP_W / (f32)WIRE_VP_H, 0.1f, 60.0f);
    term->vp3d = mat4_mul(proj, vmat);
}

static void draw_status(Terminal* term, const TermView* view)
{
    grid_clear(term);
    const CarSys* sys = view->sys;
    grid_title(term, "VEHICLE DIAGNOSTICS");

    grid_text(term, 2, 2, TC_GREEN, "ENGINE %-8s", sys->engine_on ? "RUNNING" : "OFF");
    grid_text(term, 2, 18, TC_GREEN, "RPM %4.0f", (f64)virus_lie(term, view->rpm, 1.0f));
    grid_text(term, 2, 29, TC_GREEN, "SPEED %3.0f KM/H",
              (f64)virus_lie(term, view->speed_kmh, 2.0f));

    static const PartKind rows[6] = {
        PART_ENGINE, PART_BATTERY, PART_ALTERNATOR, PART_RADIATOR, PART_FUEL_TANK, PART_HEADLIGHTS,
    };
    for (i32 i = 0; i < 6; i++) {
        const PartSlot* slot = &sys->parts[rows[i]];
        f32 cond = slot->installed ? slot->condition : 0.0f;
        cond = f_clamp01(virus_lie(term, cond, 3.0f + (f32)i));
        u8 color = cond > 0.6f ? TC_GREEN : (cond > 0.3f ? TC_AMBER : TC_RED);
        grid_text(term, 4 + i, 2, TC_GREEN, "%-11s", part_def(rows[i])->name);
        grid_bar(term, 4 + i, 14, 20, cond, color);
        grid_text(term, 4 + i, 37, color, slot->installed ? "%3.0f%%" : "OUT", (f64)(cond * 100.0f));
    }
    for (i32 i = 0; i < 4; i++) {
        const PartSlot* slot = &sys->parts[PART_TIRE_FL + i];
        f32 cond = slot->installed ? slot->condition : 0.0f;
        cond = f_clamp01(virus_lie(term, cond, 9.0f + (f32)i));
        u8 color = cond > 0.5f ? TC_GREEN : (cond > 0.2f ? TC_AMBER : TC_RED);
        static const char* names[4] = { "FL", "FR", "RL", "RR" };
        grid_text(term, 11 + i / 2, 2 + (i % 2) * 20, color, "TIRE %s %3.0f%%",
                  names[i], (f64)(cond * 100.0f));
    }
    build_status_wires(term, view);

    f32 fuel = f_clamp01(virus_lie(term, sys->fluids.fuel, 13.0f));
    f32 oil = f_clamp01(virus_lie(term, sys->fluids.oil, 14.0f));
    grid_text(term, 13, 2, TC_GREEN, "FUEL");
    grid_bar(term, 13, 14, 20, fuel, fuel > 0.2f ? TC_GREEN : TC_RED);
    grid_text(term, 14, 2, TC_GREEN, "OIL");
    grid_bar(term, 14, 14, 20, oil, oil > 0.3f ? TC_GREEN : TC_RED);
    f32 coolant = virus_lie(term, sys->fluids.coolant_temp, 15.0f);
    b32 hot = coolant > COOLANT_OVERHEAT_C;
    grid_text(term, 15, 2, TC_GREEN, "COOLANT");
    grid_text(term, 15, 15, hot ? TC_RED : TC_GREEN, "%3.0f C %s",
              (f64)coolant, hot ? "OVERHEAT" : "");
    f32 charge = f_clamp01(virus_lie(term, sys->elec.battery_charge, 16.0f));
    grid_text(term, 17, 2, TC_GREEN, "BATTERY");
    grid_bar(term, 17, 14, 20, charge, charge > 0.15f ? TC_GREEN : TC_RED);
    grid_text(term, 18, 2, TC_DIM, "ALT %4.1fA   DRAW %4.1fA   %s",
              (f64)virus_lie(term, sys->elec.alternator_amps, 17.0f),
              (f64)virus_lie(term, sys->elec.draw_amps, 18.0f),
              sys->handbrake_latched ? "PARK BRAKE SET" : "");
}

static f32 ease01(f32 t)
{
    t = f_clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

static void lidar_rebuild(const TermView* view)
{
    const Heightfield* hf = &view->terrain->hf;
    f32 cx = floorf(view->car_pos.x / LIDAR_SPACING + 0.5f) * LIDAR_SPACING;
    f32 cz = floorf(view->car_pos.z / LIDAR_SPACING + 0.5f) * LIDAR_SPACING;
    s_lidar_center = v3(cx, 0.0f, cz);
    u32 n = 0;
    f32 half = (f32)(LIDAR_N - 1) * 0.5f;
    for (i32 r = 0; r < LIDAR_N; r++) {
        for (i32 c = 0; c < LIDAR_N; c++) {
            f32 wx = cx + ((f32)c - half) * LIDAR_SPACING;
            f32 wz = cz + ((f32)r - half) * LIDAR_SPACING;
            s_lidar_pts[n][0] = wx;
            s_lidar_pts[n][1] = heightfield_sample(hf, wx, wz) + 0.4f;
            s_lidar_pts[n][2] = wz;
            s_lidar_pts[n][3] = s_lidar_tier >= 1
                                && terrain_road_amount(view->terrain, wx, wz) > 0.4f ? 1.0f : 0.0f;
            n++;
        }
    }
    Vec3 marks[2] = { view->garage_pos, view->mission_pos };
    f32 kinds[2] = { 2.0f, 3.0f };
    b32 show[2];
    show[0] = s_lidar_tier >= 1;
    show[1] = s_lidar_tier >= 1 && (view->mission_stage == 1 || view->mission_stage == 2);
    for (u32 m = 0; m < 2; m++) {
        if (!show[m]) {
            continue;
        }
        f32 ground = heightfield_sample(hf, marks[m].x, marks[m].z);
        for (i32 i = 0; i < 14 && n < LIDAR_MAX_POINTS; i++) {
            s_lidar_pts[n][0] = marks[m].x;
            s_lidar_pts[n][1] = ground + 2.0f + (f32)i * 3.4f;
            s_lidar_pts[n][2] = marks[m].z;
            s_lidar_pts[n][3] = kinds[m];
            n++;
        }
    }
    s_lidar_count = n;
    s_lidar_dirty = 1;
}

static void lidar_add_marker_column(const Heightfield* hf, Vec3 mark, f32 kind, u32* n)
{
    f32 ground = heightfield_sample(hf, mark.x, mark.z);
    for (i32 i = 0; i < 14 && *n < LIDAR_MAX_POINTS; i++) {
        s_lidar_pts[*n][0] = mark.x;
        s_lidar_pts[*n][1] = ground + 2.0f + (f32)i * 3.4f;
        s_lidar_pts[*n][2] = mark.z;
        s_lidar_pts[*n][3] = kind;
        (*n)++;
    }
}

static void lidar_rebuild_wide(const Terminal* term, const TermView* view)
{
    const Heightfield* hf = &view->terrain->hf;
    u32 cells = mapdata_count();
    if (cells > s_map_saved_count) {
        cells = s_map_saved_count;
    }
    u32 n = 0;
    u32 noise = 0x51ED2701u;
    for (u32 i = 0; i < cells && n + 9 < LIDAR_MAX_POINTS - 64; i++) {
        f32 cx, cz, cs;
        if (!mapdata_cell(i, &cx, &cz, &cs)) {
            continue;
        }
        f32 sub = cs / 3.0f;
        for (i32 sr = -1; sr <= 1; sr++) {
            for (i32 sc = -1; sc <= 1; sc++) {
                f32 wx = cx + (f32)sc * sub;
                f32 wz = cz + (f32)sr * sub;
                f32 wy = heightfield_sample(hf, wx, wz) + 0.4f;
                f32 kind = s_lidar_tier >= 1
                           && terrain_road_amount(view->terrain, wx, wz) > 0.4f ? 1.0f : 0.0f;
                if (term->map_corrupt) {
                    noise = noise * 1664525u + 1013904223u;
                    wy += (f32)((noise >> 8) % 61) - 30.0f;
                    kind = (f32)((noise >> 20) % 4);
                }
                s_lidar_pts[n][0] = wx;
                s_lidar_pts[n][1] = wy;
                s_lidar_pts[n][2] = wz;
                s_lidar_pts[n][3] = kind;
                n++;
            }
        }
    }
    Vec3 marks[2] = { view->garage_pos, view->mission_pos };
    f32 kinds[2] = { 2.0f, 3.0f };
    b32 show[2];
    show[0] = s_lidar_tier >= 1;
    show[1] = s_lidar_tier >= 1 && (view->mission_stage == 1 || view->mission_stage == 2);
    for (u32 m = 0; m < 2; m++) {
        if (show[m]) {
            lidar_add_marker_column(hf, marks[m], kinds[m], &n);
        }
    }
    s_lidar_count = n;
    s_lidar_dirty = 1;
}

static b32 map_project(const Terminal* term, Vec3 world, i32* out_row, i32* out_col)
{
    const f32* m = term->vp3d.m;
    f32 cx = m[0] * world.x + m[4] * world.y + m[8] * world.z + m[12];
    f32 cy = m[1] * world.x + m[5] * world.y + m[9] * world.z + m[13];
    f32 cw = m[3] * world.x + m[7] * world.y + m[11] * world.z + m[15];
    if (cw < 0.1f) {
        return 0;
    }
    f32 sx = (cx / cw * 0.5f + 0.5f) * (f32)TERM_TEX_W;
    f32 sy = (1.0f - (cy / cw * 0.5f + 0.5f)) * (f32)TERM_TEX_H;
    i32 col = (i32)((sx - (f32)TERM_ORIGIN_X) / (f32)TERM_CELL_W);
    i32 row = (i32)((sy - (f32)TERM_ORIGIN_Y) / (f32)TERM_CELL_H);
    if (col < 0 || col >= TERM_COLS || row < 1 || row >= TERM_ROWS - 1) {
        return 0;
    }
    *out_row = row;
    *out_col = col;
    return 1;
}

#define MAP_DOWNLOAD_TIME 26.0f
#define MAP_DOWNLOAD_BYTES 96256

static b32 update_map_download(Terminal* term, const TermView* view, f32 dt)
{
    if (!term->map_downloading) {
        return 0;
    }
    if (term->bus_state != 2 || !term->bus_tower || !term->tower_breached) {
        term->map_downloading = 0;
        term->mode = TERM_SHELL;
        term_print(term, "RELAY LINK LOST. TRANSFER ABORTED.\n");
        return 1;
    }
    term->map_dl_t += dt;
    f32 frac = f_clamp01(term->map_dl_t / MAP_DOWNLOAD_TIME);
    if (frac >= 1.0f) {
        term->map_downloading = 0;
        map_sync_file(term);
        mapdata_reveal_radius(term->tower_pos, 620.0f);
        map_sync_file(term);
        term->tower_download_done = 1;
        term->map_materialize = 0.0f;
        s_lidar_count = 0;
        return 0;
    }

    grid_clear(term);
    grid_title(term, "RELAY LINK -- DATA TRANSFER");
    grid_text(term, 3, 4, TC_DIM, "SOURCE  RELAY R-4 :: SURVEY CACHE");
    grid_text(term, 4, 4, TC_DIM, "TARGET  A:\\MAP.DAT");
    u32 done = (u32)(frac * (f32)MAP_DOWNLOAD_BYTES);
    grid_text(term, 6, 4, TC_GREEN, "%7u / %7u BYTES", done, (u32)MAP_DOWNLOAD_BYTES);
    i32 rate = 3200 + (i32)(fmodf(term->blink * 7.3f, 1.0f) * 900.0f);
    grid_text(term, 6, 40, TC_DIM, "%d B/S", rate);

    i32 width = 46;
    grid_text(term, 9, 6, TC_DIM, "[");
    i32 filled = (i32)(frac * (f32)width);
    for (i32 i = 0; i < width; i++) {
        term->glyphs[9][7 + i] = i < filled ? '#' : (i == filled ? '>' : '.');
        term->colors[9][7 + i] = i < filled ? TC_GREEN : TC_DIM;
    }
    grid_text(term, 9, 7 + width, TC_DIM, "]");
    grid_text(term, 10, (TERM_COLS - 4) / 2, TC_BRIGHT, "%3.0f%%", (f64)(frac * 100.0f));

    if (fmodf(term->blink, 0.9f) < 0.55f) {
        grid_text(term, 13, 4, TC_AMBER, "TRANSFER IN PROGRESS -- DO NOT DISCONNECT");
    }
    grid_text(term, TERM_ROWS - 1, 1, TC_DIM, "[Q] ABORT");
    (void)view;
    return 1;
}

static void update_map(Terminal* term, const TermView* view, f32 dt)
{
    if (update_map_download(term, view, dt)) {
        return;
    }
    f32 sweep_rate = 1.9f;
    if (s_virus.active) {
        sweep_rate = 1.9f + 14.0f * f_max(0.0f, sinf(term->blink * 3.1f) - 0.6f);
    }
    term->sweep_angle = f_wrap_angle(term->sweep_angle + dt * sweep_rate);
    term->map_materialize += dt;
    if (!term->map_wide) {
        if (view->orbit != 0.0f) {
            term->map_auto = 0;
            term->map_yaw += view->orbit * 0.9f * dt;
        } else if (term->map_auto) {
            term->map_yaw += 0.12f * dt;
        }
        term->map_yaw = f_wrap_angle(term->map_yaw);
    }
    term->map_zoom = f_clamp(term->map_zoom - view->zoom * 0.8f * dt, 0.45f, 1.9f);

    const Heightfield* hf = &view->terrain->hf;
    if (term->map_wide) {
        term->map_refresh -= dt;
        if (term->map_refresh <= 0.0f) {
            term->map_refresh = MAP_WIDE_REFRESH;
            s_lidar_tier = term->antenna_tier;
            map_sync_file(term);
            lidar_rebuild_wide(term, view);
            term->map_materialize = 0.0f;
        }
    } else {
        f32 moved_x = view->car_pos.x - s_lidar_center.x;
        f32 moved_z = view->car_pos.z - s_lidar_center.z;
        if (s_lidar_count == 0 || moved_x * moved_x + moved_z * moved_z > 12.0f * 12.0f
            || s_lidar_tier != term->antenna_tier) {
            s_lidar_tier = term->antenna_tier;
            lidar_rebuild(view);
        }
    }

    f32 settle = ease01(term->map_materialize / SURVEY_SETTLE_TIME);
    Vec3 target;
    f32 dist;
    f32 pitch;
    f32 fov;
    f32 far_plane;
    f32 cam_yaw = term->map_yaw;
    if (term->map_wide) {
        f32 ext_x = (f32)(hf->size_x - 1) * hf->cell_size;
        f32 ext_z = (f32)(hf->size_z - 1) * hf->cell_size;
        target = v3(hf->origin.x + ext_x * 0.5f, 0.0f, hf->origin.z + ext_z * 0.5f);
        target.y = heightfield_sample(hf, target.x, target.z);
        dist = f_max(ext_x, ext_z) * 0.80f * term->map_zoom;
        pitch = 1.15f;
        fov = SURVEY_FOV;
        far_plane = dist * 2.5f + 500.0f;
        cam_yaw = 0.0f;
    } else {
        target = vec3_add(view->car_pos, v3(0.0f, 4.0f, 0.0f));
        dist = (SURVEY_DIST + 200.0f * (1.0f - settle)) * term->map_zoom;
        pitch = SURVEY_PITCH + 0.30f * (1.0f - settle);
        fov = SURVEY_FOV - (13.0f * PI32 / 180.0f) * (1.0f - settle);
        far_plane = 3200.0f;
    }
    Vec3 eye = vec3_add(target, v3(sinf(cam_yaw) * cosf(pitch) * dist,
                                   sinf(pitch) * dist,
                                   cosf(cam_yaw) * cosf(pitch) * dist));
    Mat4 vmat = mat4_look_at(eye, target, v3(0.0f, 1.0f, 0.0f));
    Mat4 proj = mat4_perspective(fov, (f32)TERM_TEX_W / (f32)TERM_TEX_H, 0.5f, far_plane);
    term->vp3d = mat4_mul(proj, vmat);
    term->map_car_pos = view->car_pos;

    grid_clear(term);
    if (term->map_wide) {
        grid_title(term, "TERRAIN SURVEY -- WIDE RANGE");
        f32 coverage = 100.0f * (f32)mapdata_count() / (f32)mapdata_total();
        u32 file_kb = (MAP_FILE_BASE + s_map_saved_count * MAP_FILE_CELL_BYTES + 1023) / 1024;
        grid_text(term, 1, 1, TC_DIM, "COVERAGE %4.1f%%   MAP.DAT %3uK", (f64)coverage, file_kb);
        grid_text(term, 1, TERM_COLS - 14, TC_DIM, "NEXT SWEEP %2.0fS",
                  (f64)f_max(term->map_refresh, 0.0f));
        if (term->map_corrupt) {
            if (fmodf(term->blink, 0.8f) < 0.55f) {
                grid_text(term, TERM_ROWS - 2, 1, TC_RED,
                          "MAP DATA CORRUPTED - SURVEY UNRELIABLE");
            }
        } else if (term->map_disk_full) {
            grid_text(term, TERM_ROWS - 2, 1, TC_AMBER,
                      "DISK FULL - NEW SURVEY DATA NOT SAVED");
        }
    } else {
        grid_title(term, "TERRAIN SURVEY -- IMMEDIATE");
        grid_text(term, 1, 1, TC_DIM, "GRID %+05.0f/%+05.0f", (f64)view->car_pos.x,
                  (f64)view->car_pos.z);
        grid_text(term, 1, 22, term->antenna_tier == 0 ? TC_AMBER : TC_DIM, "ANT: %s%s",
                  antenna_variant_name(term->antenna_tier),
                  term->antenna_tier == 0 ? " (TERRAIN ONLY)" : "");
        f32 span = (f32)(LIDAR_N - 1) * LIDAR_SPACING * 0.5f;
        grid_text(term, 1, TERM_COLS - 11, TC_DIM, "RANGE %3.0fM", (f64)span);
        if (term->antenna_tier >= 2) {
            u8 scan_color = fmodf(term->blink, 1.4f) < 0.9f ? TC_GREEN : TC_DIM;
            if (s_virus.active) {
                i32 fake = 1 + (i32)(fmodf(term->blink * 0.37f, 1.0f) * 40.0f);
                grid_text(term, TERM_ROWS - 2, 1, TC_RED, "ANOMALY SCAN: %d CONTACTS CLOSING",
                          fake);
            } else {
                grid_text(term, TERM_ROWS - 2, 1, scan_color, "ANOMALY SCAN: NO CONTACTS");
            }
        }
    }

    if (term->map_materialize < 1.6f) {
        if (fmodf(term->blink, 0.5f) < 0.32f) {
            grid_text(term, TERM_ROWS / 2, TERM_COLS / 2 - 6, TC_BRIGHT, "SCANNING ...");
        }
    }

    i32 row, col;
    if (map_project(term, vec3_add(view->car_pos, v3(0.0f, 3.0f, 0.0f)), &row, &col)) {
        grid_text(term, row, col, TC_BRIGHT, "@");
    }
    if (s_virus.active) {
        for (i32 g = 0; g < 3; g++) {
            f32 ang = term->blink * (0.31f + 0.17f * (f32)g) + (f32)g * 2.3f;
            Vec3 ghost = vec3_add(view->car_pos, v3(sinf(ang) * (40.0f + 25.0f * (f32)g), 3.0f,
                                                    cosf(ang * 1.3f) * (40.0f + 25.0f * (f32)g)));
            if (fmodf(term->blink * (1.0f + 0.4f * (f32)g), 1.9f) < 0.7f
                && map_project(term, ghost, &row, &col)) {
                grid_text(term, row, col, TC_RED, "@");
            }
        }
    }
    Vec3 marks[2] = { view->garage_pos, view->mission_pos };
    const char* labels[2] = { "G", "X" };
    b32 show[2];
    show[0] = term->antenna_tier >= 1;
    show[1] = term->antenna_tier >= 1
              && (view->mission_stage == 1 || view->mission_stage == 2);
    for (u32 m = 0; m < 2; m++) {
        if (!show[m] || (m == 1 && fmodf(term->blink, 0.7f) > 0.45f)) {
            continue;
        }
        Vec3 top = marks[m];
        top.y = heightfield_sample(hf, top.x, top.z) + 52.0f;
        if (map_project(term, top, &row, &col)) {
            grid_text(term, row, col, TC_AMBER, "%s", labels[m]);
        }
    }

    if (term->map_wide) {
        grid_text(term, TERM_ROWS - 1, 1, TC_DIM, "[M] IMMEDIATE   ^ v ZOOM");
    } else {
        grid_text(term, TERM_ROWS - 1, 1, TC_DIM, "[M] WIDE RANGE   < > ORBIT   ^ v ZOOM   %s",
                  term->map_auto ? "AUTO" : "    ");
    }
    grid_text(term, TERM_ROWS - 1, TERM_COLS - 20, TC_DIM, "@ CAR  G GAR  X OBJ");
}

static void grid_block(Terminal* term, i32 row, i32 col, i32 max_rows, u8 color,
                       const char* text, i32 budget)
{
    i32 r = 0;
    i32 c = 0;
    for (const char* p = text; *p && r < max_rows && row + r < TERM_ROWS; p++) {
        if (*p == '\n') {
            r++;
            c = 0;
            continue;
        }
        if (budget >= 0) {
            if (budget == 0) {
                return;
            }
            budget--;
        }
        if (col + c >= 0 && col + c < TERM_COLS) {
            term->glyphs[row + r][col + c] = (u8)*p;
            term->colors[row + r][col + c] = color;
        }
        c++;
    }
}

static i32 comms_printable_in(const char* text, i32 budget)
{
    i32 printable = 0;
    for (const char* p = text; *p && budget > 0; p++) {
        if (*p != '\n') {
            budget--;
            if (*p != ' ') {
                printable++;
            }
        }
    }
    return printable;
}

static i32 comms_text_chars(const char* text)
{
    i32 n = 0;
    for (const char* p = text; *p; p++) {
        if (*p != '\n') {
            n++;
        }
    }
    return n;
}

static const char* comms_page_text(const CommsMsg* msg, i32 page)
{
    if (!msg || page < 0 || page >= msg->briefing.page_count) {
        return "";
    }
    return msg->briefing.pages[page];
}

static void comms_enter_phase(Terminal* term, CommsPhase phase)
{
    term->comms_phase = phase;
    term->comms_phase_time = 0.0f;
    if (phase == COMMS_PHASE_INBOX || phase == COMMS_PHASE_BRIEF) {
        term->comms_mat = 0.0f;
    }
    if (phase == COMMS_PHASE_BRIEF) {
        term->comms_reveal = 0.0f;
        term->comms_blips = 0;
    }
}

static void comms_set_status(Terminal* term, const char* text)
{
    snprintf(term->comms_status, sizeof(term->comms_status), "%s", text);
    term->comms_status_until = 5.0f;
}

static void comms_key_char(Terminal* term, char c)
{
    if (term->comms_phase == COMMS_PHASE_INBOX && c >= '1' && c <= '9') {
        CommsMsg* msg = comms_inbox_get(&s_comms, c - '0');
        if (msg) {
            term->comms_open = c - '0';
            term->comms_page = 0;
            comms_enter_phase(term, COMMS_PHASE_READ);
            term->comms_mat = 10.0f;
            term->click_pending = 1;
            if (!msg->read) {
                msg->read = 1;
            }
        }
        return;
    }
    if (term->comms_phase != COMMS_PHASE_READ) {
        return;
    }
    CommsMsg* msg = comms_inbox_get(&s_comms, term->comms_open);
    if (!msg) {
        return;
    }
    if (c == 'B' && msg->has_briefing) {
        term->comms_page = 0;
        comms_enter_phase(term, COMMS_PHASE_BRIEF);
    } else if (c == 'L' && msg->att_site && msg->att_site[0] && !msg->site_downloaded) {
        msg->site_downloaded = 1;
        comms_set_status(term, "COORDINATES SAVED TO SURVEY.");
    } else if (c == 'P' && msg->att_part && msg->att_part[0] && !msg->part_claimed) {
        msg->part_claimed = 1;
        comms_set_status(term, "SUPPLY VOUCHER LOGGED.");
    }
}

static void comms_key_enter(Terminal* term)
{
    if (term->comms_phase != COMMS_PHASE_BRIEF) {
        return;
    }
    CommsMsg* msg = comms_inbox_get(&s_comms, term->comms_open);
    if (!msg) {
        comms_enter_phase(term, COMMS_PHASE_READ);
        return;
    }
    i32 total = comms_text_chars(comms_page_text(msg, term->comms_page));
    if (term->comms_phase_time > COMMS_SPRITE_SCAN_TIME && (i32)term->comms_reveal < total) {
        term->comms_reveal = (f32)total;
        return;
    }
    if (term->comms_page + 1 < msg->briefing.page_count) {
        term->comms_page++;
        term->comms_reveal = 0.0f;
        term->comms_blips = 0;
    } else {
        comms_enter_phase(term, COMMS_PHASE_READ);
        term->comms_mat = 10.0f;
    }
}

static i32 comms_connect_lines(char lines[5][64])
{
    snprintf(lines[0], 64, "DR RELAYNET TERMINAL 2.3");
    snprintf(lines[1], 64, "ALIGNING WHIP ANTENNA ............ LOCK");
    snprintf(lines[2], 64, "GARAGE RELAY HANDSHAKE ........... CARRIER OK");
    snprintf(lines[3], 64, "CRYPT KEY 'LONG PATIENCE' ........ ACCEPTED");
    snprintf(lines[4], 64, "MAILBOX SYNC ..................... %d STORED / %d NEW",
             comms_delivered_count(&s_comms), comms_unread_count(&s_comms));
    return 5;
}

static void comms_att_tags(const CommsMsg* msg, char* buf, u32 size)
{
    buf[0] = 0;
    if (msg->att_site && msg->att_site[0]) {
        snprintf(buf + strlen(buf), size - strlen(buf), "LOC ");
    }
    if (msg->has_briefing) {
        snprintf(buf + strlen(buf), size - strlen(buf), "BRF ");
    }
    if (msg->att_part && msg->att_part[0]) {
        snprintf(buf + strlen(buf), size - strlen(buf), "PKG ");
    }
}

static void draw_comms_connect(Terminal* term, f32 dt)
{
    char lines[5][64];
    i32 n = comms_connect_lines(lines);
    i32 total = 0;
    for (i32 i = 0; i < n; i++) {
        total += (i32)strlen(lines[i]);
    }
    i32 before = (i32)((term->comms_phase_time - dt) * COMMS_CONNECT_CPS);
    i32 after = (i32)(term->comms_phase_time * COMMS_CONNECT_CPS);
    if (after > before && before < total) {
        term->click_pending = 1;
    }
    i32 budget = after;
    i32 row = 2;
    for (i32 i = 0; i < n && budget > 0; i++) {
        grid_block(term, row, 3, 1, i == 0 ? TC_BRIGHT : TC_GREEN, lines[i], budget);
        budget -= (i32)strlen(lines[i]);
        row += i == 0 ? 2 : 1;
    }
    if (fmodf(term->blink, 0.5f) < 0.3f && row + 1 < TERM_ROWS) {
        term->glyphs[row + 1][3] = 127;
        term->colors[row + 1][3] = TC_BRIGHT;
    }
    if (term->comms_phase_time * COMMS_CONNECT_CPS
        > (f32)total + COMMS_CONNECT_HOLD * COMMS_CONNECT_CPS) {
        comms_enter_phase(term, COMMS_PHASE_INBOX);
    }
}

static void draw_comms_inbox(Terminal* term)
{
    grid_text(term, 0, 1, TC_BRIGHT,
              "== RELAYNET // MAILBOX ==============================================");
    grid_text(term, 1, 1, TC_GREEN, "CARRIER: GARAGE RELAY  LINK: GREEN  TRAFFIC: %d STORED / %d UNREAD",
              comms_delivered_count(&s_comms), comms_unread_count(&s_comms));

    i32 total = comms_delivered_count(&s_comms);
    if (total == 0) {
        grid_text(term, 4, 1, TC_GREEN, "NO TRAFFIC ON THE RELAY.");
    }
    i32 row = 3;
    for (i32 i = 1; i <= total && row < TERM_ROWS - 2; i++) {
        const CommsMsg* msg = comms_inbox_get(&s_comms, i);
        if (!msg) {
            continue;
        }
        char tags[16];
        comms_att_tags(msg, tags, sizeof(tags));
        b32 unread = !msg->read;
        u8 color = unread ? TC_BRIGHT : TC_DIM;
        if (unread && fmodf(term->blink + (f32)i * 0.13f, 1.1f) < 0.12f) {
            color = TC_GREEN;
        }
        grid_text(term, row, 2, color, "[%d]%c C%02d %-12s %-28.28s %s", i, unread ? '*' : ' ',
                  msg->cycle, msg->from, msg->sub, tags);
        row++;
    }

    grid_text(term, TERM_ROWS - 1, 1, TC_DIM, "[1-9] OPEN TRANSMISSION      [Q] DISCONNECT");
}

static void draw_comms_read(Terminal* term)
{
    CommsMsg* msg = comms_inbox_get(&s_comms, term->comms_open);
    if (!msg) {
        return;
    }
    grid_text(term, 0, 1, TC_BRIGHT,
              "== RELAYNET // TRANSMISSION %02d =====================================",
              term->comms_open);
    grid_text(term, 1, 1, TC_GREEN, "FROM: %-18s CYCLE %02d", msg->from, msg->cycle);
    grid_text(term, 2, 1, TC_GREEN, "SUBJ: %s", msg->sub);

    grid_block(term, 4, 1, 12, TC_GREEN, msg->body, -1);

    b32 any_att = (msg->att_site && msg->att_site[0]) || msg->has_briefing
                  || (msg->att_part && msg->att_part[0]);
    i32 ay = 17;
    if (any_att) {
        grid_text(term, ay++, 1, TC_BRIGHT, "ATTACHMENTS:");
        if (msg->has_briefing) {
            grid_text(term, ay++, 1, fmodf(term->blink, 1.0f) < 0.6f ? TC_BRIGHT : TC_GREEN,
                      "  [B] RECORDED BRIEFING: %s", msg->briefing.speaker);
        }
        if (msg->att_site && msg->att_site[0]) {
            if (msg->site_downloaded) {
                grid_text(term, ay++, 1, TC_DIM, "      SITE COORDINATES: %s (ON FILE)",
                          msg->att_site);
            } else {
                grid_text(term, ay++, 1, TC_BRIGHT, "  [L] SITE COORDINATES: %s -- DOWNLOAD",
                          msg->att_site);
            }
        }
        if (msg->att_part && msg->att_part[0]) {
            if (msg->part_claimed) {
                grid_text(term, ay++, 1, TC_DIM, "      SUPPLY VOUCHER: %s (CLAIMED)",
                          msg->att_part);
            } else {
                grid_text(term, ay++, 1, TC_BRIGHT, "  [P] SUPPLY VOUCHER: %s %.0f%% -- CLAIM",
                          msg->att_part, (f64)msg->att_part_cond);
            }
        }
    }

    if (term->comms_status[0] && term->comms_status_until > 0.0f
        && fmodf(term->blink, 0.6f) < 0.4f) {
        grid_text(term, TERM_ROWS - 2, 1, TC_AMBER, "%s", term->comms_status);
    }
    grid_text(term, TERM_ROWS - 1, 1, TC_DIM, "[Q] BACK TO MAILBOX");
}

static void comms_frame(Terminal* term, i32 row0, i32 col0, i32 cols, i32 rows, i32 title_cols)
{
    for (i32 c = 0; c < cols; c++) {
        if (c < 2 || c > title_cols + 2) {
            term->glyphs[row0][col0 + c] = '-';
            term->colors[row0][col0 + c] = TC_GREEN;
        }
        term->glyphs[row0 + rows - 1][col0 + c] = '-';
        term->colors[row0 + rows - 1][col0 + c] = TC_GREEN;
    }
    for (i32 r = 1; r < rows - 1; r++) {
        term->glyphs[row0 + r][col0] = '|';
        term->colors[row0 + r][col0] = TC_GREEN;
        term->glyphs[row0 + r][col0 + cols - 1] = '|';
        term->colors[row0 + r][col0 + cols - 1] = TC_GREEN;
    }
    term->glyphs[row0][col0] = '+';
    term->glyphs[row0][col0 + cols - 1] = '+';
    term->glyphs[row0 + rows - 1][col0] = '+';
    term->glyphs[row0 + rows - 1][col0 + cols - 1] = '+';
}

static void draw_comms_brief(Terminal* term, f32 dt)
{
    CommsMsg* msg = comms_inbox_get(&s_comms, term->comms_open);
    if (!msg) {
        comms_enter_phase(term, COMMS_PHASE_READ);
        return;
    }
    const CommsBriefing* brief = &msg->briefing;
    const char* text = comms_page_text(msg, term->comms_page);
    i32 total = comms_text_chars(text);
    b32 talking = term->comms_phase_time > COMMS_SPRITE_SCAN_TIME;

    if (talking && (i32)term->comms_reveal < total) {
        term->comms_reveal += dt * COMMS_SPEECH_CPS;
        i32 spoken = comms_printable_in(text, (i32)term->comms_reveal);
        if (spoken / 2 > term->comms_blips) {
            term->comms_blips = spoken / 2;
            term->click_pending = 1;
        }
    }

    char title[64];
    snprintf(title, sizeof(title), " RELAYNET RECORDED BRIEFING -- %s ", brief->speaker);
    comms_frame(term, 0, 1, 68, TERM_ROWS, (i32)strlen(title));
    grid_text(term, 0, 4, TC_BRIGHT, "%s", title);
    if (fmodf(term->blink, 1.0f) < 0.55f) {
        grid_text(term, 2, 60, TC_BRIGHT, "\x7f REC");
    }

    i32 rows_visible = (i32)(term->comms_phase_time / COMMS_SPRITE_SCAN_TIME
                             * (f32)brief->sprite_rows);
    if (rows_visible > brief->sprite_rows) {
        rows_visible = brief->sprite_rows;
    }
    b32 speaking = talking && (i32)term->comms_reveal < total;
    for (i32 r = 0; r < rows_visible; r++) {
        u8 color = TC_GREEN;
        i32 jitter = 0;
        if (r == rows_visible - 1 && rows_visible < brief->sprite_rows) {
            color = TC_BRIGHT;
        }
        if (speaking && r > brief->sprite_rows * 2 / 3) {
            color = f_abs(sinf(term->blink * 26.0f + (f32)r * 1.7f)) > 0.55f
                    ? TC_GREEN : TC_DIM;
            jitter = fmodf(term->blink * 31.0f + (f32)r, 2.0f) < 0.2f ? 1 : 0;
        }
        grid_block(term, 3 + r, 4 + jitter, 1, color, brief->sprite[r], -1);
    }

    if (talking) {
        grid_block(term, 4, 37, TERM_ROWS - 8, TC_BRIGHT, text, (i32)term->comms_reveal);
    }

    grid_text(term, TERM_ROWS - 2, 4, TC_DIM, "PAGE %d/%d", term->comms_page + 1,
              brief->page_count);
    if (talking && (i32)term->comms_reveal >= total && fmodf(term->blink, 0.7f) < 0.45f) {
        grid_text(term, TERM_ROWS - 2, 16, TC_BRIGHT, "%s",
                  term->comms_page + 1 < brief->page_count
                  ? "[ENTER] CONTINUE" : "[ENTER] END OF RECORDING");
    }
}

static void update_comms(Terminal* term, f32 dt)
{
    term->comms_phase_time += dt;
    term->comms_mat += dt;
    if (term->comms_status_until > 0.0f) {
        term->comms_status_until -= dt;
    }
    grid_clear(term);
    switch (term->comms_phase) {
    case COMMS_PHASE_CONNECT:
        draw_comms_connect(term, dt);
        break;
    case COMMS_PHASE_INBOX:
        draw_comms_inbox(term);
        break;
    case COMMS_PHASE_READ:
        draw_comms_read(term);
        break;
    case COMMS_PHASE_BRIEF:
        draw_comms_brief(term, dt);
        break;
    }
}

static void draw_link_port(Terminal* term, i32 row, const char* label, i32 state,
                           const char* linked_desc)
{
    grid_text(term, row, 2, TC_BRIGHT, "%s", label);
    if (state == 0) {
        grid_text(term, row + 1, 4, TC_DIM, "UNPLUGGED");
    } else if (state == 1) {
        u8 color = fmodf(term->blink, 0.9f) < 0.55f ? TC_AMBER : TC_DIM;
        grid_text(term, row + 1, 4, color, "PLUGGED - NO LINK");
    } else {
        grid_text(term, row + 1, 4, TC_GREEN, "LINKED - %s", linked_desc);
    }
}

static void update_link(Terminal* term, f32 dt)
{
    if (term->link_deny > 0.0f) {
        term->link_deny -= dt;
    }
    if (term->link_anim_port >= 0) {
        f32 prev = term->link_anim_t;
        term->link_anim_t += dt;
        if ((i32)(term->link_anim_t * 7.0f) != (i32)(prev * 7.0f)) {
            term->click_pending = 1;
        }
        i32 port = term->link_anim_port;
        i32 state = port == 0 ? term->coax_state : term->bus_state;
        b32 secured = port == 1 && term->bus_tower && !term->tower_breached;
        if (state != 1) {
            term->link_anim_port = -1;
        } else if (secured && term->link_anim_t >= 1.1f) {
            term->link_deny = 3.0f;
            term->link_anim_port = -1;
            term->click_pending = 1;
        } else if (term->link_anim_t >= 1.6f) {
            term->link_request[port] = 1;
            term->link_anim_port = -1;
        }
    }

    grid_clear(term);
    grid_title(term, "PORT LINK MANAGER");

    char coax_desc[40];
    if (term->coax_camera) {
        snprintf(coax_desc, sizeof(coax_desc), "CAMERA FILM");
    } else {
        snprintf(coax_desc, sizeof(coax_desc), "%s ANTENNA",
                 antenna_variant_name(term->antenna_tier));
    }
    draw_link_port(term, 2, "PORT A   COAX / ANTENNA", term->coax_state,
                   term->coax_camera || term->antenna_tier >= 0 ? coax_desc : "ANTENNA");
    const char* bus_desc = term->bus_tower
                           ? (term->tower_breached ? "RELAY R-4 (OPEN)" : "RELAY R-4 (SECURED)")
                           : "ENGINE BAY TAP";
    draw_link_port(term, 5, term->bus_tower ? "PORT B   REMOTE NODE" : "PORT B   VEHICLE BUS",
                   term->bus_state, bus_desc);
    if (term->bus_state == 2 && term->bus_tower && !term->tower_breached) {
        grid_text(term, 6, 4, fmodf(term->blink, 0.7f) < 0.45f ? TC_RED : TC_DIM,
                  "LOCKED - RUN BREACH");
    }

    if (term->link_deny > 0.0f) {
        grid_text(term, 9, 2, fmodf(term->blink, 0.4f) < 0.25f ? TC_RED : TC_DIM,
                  "ACCESS DENIED - PORT SECURED");
        grid_text(term, 10, 2, TC_DIM, "RUN BREACH TO CRACK THE LOCK.");
    } else if (term->link_anim_port >= 0) {
        i32 dots = (i32)(term->link_anim_t * 8.0f);
        if (dots > 12) {
            dots = 12;
        }
        grid_text(term, 9, 2, TC_BRIGHT, "NEGOTIATING PORT %c %.*s",
                  term->link_anim_port == 0 ? 'A' : 'B', dots, "............");
        if (term->link_anim_t > 1.1f) {
            grid_text(term, 10, 2, TC_GREEN, "CARRIER OK");
        }
    } else if (term->coax_state == 1 || term->bus_state == 1) {
        if (fmodf(term->blink, 0.8f) < 0.5f) {
            grid_text(term, 9, 2, TC_AMBER, "PORT READY. INITIALIZE TO ESTABLISH LINK.");
        }
    } else if (term->coax_state == 0 && term->bus_state == 0) {
        grid_text(term, 9, 2, TC_DIM, "CONNECT CABLES AT REAR OF UNIT.");
    }

    grid_text(term, TERM_ROWS - 1, 1, TC_DIM, "[1] INIT PORT A   [2] INIT PORT B   [Q] BACK");
}

static const char* VIRUS_TAUNTS[3] = {
    "?SYN ?SYN ?SYN CARRIER LOST\n",
    "I CAN SEE THE ROAD FROM HERE\n",
    "SECTOR 0 SECTOR 0 SECTOR 0\n",
};

static void virus_tick(Terminal* term, f32 dt)
{
    if (s_virus.burst > 0.0f) {
        s_virus.burst -= dt;
        if (term->mode != TERM_BOOT) {
            i32 n = 8 + (i32)(virus_rand() * 30.0f);
            for (i32 i = 0; i < n; i++) {
                i32 r = (i32)(virus_rand() * (f32)TERM_ROWS) % TERM_ROWS;
                i32 c = (i32)(virus_rand() * (f32)TERM_COLS) % TERM_COLS;
                term->glyphs[r][c] = (u8)(33 + (i32)(virus_rand() * 91.0f));
                term->colors[r][c] = virus_rand() < 0.3f ? TC_RED : TC_BRIGHT;
            }
        }
    }
    s_virus.next_beep -= dt;
    if (s_virus.next_beep <= 0.0f) {
        s_virus.next_beep = 1.5f + virus_rand() * 6.0f;
        term->click_pending = 1;
    }
    s_virus.next_corrupt -= dt;
    if (s_virus.next_corrupt <= 0.0f) {
        s_virus.next_corrupt = 60.0f + virus_rand() * 60.0f;
        FsDrive* d = &s_fs.drives[FS_DRIVE_A];
        i32 candidates[FS_DRIVE_NODES];
        i32 count = 0;
        for (i32 i = 1; i < FS_DRIVE_NODES; i++) {
            if (d->nodes[i].used && !d->nodes[i].is_dir && !d->nodes[i].corrupted) {
                candidates[count++] = i;
            }
        }
        if (count > 0) {
            FsNode* victim = &d->nodes[candidates[(i32)(virus_rand() * (f32)count) % count]];
            victim->corrupted = 1;
            s_virus.burst = 0.6f + virus_rand() * 0.6f;
            term->click_pending = 1;
            if (term->mode == TERM_SHELL) {
                term_printf(term, "WRITE FAULT ON DRIVE A: -- %s\n", victim->name);
                if (virus_rand() < 0.35f) {
                    term_print(term, VIRUS_TAUNTS[(u32)(virus_rand() * 2.999f)]);
                }
            }
        }
    }
}

f32 terminal_virus_fx(const Terminal* term)
{
    if (!term->powered || !s_virus.active) {
        return 0.0f;
    }
    return s_virus.burst > 0.0f ? 1.0f : 0.0f;
}

f32 terminal_pixelate(const Terminal* term)
{
    if (!term->powered) {
        return 1.0f;
    }
    if (s_virus.active && s_virus.burst > 0.0f && fmodf(term->blink * 13.0f, 1.0f) < 0.5f) {
        return 3.0f;
    }
    if (term->mode != TERM_COMMS || term->comms_phase == COMMS_PHASE_CONNECT) {
        return 1.0f;
    }
    if (term->comms_mat >= 1.0f) {
        return 1.0f;
    }
    i32 level = (i32)(term->comms_mat * 7.0f);
    return (f32)(64 >> level);
}

void terminal_update(Terminal* term, const TermView* view, f32 dt)
{
    if (!term->powered) {
        return;
    }
    term->coax_state = view->coax_state;
    term->bus_state = view->bus_state;
    term->antenna_tier = view->antenna_tier;
    term->coax_camera = view->coax_camera;
    term->bus_tower = view->bus_tower;
    term->tower_breached = view->tower_breached;
    term->tower_pos = view->tower_pos;
    if (!term->dev_time_request) {
        term->dev_tod = view->time_of_day;
    }
    term->dev_rain = view->weather_rain;
    term->dev_wet = view->weather_wetness;
    if (!term->dev_weather_request) {
        term->dev_wmode = view->weather_mode;
    }
    term->deck_docked = view->sys->parts[PART_COMPUTER].installed;
    term->deck_tape = view->sys->tape_inserted;
    term->deck_cond = view->sys->tape_cond;
    term->deck_play = view->sys->deck_play;
    terminal_camera_mount(term, view->coax_state == 2 && view->coax_camera);
    terminal_tower_mount(term, view->bus_state == 2 && view->bus_tower && view->tower_breached);
    if (!(view->bus_tower && view->tower_breached)) {
        term->tower_download_done = 0;
    }
    if (term->mode == TERM_STATUS && (term->bus_state != 2 || term->bus_tower)) {
        term->mode = TERM_SHELL;
    }
    if (term->mode == TERM_MAP && (term->coax_state != 2 || term->antenna_tier < 0)) {
        term->mode = TERM_SHELL;
    }
    if (term->mode == TERM_BREACH && !(term->bus_state >= 1 && term->bus_tower)) {
        term->mode = TERM_SHELL;
        term_print(term, "PORT CONNECTION LOST. BREACH ABORTED.\n");
    }
    term->mode_timer += dt;
    term->blink += dt;
    if (term->blink > 100.0f) {
        term->blink -= 100.0f;
    }

    if (term->mode == TERM_BOOT && term->mode_timer > 4.8f) {
        term->mode = TERM_SHELL;
        term->mode_timer = 0.0f;
    }
    if (term->mode_timer < 0.0f) {
        term->mode_timer = 0.0f;
        static const char* mission_text[4] = {
            "OBJECTIVE: RESTORE THE VEHICLE AND GET MOVING.\n",
            "OBJECTIVE: REACH THE MARKED SITE. SEE MAP.\n",
            "OBJECTIVE: RETURN TO THE GARAGE. SEE MAP.\n",
            "OBJECTIVE: COMPLETE. AWAITING FURTHER TASKING.\n",
        };
        term_print(term, mission_text[view->mission_stage < 4 ? view->mission_stage : 3]);
    }

    term_reveal(term, dt);

    switch (term->mode) {
    case TERM_BOOT:
        draw_boot(term);
        break;
    case TERM_SHELL:
        draw_shell(term);
        break;
    case TERM_STATUS:
        term->status_spin = f_wrap_angle(term->status_spin + dt * 0.55f);
        draw_status(term, view);
        break;
    case TERM_MAP:
        update_map(term, view, dt);
        break;
    case TERM_COMMS:
        update_comms(term, dt);
        break;
    case TERM_LINK:
        update_link(term, dt);
        break;
    case TERM_VIEW:
        term->map_materialize += dt;
        draw_view(term);
        break;
    case TERM_VIDEO:
        if (term->coax_state != 2 || !term->coax_camera) {
            term->mode = TERM_SHELL;
            term_print(term, "VIDEO SOURCE LOST.\n");
        } else {
            term->map_materialize += dt;
            draw_video(term);
        }
        break;
    case TERM_BREACH:
        draw_breach(term, dt);
        break;
    case TERM_TAPES:
        update_tapes(term, dt);
        break;
    case TERM_DEV:
        draw_dev(term);
        break;
    }

    if (s_virus.active) {
        virus_tick(term, dt);
    }
}

static u32 build_glyph_instances(const Terminal* term)
{
    u32 n = 0;
    for (i32 row = 0; row < TERM_ROWS; row++) {
        for (i32 col = 0; col < TERM_COLS; col++) {
            u16 glyph = term->glyphs[row][col];
            u8 color = term->colors[row][col];
            f32 x = (f32)(TERM_ORIGIN_X + col * TERM_CELL_W);
            f32 y = (f32)(TERM_ORIGIN_Y + row * TERM_CELL_H + 1);
            if (color == TC_BG) {
                s_glyph_insts[n].x = x;
                s_glyph_insts[n].y = y;
                s_glyph_insts[n].glyph = (f32)TERM_FONT_BLOCK;
                s_glyph_insts[n].color = (f32)TC_GREEN;
                s_glyph_insts[n].w = 8.0f;
                n++;
            }
            if (glyph <= 32 || glyph == TERM_FONT_WIDE_CONT) {
                continue;
            }
            b32 wide = 0;
            u32 slot = term_font_slot(glyph, &wide);
            s_glyph_insts[n].x = x;
            s_glyph_insts[n].y = y;
            s_glyph_insts[n].glyph = (f32)slot;
            s_glyph_insts[n].color = (f32)color;
            s_glyph_insts[n].w = wide ? 16.0f : 8.0f;
            n++;
        }
    }
    return n;
}

void terminal_render(Terminal* term)
{
    if (!s_gpu.ready || !term->powered) {
        return;
    }
    u32 text_prog = r_shader("term_text");
    u32 persist_prog = r_shader("term_persist");
    u32 map_prog = r_shader("term_map");
    if (!text_prog || !persist_prog) {
        return;
    }

    static const f32 bg[4] = { 0.027f, 0.086f, 0.035f, 1.0f };
    static const f32 black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    static const f32 depth_one = 1.0f;
    if (s_gpu.clear_pending) {
        s_gpu.clear_pending = 0;
        for (u32 i = 0; i < 2; i++) {
            glClearNamedFramebufferfv(s_gpu.persist_fbo[i], GL_COLOR, 0, black);
            glGenerateTextureMipmap(s_gpu.persist_tex[i]);
        }
    }
    glClearNamedFramebufferfv(s_gpu.fbo, GL_COLOR, 0, bg);
    glClearNamedFramebufferfv(s_gpu.fbo, GL_DEPTH, 0, &depth_one);

    glBindFramebuffer(GL_FRAMEBUFFER, s_gpu.fbo);
    glViewport(0, 0, TERM_TEX_W, TERM_TEX_H);

    if (term->mode == TERM_MAP && map_prog && s_lidar_count > 0) {
        if (s_lidar_dirty) {
            s_lidar_dirty = 0;
            glNamedBufferSubData(s_gpu.point_vbo, 0,
                                 (GLsizeiptr)(s_lidar_count * 4 * sizeof(f32)), s_lidar_pts);
        }
        f32 reveal = term->map_materialize
                     * (term->map_wide ? LIDAR_WIDE_REVEAL_SPEED : LIDAR_REVEAL_SPEED);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glUseProgram(map_prog);
        glProgramUniformMatrix4fv(map_prog, 0, 1, GL_FALSE, term->vp3d.m);
        glProgramUniform3f(map_prog, 4, term->map_car_pos.x, term->map_car_pos.y,
                           term->map_car_pos.z);
        glProgramUniform1f(map_prog, 5, (f32)platform_time_now());
        glProgramUniform1f(map_prog, 6, term->sweep_angle);
        glProgramUniform1f(map_prog, 7, reveal);
        glBindVertexArray(s_gpu.point_vao);
        glDrawArrays(GL_POINTS, 0, (GLsizei)s_lidar_count);
        glDisable(GL_PROGRAM_POINT_SIZE);
        glDisable(GL_DEPTH_TEST);
    }

    if (term->mode == TERM_VIEW) {
        u32 pic_prog = r_shader("term_pic");
        const u8* pic_data = disk_photo_data(term->view_pic);
        if (pic_prog && pic_data) {
            if (s_gpu.pic_uploaded != term->view_pic) {
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTextureSubImage2D(s_gpu.pic_tex, 0, 0, 0, PHOTO_W, PHOTO_H,
                                    GL_RGB, GL_UNSIGNED_BYTE, pic_data);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                s_gpu.pic_uploaded = term->view_pic;
            }
            f32 reveal = f_clamp01(term->map_materialize * 0.9f);
            glDisable(GL_DEPTH_TEST);
            glUseProgram(pic_prog);
            glProgramUniform4f(pic_prog, 0, -0.755f, -0.76f, 0.755f, 0.75f);
            glProgramUniform1f(pic_prog, 1, reveal);
            glBindTextureUnit(0, s_gpu.pic_tex);
            glBindVertexArray(s_gpu.empty_vao);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
    }

    if (term->mode == TERM_VIDEO && s_video_src_tex) {
        u32 pic_prog = r_shader("term_pic");
        if (pic_prog) {
            f32 reveal = f_clamp01(term->map_materialize * 1.6f);
            glDisable(GL_DEPTH_TEST);
            glUseProgram(pic_prog);
            glProgramUniform4f(pic_prog, 0, -0.755f, 0.75f, 0.755f, -0.76f);
            glProgramUniform1f(pic_prog, 1, reveal);
            glBindTextureUnit(0, s_video_src_tex);
            glBindVertexArray(s_gpu.empty_vao);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
    }

    u32 wire_prog = r_shader("term_wire");
    if (term->mode == TERM_STATUS && wire_prog && s_wire_count > 0) {
        glViewport(WIRE_VP_X, WIRE_VP_Y, WIRE_VP_W, WIRE_VP_H);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glDisable(GL_DEPTH_TEST);
        glUseProgram(wire_prog);
        for (u32 i = 0; i < s_wire_count; i++) {
            Mat4 mvp = mat4_mul(term->vp3d, s_wires[i].model);
            glProgramUniformMatrix4fv(wire_prog, 0, 1, GL_FALSE, mvp.m);
            glProgramUniform3f(wire_prog, 4, s_wires[i].color.x, s_wires[i].color.y,
                               s_wires[i].color.z);
            glBindVertexArray(s_wires[i].mesh->vao);
            for (u32 s = 0; s < s_wires[i].mesh->submesh_count; s++) {
                const GpuSubmesh* sub = &s_wires[i].mesh->submeshes[s];
                glDrawElements(GL_TRIANGLES, (GLsizei)sub->index_count, GL_UNSIGNED_INT,
                               (const void*)((u64)sub->first_index * sizeof(u32)));
            }
        }
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glViewport(0, 0, TERM_TEX_W, TERM_TEX_H);
    }

    u32 count = build_glyph_instances(term);
    if (count > 0) {
        glNamedBufferSubData(s_gpu.text_vbo, 0, (GLsizeiptr)(count * sizeof(GlyphInst)),
                             s_glyph_insts);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(text_prog);
        glBindTextureUnit(0, s_gpu.atlas);
        glBindVertexArray(s_gpu.text_vao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)count);
        glDisable(GL_BLEND);
    }

    u32 next = 1 - s_gpu.persist_idx;
    glBindFramebuffer(GL_FRAMEBUFFER, s_gpu.persist_fbo[next]);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(persist_prog);
    glProgramUniform1f(persist_prog, 0, TERM_PERSIST_DECAY);
    glBindTextureUnit(0, s_gpu.color_tex);
    glBindTextureUnit(1, s_gpu.persist_tex[s_gpu.persist_idx]);
    glBindVertexArray(s_gpu.empty_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glGenerateTextureMipmap(s_gpu.persist_tex[next]);
    s_gpu.persist_idx = next;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    glEnable(GL_DEPTH_TEST);
}
