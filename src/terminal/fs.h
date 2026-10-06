#pragma once

#include "core/fixed_string.h"
#include "core/types.h"

#include <span>
#include <string_view>

namespace anom {

inline constexpr i32 kFsDriveCount = 4;
inline constexpr i32 kFsDriveA = 0;
inline constexpr i32 kFsDriveB = 1;
inline constexpr i32 kFsDriveC = 2;
inline constexpr i32 kFsDriveD = 3;

inline constexpr u32 kFsNameMax = 12;
inline constexpr i32 kFsDriveNodes = 96;
inline constexpr u32 kFsPoolSize = 24576;
inline constexpr u32 kFsPathMax = 64;
inline constexpr u32 kFsLabelMax = 15;
inline constexpr u32 kFsPathDepth = 16;
inline constexpr u32 kFsSystemCapacity = 362496;

enum class FsExe : i32 {
    None = 0,
    Status,
    Map,
    Link,
    Comms,
    Av,
    Toy,
    Video,
    Breach,
    Gate,
    Tapes,
    Dev,
    Travel,
    Synth,
};

FsExe fs_exe_from_name(std::string_view name);
std::string_view fs_exe_name(FsExe exe);

enum class FsError : u32 {
    Ok,
    NotFound,
    NotReady,
    IsDir,
    NotDir,
    NotEmpty,
    BadName,
    Full,
    NoSpace,
    Self,
};

std::string_view fs_error_text(FsError err);

struct FsNode {
    FixedString<kFsNameMax + 1> name;
    bool used = false;
    bool is_dir = false;
    i32 parent = -1;
    u32 size = 0;
    FsExe exe = FsExe::None;
    i32 pic = -1;
    i32 trk = -1;
    bool infected = false;
    bool corrupted = false;
    std::string_view run_text;
    std::string_view rom_text;
    u32 pool_off = 0;
    u32 pool_len = 0;
};

struct FsDrive {
    bool mounted = false;
    FixedString<kFsLabelMax + 1> label;
    u32 capacity = 0;
    FsNode nodes[kFsDriveNodes];
    char pool[kFsPoolSize] = {};
    u32 pool_used = 0;
};

struct FsRef {
    i32 drive = 0;
    i32 node = -1;

    bool operator==(const FsRef& other) const
    {
        return drive == other.drive && node == other.node;
    }
};

class Fs {
public:
    void init();

    void mount(i32 drive, std::string_view label, u32 capacity);
    void unmount(i32 drive);
    void format(i32 drive, std::string_view label);
    bool mounted(i32 drive) const;

    u32 used_bytes(i32 drive) const;
    u32 free_bytes(i32 drive) const;

    static FsRef root(i32 drive) { return FsRef{drive, 0}; }
    bool valid(FsRef ref) const;
    const FsNode* node(FsRef ref) const;
    FsNode* node_mut(FsRef ref);

    FsRef resolve(FsRef cwd, std::string_view path) const;
    static i32 path_drive(std::string_view path);
    void path_string(FsRef ref, char* buf, u32 size) const;
    std::string_view text(FsRef ref) const;

    FsRef mkdir(FsRef dir, std::string_view name);
    FsRef mkfile_rom(FsRef dir, std::string_view name, std::string_view rom_text, u32 size,
                     FsExe exe);
    FsRef mkfile_data(FsRef dir, std::string_view name, std::string_view data);
    FsError copy(FsRef src, FsRef dst_dir, std::string_view dst_name);
    FsError move(FsRef src, FsRef dst_dir, std::string_view dst_name);
    FsError remove(FsRef ref);
    FsError rmdir(FsRef ref);

    FsDrive& drive(i32 index) { return drives_[index]; }
    const FsDrive& drive(i32 index) const { return drives_[index]; }

private:
    static i32 find_child(const FsDrive& d, i32 dir, std::string_view name);
    static i32 alloc_node(FsDrive& d);
    static void reset_drive(FsDrive& d);

    FsDrive drives_[kFsDriveCount];
};

bool fs_name_valid(std::string_view name);

} // namespace anom
