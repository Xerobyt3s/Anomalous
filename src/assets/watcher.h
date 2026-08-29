#pragma once

#include "core/fixed_string.h"
#include "core/types.h"

#include <string_view>

namespace anom {

class FileWatcher {
public:
    static constexpr u32 kMaxWatches = 256;
    static constexpr u32 kInvalidId = 0xFFFFFFFFu;

    using Callback = void (*)(void* user, std::string_view path);
    using StatFn = i64 (*)(std::string_view path);

    FileWatcher();

    void set_stat_fn(StatFn fn) { stat_ = fn; }
    void set_interval(f64 seconds) { interval_ = seconds; }

    u32 add(std::string_view path, Callback callback, void* user);
    void remove(u32 id);

    u32 poll(f64 now);
    u32 check_all();

    u32 count() const { return count_; }

private:
    struct Watch {
        FixedString<192> path;
        Callback callback = nullptr;
        void* user = nullptr;
        i64 mtime = 0;
        bool used = false;
    };

    Watch watches_[kMaxWatches];
    StatFn stat_;
    f64 interval_ = 1.0;
    f64 next_poll_ = 0.0;
    u32 count_ = 0;
};

} // namespace anom
