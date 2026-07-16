#include "core/config.h"
#include "core/arena.h"
#include "core/log.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static char* str_trim(char* s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r') {
        s++;
    }
    char* end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
        end--;
    }
    *end = 0;
    return s;
}

b32 config_parse(Config* cfg, struct Arena* arena, const char* text)
{
    cfg->count = 0;
    u64 len = strlen(text);
    char* buf = arena_push_array(arena, char, len + 1);
    memcpy(buf, text, len + 1);

    char section[64] = {0};
    char* cursor = buf;
    while (cursor) {
        char* line = cursor;
        char* newline = strchr(cursor, '\n');
        if (newline) {
            *newline = 0;
            cursor = newline + 1;
        } else {
            cursor = 0;
        }

        char* comment = strchr(line, '#');
        if (comment) {
            *comment = 0;
        }
        line = str_trim(line);
        if (!line[0]) {
            continue;
        }

        if (line[0] == '[') {
            char* close = strchr(line, ']');
            if (!close) {
                log_warn("config: malformed section header: %s", line);
                continue;
            }
            *close = 0;
            snprintf(section, sizeof(section), "%s", str_trim(line + 1));
            continue;
        }

        char* equals = strchr(line, '=');
        if (!equals) {
            log_warn("config: malformed line: %s", line);
            continue;
        }
        *equals = 0;
        char* key = str_trim(line);
        char* value = str_trim(equals + 1);
        if (!key[0]) {
            continue;
        }
        if (cfg->count >= CONFIG_MAX_ENTRIES) {
            log_error("config: too many entries (max %d)", CONFIG_MAX_ENTRIES);
            return 0;
        }

        u64 full_len = strlen(section) + 1 + strlen(key) + 1;
        char* full_key = arena_push_array(arena, char, full_len);
        if (section[0]) {
            snprintf(full_key, full_len, "%s.%s", section, key);
        } else {
            snprintf(full_key, full_len, "%s", key);
        }
        cfg->entries[cfg->count].key = full_key;
        cfg->entries[cfg->count].value = value;
        cfg->count++;
    }
    return 1;
}

const char* config_get_str(const Config* cfg, const char* key, const char* fallback)
{
    for (u32 i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->entries[i].key, key) == 0) {
            return cfg->entries[i].value;
        }
    }
    return fallback;
}

f32 config_get_f32(const Config* cfg, const char* key, f32 fallback)
{
    const char* value = config_get_str(cfg, key, 0);
    if (!value) {
        return fallback;
    }
    return (f32)atof(value);
}

i32 config_get_i32(const Config* cfg, const char* key, i32 fallback)
{
    const char* value = config_get_str(cfg, key, 0);
    if (!value) {
        return fallback;
    }
    return atoi(value);
}

Vec3 config_get_vec3(const Config* cfg, const char* key, Vec3 fallback)
{
    const char* value = config_get_str(cfg, key, 0);
    if (!value) {
        return fallback;
    }
    Vec3 result = fallback;
    char* end = 0;
    result.x = strtof(value, &end);
    result.y = strtof(end, &end);
    result.z = strtof(end, &end);
    return result;
}

u32 config_get_f32_list(const Config* cfg, const char* key, f32* out, u32 max_count)
{
    const char* value = config_get_str(cfg, key, 0);
    if (!value) {
        return 0;
    }
    u32 count = 0;
    const char* p = value;
    while (count < max_count) {
        char* end = 0;
        f32 parsed = strtof(p, &end);
        if (end == p) {
            break;
        }
        out[count++] = parsed;
        p = end;
    }
    return count;
}

void config_selftest(void)
{
    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    const char* text =
        "top = 1.5\n"
        "[body]\n"
        "mass = 1400 # comment\n"
        "com = 0.1 -0.2 0.3\n"
        "\n"
        "[gears]\n"
        "ratios = 3.6 2.1 1.4\n"
        "count = 3\n";
    Config cfg;
    b32 ok = config_parse(&cfg, &g_frame_arena, text);
    ASSERT(ok);
    ASSERT(cfg.count == 5);
    ASSERT(config_get_f32(&cfg, "top", 0.0f) == 1.5f);
    ASSERT(config_get_f32(&cfg, "body.mass", 0.0f) == 1400.0f);
    ASSERT(config_get_i32(&cfg, "gears.count", 0) == 3);
    ASSERT(config_get_f32(&cfg, "missing", 7.0f) == 7.0f);
    Vec3 com = config_get_vec3(&cfg, "body.com", vec3_zero());
    ASSERT(f_abs(com.x - 0.1f) < 1e-6f && f_abs(com.y + 0.2f) < 1e-6f && f_abs(com.z - 0.3f) < 1e-6f);
    f32 ratios[8];
    u32 ratio_count = config_get_f32_list(&cfg, "gears.ratios", ratios, 8);
    ASSERT(ratio_count == 3 && ratios[0] == 3.6f && ratios[2] == 1.4f);
    arena_temp_end(temp);
    log_info("config selftest passed");
}
