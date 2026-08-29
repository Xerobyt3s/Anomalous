#include "assets/watcher.h"
#include "core/log.h"
#include "platform/filesystem.h"

namespace anom {

FileWatcher::FileWatcher()
    : stat_(&fs::file_mtime)
{
}

u32 FileWatcher::add(std::string_view path, Callback callback, void* user)
{
    for (u32 i = 0; i < kMaxWatches; i++) {
        Watch& watch = watches_[i];
        if (watch.used) {
            continue;
        }
        watch.path.assign(path);
        watch.callback = callback;
        watch.user = user;
        watch.mtime = stat_(watch.path.view());
        watch.used = true;
        count_++;
        return i;
    }
    log_error("watcher: exhausted at capacity %u, dropping %.*s", kMaxWatches,
              static_cast<int>(path.size()), path.data());
    return kInvalidId;
}

void FileWatcher::remove(u32 id)
{
    if (id >= kMaxWatches || !watches_[id].used) {
        return;
    }
    watches_[id] = Watch{};
    count_--;
}

u32 FileWatcher::poll(f64 now)
{
    if (now < next_poll_) {
        return 0;
    }
    next_poll_ = now + interval_;
    return check_all();
}

u32 FileWatcher::check_all()
{
    u32 fired = 0;
    for (u32 i = 0; i < kMaxWatches; i++) {
        Watch& watch = watches_[i];
        if (!watch.used) {
            continue;
        }
        const i64 mtime = stat_(watch.path.view());
        if (mtime == watch.mtime) {
            continue;
        }
        watch.mtime = mtime;
        if (watch.callback) {
            watch.callback(watch.user, watch.path.view());
            fired++;
        }
    }
    return fired;
}

} // namespace anom
