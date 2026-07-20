#pragma once

#include "core/types.h"

struct Arena;

#define MAX_KEYS 512
#define MAX_MOUSE_BUTTONS 8

#define KEY_SPACE 32
#define KEY_A 65
#define KEY_C 67
#define KEY_D 68
#define KEY_E 69
#define KEY_F 70
#define KEY_G 71
#define KEY_H 72
#define KEY_L 76
#define KEY_T 84
#define KEY_Q 81
#define KEY_R 82
#define KEY_S 83
#define KEY_W 87
#define KEY_ESCAPE 256
#define KEY_ENTER 257
#define KEY_TAB 258
#define KEY_BACKSPACE 259
#define KEY_DELETE 261
#define KEY_RIGHT 262
#define KEY_LEFT 263
#define KEY_HOME 268
#define KEY_END 269
#define KEY_DOWN 264
#define KEY_UP 265
#define KEY_LEFT_SHIFT 340
#define KEY_LEFT_CONTROL 341
#define KEY_F1 290
#define KEY_F2 291
#define KEY_F3 292
#define KEY_F4 293
#define KEY_F5 294
#define KEY_F6 295
#define KEY_F7 296
#define KEY_F8 297

#define MOUSE_LEFT 0
#define MOUSE_RIGHT 1
#define MOUSE_MIDDLE 2

typedef struct GameInput {
    u8 key_down[MAX_KEYS];
    u8 key_pressed[MAX_KEYS];
    u8 key_released[MAX_KEYS];
    u8 mouse_down[MAX_MOUSE_BUTTONS];
    u8 mouse_pressed[MAX_MOUSE_BUTTONS];
    u8 mouse_released[MAX_MOUSE_BUTTONS];
    f32 mouse_x, mouse_y;
    f32 mouse_dx, mouse_dy;
    f32 scroll_dy;
} GameInput;

typedef struct FileData {
    u8* data;
    u64 size;
} FileData;

typedef struct PlatformDirEntry {
    char name[256];
    b32 is_dir;
} PlatformDirEntry;

b32  platform_init(const char* title, i32 width, i32 height);
void platform_shutdown(void);
b32  platform_should_close(void);
void platform_request_close(void);
void platform_poll_input(void);
void platform_swap_buffers(void);
void platform_set_title(const char* title);
void platform_framebuffer_size(i32* out_width, i32* out_height);
f64  platform_time_now(void);

const GameInput* platform_input(void);
u32  platform_next_char(void);

void platform_set_cursor_captured(b32 captured);
b32  platform_cursor_captured(void);

FileData platform_read_entire_file(struct Arena* arena, const char* path);
i64      platform_file_mtime(const char* path);
u32      platform_list_dir(const char* path, PlatformDirEntry* out, u32 max_count);
void*    platform_fopen(const char* path, const char* mode);
u32      platform_utf8_to_wide(const char* utf8, u16* out, u32 out_count);
