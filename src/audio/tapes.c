#include "audio/tapes.h"
#include "core/log.h"
#include "platform/platform.h"

#include <stdio.h>
#include <string.h>

typedef struct TapeTrack {
    char name[TAPE_NAME_MAX + 1];
    char path[320];
    b32 on_relay;
    u32 file_bytes;
} TapeTrack;

static TapeTrack s_tracks[TAPE_TRACK_MAX];
static u32 s_track_count;

static b32 has_audio_ext(const char* name)
{
    u32 len = (u32)strlen(name);
    if (len < 5) {
        return 0;
    }
    const char* ext = name + len - 4;
    return _stricmp(ext, ".mp3") == 0 || _stricmp(ext, ".wav") == 0;
}

static void display_name(const char* file, char* out, u32 out_size)
{
    const char* end = file + strlen(file);
    for (const char* p = end; p > file; p--) {
        if (p[-1] == '.') {
            end = p - 1;
            break;
        }
    }
    u32 n = 0;
    for (const char* p = file; p < end; p++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (c == '_') {
            c = ' ';
        }
        u32 need = 1;
        if ((u8)c >= 0xF0) {
            need = 4;
        } else if ((u8)c >= 0xE0) {
            need = 3;
        } else if ((u8)c >= 0xC0) {
            need = 2;
        }
        if (n + need + 1 > out_size) {
            break;
        }
        out[n++] = c;
    }
    while (n && (u8)out[n - 1] == ' ') {
        n--;
    }
    out[n] = 0;
}

static u32 real_file_bytes(const char* path)
{
    FILE* f = (FILE*)platform_fopen(path, "rb");
    if (!f) {
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    return size > 0 ? (u32)size : 0;
}

static void scan_dir(const char* dir, b32 on_relay)
{
    static PlatformDirEntry entries[TAPE_TRACK_MAX];
    u32 count = platform_list_dir(dir, entries, TAPE_TRACK_MAX);
    for (u32 i = 1; i < count; i++) {
        PlatformDirEntry key = entries[i];
        u32 j = i;
        while (j > 0 && _stricmp(entries[j - 1].name, key.name) > 0) {
            entries[j] = entries[j - 1];
            j--;
        }
        entries[j] = key;
    }
    for (u32 i = 0; i < count && s_track_count < TAPE_TRACK_MAX; i++) {
        if (entries[i].is_dir || !has_audio_ext(entries[i].name)) {
            continue;
        }
        TapeTrack* t = &s_tracks[s_track_count];
        snprintf(t->path, sizeof(t->path), "%s/%s", dir, entries[i].name);
        display_name(entries[i].name, t->name, sizeof(t->name));
        t->on_relay = on_relay;
        u32 real = real_file_bytes(t->path);
        u32 fict = real / 56;
        t->file_bytes = fict < 20480 ? 20480 : (fict > 98304 ? 98304 : fict);
        s_track_count++;
    }
}

void tapes_init(void)
{
    s_track_count = 0;
    scan_dir("assets/tapes/found", 0);
    scan_dir("assets/tapes/relay", 1);
    scan_dir("tapes", 1);
    log_info("tapes: %u tracks registered", s_track_count);
}

u32 tapes_count(void)
{
    return s_track_count;
}

static b32 track_valid(i32 track)
{
    return track >= 0 && (u32)track < s_track_count;
}

const char* tapes_name(i32 track)
{
    return track_valid(track) ? s_tracks[track].name : "UNKNOWN";
}

const char* tapes_path(i32 track)
{
    return track_valid(track) ? s_tracks[track].path : "";
}

b32 tapes_on_relay(i32 track)
{
    return track_valid(track) && s_tracks[track].on_relay;
}

u32 tapes_file_bytes(i32 track)
{
    return track_valid(track) ? s_tracks[track].file_bytes : 0;
}

const char* tape_label(i32 aux)
{
    if (aux <= 0) {
        return "BLANK";
    }
    return track_valid(aux - 1) ? s_tracks[aux - 1].name : "UNKNOWN";
}
