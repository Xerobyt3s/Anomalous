#pragma once

#include "core/types.h"
#include "math/vmath.h"
#include "terminal/fs.h"

struct CarSys;
struct Vehicle;
struct PhysWorld;
struct Terrain;

#define TERM_COLS 70
#define TERM_ROWS 24
#define TERM_CELL_W 8
#define TERM_CELL_H 14
#define TERM_TEX_W 640
#define TERM_TEX_H 400
#define TERM_ORIGIN_X 40
#define TERM_ORIGIN_Y 32
#define TERM_LINES 32
#define TERM_INPUT_MAX 48
#define TERM_PENDING_MAX 4096
#define TERM_HISTORY 8

typedef enum TermMode {
    TERM_BOOT,
    TERM_SHELL,
    TERM_STATUS,
    TERM_MAP,
    TERM_COMMS,
    TERM_LINK,
    TERM_VIEW,
    TERM_VIDEO,
    TERM_BREACH,
    TERM_TAPES,
    TERM_DEV,
} TermMode;

typedef enum CommsPhase {
    COMMS_PHASE_CONNECT,
    COMMS_PHASE_INBOX,
    COMMS_PHASE_READ,
    COMMS_PHASE_BRIEF,
} CommsPhase;

typedef struct TermView {
    const struct CarSys* sys;
    const struct Vehicle* veh;
    struct PhysWorld* phys;
    const struct Terrain* terrain;
    f32 time_of_day;
    f32 weather_rain;
    f32 weather_wetness;
    i32 weather_mode;
    Vec3 car_pos;
    Vec3 garage_pos;
    Vec3 mission_pos;
    u32 mission_stage;
    f32 speed_kmh;
    f32 rpm;
    f32 orbit;
    f32 zoom;
    i32 coax_state;
    i32 bus_state;
    i32 antenna_tier;
    b32 coax_camera;
    b32 bus_tower;
    b32 tower_breached;
    Vec3 tower_pos;
} TermView;

typedef struct Terminal {
    u16 glyphs[TERM_ROWS][TERM_COLS];
    u8 colors[TERM_ROWS][TERM_COLS];
    char lines[TERM_LINES][TERM_COLS + 1];
    u32 line_head;
    u32 line_count;
    char out_line[TERM_COLS + 1];
    u32 out_len;
    char pending[TERM_PENDING_MAX];
    u32 pend_head;
    u32 pend_tail;
    f32 reveal_accum;
    char input[TERM_INPUT_MAX + 1];
    u32 input_len;
    u32 input_cursor;
    char history[TERM_HISTORY][TERM_INPUT_MAX + 1];
    u32 history_count;
    i32 history_pos;
    TermMode mode;
    f32 mode_timer;
    u32 boot_step;
    f32 blink;
    f32 sweep_angle;
    f32 map_yaw;
    f32 map_zoom;
    f32 map_materialize;
    b32 map_auto;
    b32 map_wide;
    f32 map_refresh;
    b32 map_disk_full;
    b32 map_corrupt;
    b32 map_downloading;
    f32 map_dl_t;
    f32 status_spin;
    Mat4 vp3d;
    Vec3 map_car_pos;
    CommsPhase comms_phase;
    f32 comms_phase_time;
    f32 comms_mat;
    f32 comms_reveal;
    i32 comms_open;
    i32 comms_page;
    i32 comms_blips;
    char comms_status[40];
    f32 comms_status_until;
    i32 coax_state;
    i32 bus_state;
    i32 antenna_tier;
    b32 coax_camera;
    b32 bus_tower;
    b32 tower_breached;
    Vec3 tower_pos;
    b32 deck_docked;
    i32 deck_tape;
    f32 deck_cond;
    b32 deck_play;
    b32 breach_request;
    b32 tower_download_done;
    i32 view_pic;
    char view_name[FS_NAME_MAX + 1];
    i32 link_anim_port;
    f32 link_anim_t;
    f32 link_deny;
    b32 link_request[2];
    i32 tapes_sel;
    i32 tapes_count;
    i32 tapes_write_track;
    f32 tapes_write_t;
    i32 tapes_dl_track;
    f32 tapes_dl_t;
    f32 tapes_click_t;
    i32 tapes_prompt_track;
    char tapes_dest[40];
    u32 tapes_dest_len;
    u32 tapes_dest_cursor;
    i32 tapes_dest_drive;
    i32 tapes_dest_node;
    char tapes_dest_name[FS_NAME_MAX + 1];
    char tapes_status[40];
    f32 tapes_status_until;
    b32 tape_write_request;
    i32 tape_write_value;
    f32 dev_tod;
    b32 dev_warp;
    b32 dev_time_request;
    f32 dev_time_value;
    f32 dev_rain;
    f32 dev_wet;
    i32 dev_wmode;
    i32 dev_weather_request;
    i32 cwd_drive;
    i32 cwd_node;
    i32 format_drive;
    b32 powered;
    b32 wants_off;
    b32 click_pending;
} Terminal;

b32  terminal_init(Terminal* term);
void terminal_power(Terminal* term, b32 on);
void terminal_disk_set(Terminal* term, i32 disk);
b32  terminal_camera_capture(const u8* gray);
u32  terminal_camera_exposures(void);
b32  terminal_video_active(const Terminal* term);
void terminal_video_set(u32 texture);
void terminal_update(Terminal* term, const TermView* view, f32 dt);
void terminal_render(Terminal* term);
void terminal_key_char(Terminal* term, char c);
void terminal_key_special(Terminal* term, i32 key);
u32  terminal_texture(const Terminal* term);
f32  terminal_pixelate(const Terminal* term);
f32  terminal_virus_fx(const Terminal* term);
