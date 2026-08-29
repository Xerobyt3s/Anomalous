#pragma once

#include "core/fixed_string.h"
#include "core/types.h"

#include <string>
#include <string_view>
#include <vector>

namespace anom {

class Arena;
class FileWatcher;

class ShaderLibrary {
public:
    static constexpr u32 kMaxShaders = 32;
    static constexpr u32 kMaxDependencies = 16;
    static constexpr i32 kMaxIncludeDepth = 8;

    void init(FileWatcher& watcher, Arena& scratch);
    void shutdown();

    u32 program(std::string_view name);
    u32 reload_count() const { return reload_count_; }
    u32 count() const { return count_; }

    static bool preprocess(std::string_view path, std::string& out,
                           std::vector<std::string>& dependencies,
                           std::vector<std::string>& included, i32 depth = 0);

private:
    struct Entry {
        FixedString<32> name;
        FixedString<128> vert_path;
        FixedString<128> frag_path;
        u32 program = 0;
        u32 watches[kMaxDependencies] = {};
        u32 watch_count = 0;
        ShaderLibrary* owner = nullptr;
        bool used = false;
    };

    static void on_source_changed(void* user, std::string_view path);

    u32 build(Entry& entry, std::vector<std::string>* out_dependencies);
    u32 compile_stage(u32 stage_type, std::string_view path,
                      std::vector<std::string>& dependencies);
    void rewatch(Entry& entry, const std::vector<std::string>& dependencies);

    Entry entries_[kMaxShaders];
    FileWatcher* watcher_ = nullptr;
    Arena* scratch_ = nullptr;
    u32 count_ = 0;
    u32 reload_count_ = 0;
};

} // namespace anom
