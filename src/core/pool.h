#pragma once

#include "core/types.h"

struct Arena;

typedef struct Handle {
    u32 idx;
    u32 gen;
} Handle;

#define HANDLE_INVALID ((Handle){ 0xFFFFFFFFu, 0 })

typedef struct Pool {
    u8* items;
    u32* gens;
    u8* alive;
    u32* free_stack;
    u32 item_size;
    u32 capacity;
    u32 free_count;
    u32 count;
} Pool;

void   pool_init(Pool* pool, struct Arena* arena, u32 item_size, u32 capacity);
Handle pool_alloc(Pool* pool);
void   pool_free(Pool* pool, Handle handle);
void*  pool_get(Pool* pool, Handle handle);
void*  pool_at(Pool* pool, u32 idx);

static inline b32 handle_valid(Handle handle)
{
    return handle.idx != 0xFFFFFFFFu;
}
