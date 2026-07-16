#pragma once

#include "core/types.h"
#include "math/vmath.h"

struct Arena;

#define CONFIG_MAX_ENTRIES 512

typedef struct ConfigEntry {
    const char* key;
    const char* value;
} ConfigEntry;

typedef struct Config {
    ConfigEntry entries[CONFIG_MAX_ENTRIES];
    u32 count;
} Config;

b32         config_parse(Config* cfg, struct Arena* arena, const char* text);
const char* config_get_str(const Config* cfg, const char* key, const char* fallback);
f32         config_get_f32(const Config* cfg, const char* key, f32 fallback);
i32         config_get_i32(const Config* cfg, const char* key, i32 fallback);
Vec3        config_get_vec3(const Config* cfg, const char* key, Vec3 fallback);
u32         config_get_f32_list(const Config* cfg, const char* key, f32* out, u32 max_count);

void config_selftest(void);
