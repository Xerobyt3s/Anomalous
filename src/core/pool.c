#include "core/pool.h"
#include "core/arena.h"

#include <string.h>

void pool_init(Pool* pool, struct Arena* arena, u32 item_size, u32 capacity)
{
    pool->items = (u8*)arena_push_size(arena, (u64)item_size * capacity, 16);
    pool->gens = arena_push_array(arena, u32, capacity);
    pool->alive = arena_push_array(arena, u8, capacity);
    pool->free_stack = arena_push_array(arena, u32, capacity);
    pool->item_size = item_size;
    pool->capacity = capacity;
    pool->free_count = capacity;
    pool->count = 0;
    for (u32 i = 0; i < capacity; i++) {
        pool->gens[i] = 1;
        pool->alive[i] = 0;
        pool->free_stack[i] = capacity - 1 - i;
    }
}

Handle pool_alloc(Pool* pool)
{
    ASSERT(pool->free_count > 0);
    if (!pool->free_count) {
        return HANDLE_INVALID;
    }
    u32 idx = pool->free_stack[--pool->free_count];
    pool->alive[idx] = 1;
    pool->count++;
    memset(pool->items + (u64)idx * pool->item_size, 0, pool->item_size);
    Handle handle = { idx, pool->gens[idx] };
    return handle;
}

void pool_free(Pool* pool, Handle handle)
{
    if (!pool_get(pool, handle)) {
        return;
    }
    pool->alive[handle.idx] = 0;
    pool->gens[handle.idx]++;
    pool->free_stack[pool->free_count++] = handle.idx;
    pool->count--;
}

void* pool_get(Pool* pool, Handle handle)
{
    if (handle.idx >= pool->capacity || !pool->alive[handle.idx] || pool->gens[handle.idx] != handle.gen) {
        return 0;
    }
    return pool->items + (u64)handle.idx * pool->item_size;
}

void* pool_at(Pool* pool, u32 idx)
{
    if (idx >= pool->capacity || !pool->alive[idx]) {
        return 0;
    }
    return pool->items + (u64)idx * pool->item_size;
}
