#pragma once

#include "core/types.h"

#define COMMS_MSG_MAX 24
#define COMMS_SPRITE_ROWS 14
#define COMMS_SPRITE_COLS 30
#define COMMS_PAGES_MAX 6

typedef struct CommsBriefing {
    const char* speaker;
    const char* sprite[COMMS_SPRITE_ROWS];
    i32 sprite_rows;
    const char* pages[COMMS_PAGES_MAX];
    i32 page_count;
} CommsBriefing;

typedef struct CommsMsg {
    const char* from;
    const char* sub;
    const char* body;
    const char* att_site;
    const char* att_part;
    f32 att_part_cond;
    b32 has_briefing;
    CommsBriefing briefing;
    b32 delivered;
    b32 read;
    b32 site_downloaded;
    b32 part_claimed;
    i32 cycle;
} CommsMsg;

typedef struct Comms {
    CommsMsg msgs[COMMS_MSG_MAX];
    i32 count;
} Comms;

void comms_init(Comms* comms);
i32  comms_delivered_count(const Comms* comms);
i32  comms_unread_count(const Comms* comms);
CommsMsg* comms_inbox_get(Comms* comms, i32 number);
