#pragma once

#include "core/types.h"

typedef struct Arena {
    u8* base;
    u64 reserved;
    u64 committed;
    u64 used;
} Arena;

typedef struct ArenaTemp {
    Arena* arena;
    u64 used;
} ArenaTemp;

extern Arena g_perm_arena;
extern Arena g_frame_arena;

void  arena_init(Arena* arena, u64 reserve_size);
void  arena_release(Arena* arena);
void  arena_reset(Arena* arena);
void* arena_push_size(Arena* arena, u64 size, u64 alignment);

ArenaTemp arena_temp_begin(Arena* arena);
void      arena_temp_end(ArenaTemp temp);

#define arena_push(arena, type) ((type*)arena_push_size((arena), sizeof(type), _Alignof(type)))
#define arena_push_array(arena, type, count) ((type*)arena_push_size((arena), sizeof(type) * (u64)(count), _Alignof(type)))
