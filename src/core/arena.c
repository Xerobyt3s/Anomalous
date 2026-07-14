#include "core/arena.h"
#include "core/log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

Arena g_perm_arena;
Arena g_frame_arena;

#define ARENA_COMMIT_CHUNK MEGABYTES(1)

static u64 align_up(u64 value, u64 alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

void arena_init(Arena* arena, u64 reserve_size)
{
    arena->base = (u8*)VirtualAlloc(0, reserve_size, MEM_RESERVE, PAGE_READWRITE);
    if (!arena->base) {
        log_error("arena: failed to reserve %llu bytes", (unsigned long long)reserve_size);
        ASSERT(0);
    }
    arena->reserved = reserve_size;
    arena->committed = 0;
    arena->used = 0;
}

void arena_release(Arena* arena)
{
    if (arena->base) {
        VirtualFree(arena->base, 0, MEM_RELEASE);
    }
    arena->base = 0;
    arena->reserved = 0;
    arena->committed = 0;
    arena->used = 0;
}

void arena_reset(Arena* arena)
{
    arena->used = 0;
}

void* arena_push_size(Arena* arena, u64 size, u64 alignment)
{
    ASSERT(arena->base);
    u64 aligned_used = align_up(arena->used, alignment);
    u64 new_used = aligned_used + size;
    ASSERT(new_used <= arena->reserved);
    if (new_used > arena->committed) {
        u64 new_committed = align_up(new_used, ARENA_COMMIT_CHUNK);
        if (new_committed > arena->reserved) {
            new_committed = arena->reserved;
        }
        void* committed = VirtualAlloc(arena->base + arena->committed, new_committed - arena->committed, MEM_COMMIT, PAGE_READWRITE);
        if (!committed) {
            log_error("arena: failed to commit %llu bytes", (unsigned long long)(new_committed - arena->committed));
            ASSERT(0);
        }
        arena->committed = new_committed;
    }
    void* result = arena->base + aligned_used;
    arena->used = new_used;
    memset(result, 0, size);
    return result;
}

ArenaTemp arena_temp_begin(Arena* arena)
{
    ArenaTemp temp;
    temp.arena = arena;
    temp.used = arena->used;
    return temp;
}

void arena_temp_end(ArenaTemp temp)
{
    temp.arena->used = temp.used;
}
