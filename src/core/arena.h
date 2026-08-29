#pragma once

#include "core/types.h"

#include <type_traits>

namespace anom {

class Arena {
public:
    Arena() = default;
    explicit Arena(u64 reserve_size);
    ~Arena();

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&& other) noexcept;
    Arena& operator=(Arena&& other) noexcept;

    bool reserve(u64 reserve_size);
    void release();

    void* push_bytes(u64 size, u64 alignment);

    template<typename T>
    T* push()
    {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(std::is_trivially_destructible_v<T>);
        return static_cast<T*>(push_bytes(sizeof(T), alignof(T)));
    }

    template<typename T>
    T* push_array(u64 count)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(std::is_trivially_destructible_v<T>);
        return static_cast<T*>(push_bytes(sizeof(T) * count, alignof(T)));
    }

    void reset() { used_ = 0; }
    void rewind(u64 mark);

    u64 used() const { return used_; }
    u64 committed() const { return committed_; }
    u64 reserved() const { return reserved_; }
    bool valid() const { return base_ != nullptr; }

private:
    u8* base_ = nullptr;
    u64 reserved_ = 0;
    u64 committed_ = 0;
    u64 used_ = 0;
};

class ArenaScope {
public:
    explicit ArenaScope(Arena& arena) : arena_(&arena), mark_(arena.used()) {}
    ~ArenaScope() { arena_->rewind(mark_); }

    ArenaScope(const ArenaScope&) = delete;
    ArenaScope& operator=(const ArenaScope&) = delete;
    ArenaScope(ArenaScope&&) = delete;
    ArenaScope& operator=(ArenaScope&&) = delete;

    u64 mark() const { return mark_; }

private:
    Arena* arena_;
    u64 mark_;
};

} // namespace anom
