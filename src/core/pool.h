#pragma once

#include "core/arena.h"
#include "core/handle.h"
#include "core/log.h"
#include "core/types.h"

#include <cstring>
#include <span>
#include <type_traits>

namespace anom {

template<typename T>
class Pool {
public:
    static_assert(std::is_trivially_default_constructible_v<T>);
    static_assert(std::is_trivially_destructible_v<T>);

    using HandleType = Handle<T>;

    void init(Arena& arena, u32 capacity, const char* debug_name = "pool")
    {
        items_ = arena.push_array<T>(capacity);
        gens_ = arena.push_array<u32>(capacity);
        alive_ = arena.push_array<u8>(capacity);
        free_stack_ = arena.push_array<u32>(capacity);
        dense_ = arena.push_array<u32>(capacity);
        dense_pos_ = arena.push_array<u32>(capacity);
        capacity_ = capacity;
        free_count_ = capacity;
        count_ = 0;
        exhausted_reported_ = false;
        debug_name_ = debug_name;
        for (u32 i = 0; i < capacity; i++) {
            gens_[i] = 1;
            alive_[i] = 0;
            free_stack_[i] = capacity - 1 - i;
        }
    }

    HandleType alloc()
    {
        if (free_count_ == 0) {
            if (!exhausted_reported_) {
                exhausted_reported_ = true;
                log_error("%s: exhausted at capacity %u", debug_name_, capacity_);
            }
            return HandleType{};
        }
        const u32 idx = free_stack_[--free_count_];
        alive_[idx] = 1;
        dense_[count_] = idx;
        dense_pos_[idx] = count_;
        count_++;
        std::memset(&items_[idx], 0, sizeof(T));
        return HandleType{idx, gens_[idx]};
    }

    void free(HandleType handle)
    {
        if (!get(handle)) {
            return;
        }
        const u32 idx = handle.idx;
        const u32 pos = dense_pos_[idx];
        const u32 last = count_ - 1;
        const u32 moved = dense_[last];
        dense_[pos] = moved;
        dense_pos_[moved] = pos;
        count_--;

        alive_[idx] = 0;
        gens_[idx]++;
        free_stack_[free_count_++] = idx;
    }

    T* get(HandleType handle)
    {
        if (handle.idx >= capacity_ || !alive_[handle.idx] || gens_[handle.idx] != handle.gen) {
            return nullptr;
        }
        return &items_[handle.idx];
    }

    const T* get(HandleType handle) const
    {
        return const_cast<Pool*>(this)->get(handle);
    }

    T* at(u32 idx)
    {
        if (idx >= capacity_ || !alive_[idx]) {
            return nullptr;
        }
        return &items_[idx];
    }

    const T* at(u32 idx) const { return const_cast<Pool*>(this)->at(idx); }

    HandleType handle_at(u32 idx) const
    {
        if (idx >= capacity_ || !alive_[idx]) {
            return HandleType{};
        }
        return HandleType{idx, gens_[idx]};
    }

    std::span<const u32> live_indices() const { return {dense_, count_}; }

    T& operator[](u32 dense_index) { return items_[dense_[dense_index]]; }
    const T& operator[](u32 dense_index) const { return items_[dense_[dense_index]]; }

    void clear()
    {
        while (count_ > 0) {
            free(handle_at(dense_[count_ - 1]));
        }
    }

    u32 count() const { return count_; }
    u32 capacity() const { return capacity_; }

private:
    T* items_ = nullptr;
    u32* gens_ = nullptr;
    u8* alive_ = nullptr;
    u32* free_stack_ = nullptr;
    u32* dense_ = nullptr;
    u32* dense_pos_ = nullptr;
    u32 capacity_ = 0;
    u32 free_count_ = 0;
    u32 count_ = 0;
    bool exhausted_reported_ = false;
    const char* debug_name_ = "pool";
};

} // namespace anom
