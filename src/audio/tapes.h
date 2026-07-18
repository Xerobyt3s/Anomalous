#pragma once

#include "core/types.h"

#define TAPE_TRACK_MAX 40
#define TAPE_NAME_MAX 20

void        tapes_init(void);
u32         tapes_count(void);
const char* tapes_name(i32 track);
const char* tapes_path(i32 track);
b32         tapes_on_relay(i32 track);
u32         tapes_file_bytes(i32 track);
const char* tape_label(i32 aux);
