#include "core/arena.h"
#include "core/log.h"

#include <cstring>
#include <utility>

#include <windows.h>

namespace anom {
namespace {

constexpr u64 kCommitChunk = 1ull << 20;

constexpr u64 align_up(u64 value, u64 alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace

Arena::Arena(u64 reserve_size)
{
    reserve(reserve_size);
}

Arena::~Arena()
{
    release();
}

Arena::Arena(Arena&& other) noexcept
    : base_(std::exchange(other.base_, nullptr))
    , reserved_(std::exchange(other.reserved_, 0))
    , committed_(std::exchange(other.committed_, 0))
    , used_(std::exchange(other.used_, 0))
{
}

Arena& Arena::operator=(Arena&& other) noexcept
{
    if (this != &other) {
        release();
        base_ = std::exchange(other.base_, nullptr);
        reserved_ = std::exchange(other.reserved_, 0);
        committed_ = std::exchange(other.committed_, 0);
        used_ = std::exchange(other.used_, 0);
    }
    return *this;
}

bool Arena::reserve(u64 reserve_size)
{
    release();
    base_ = static_cast<u8*>(VirtualAlloc(nullptr, reserve_size, MEM_RESERVE, PAGE_READWRITE));
    if (!base_) {
        log_error("arena: failed to reserve %llu bytes", static_cast<unsigned long long>(reserve_size));
        return false;
    }
    reserved_ = reserve_size;
    committed_ = 0;
    used_ = 0;
    return true;
}

void Arena::release()
{
    if (base_) {
        VirtualFree(base_, 0, MEM_RELEASE);
    }
    base_ = nullptr;
    reserved_ = 0;
    committed_ = 0;
    used_ = 0;
}

void* Arena::push_bytes(u64 size, u64 alignment)
{
    ASSERT(base_);
    const u64 aligned_used = align_up(used_, alignment);
    const u64 new_used = aligned_used + size;
    if (new_used > reserved_) {
        log_error("arena: out of reserved space, wanted %llu with %llu of %llu used",
                  static_cast<unsigned long long>(size),
                  static_cast<unsigned long long>(used_),
                  static_cast<unsigned long long>(reserved_));
        ASSERT(false);
        return nullptr;
    }
    if (new_used > committed_) {
        u64 new_committed = align_up(new_used, kCommitChunk);
        if (new_committed > reserved_) {
            new_committed = reserved_;
        }
        void* committed = VirtualAlloc(base_ + committed_, new_committed - committed_,
                                       MEM_COMMIT, PAGE_READWRITE);
        if (!committed) {
            log_error("arena: failed to commit %llu bytes",
                      static_cast<unsigned long long>(new_committed - committed_));
            ASSERT(false);
            return nullptr;
        }
        committed_ = new_committed;
    }
    void* result = base_ + aligned_used;
    used_ = new_used;
    std::memset(result, 0, size);
    return result;
}

void Arena::rewind(u64 mark)
{
    ASSERT(mark <= used_);
    if (mark <= used_) {
        used_ = mark;
    }
}

} // namespace anom
