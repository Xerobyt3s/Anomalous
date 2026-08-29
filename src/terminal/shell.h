#pragma once

#include "core/fixed_string.h"
#include "core/types.h"
#include "terminal/fs.h"
#include "terminal/screen.h"

#include <string_view>

namespace anom {

class DiskStore;

inline constexpr u32 kTermInputMax = 48;
inline constexpr u32 kTermHistory = 8;
inline constexpr u32 kTermPromptMax = 80;
inline constexpr u32 kShellMaxTokens = 3;

enum class ShellKey : u32 {
    Enter,
    Backspace,
    Delete,
    Left,
    Right,
    Home,
    End,
    Up,
    Down,
};

struct ShellRequest {
    FsExe launch = FsExe::None;
    FsRef launch_ref{kFsDriveA, -1};
    i32 view_pic = -1;
    FixedString<kFsNameMax + 1> view_name;
    bool power_off = false;

    bool any() const
    {
        return launch_ref.node >= 0 || view_pic > 0 || power_off;
    }
};

class Shell {
public:
    void init(DiskStore* disks);
    void reset();

    Fs& fs() { return fs_; }
    const Fs& fs() const { return fs_; }
    Screen& screen() { return screen_; }
    const Screen& screen() const { return screen_; }

    void key_char(char c);
    void key(ShellKey k);

    void run(std::string_view command);
    void prompt(char* buf, u32 size) const;

    std::string_view input() const { return {input_, input_len_}; }
    u32 cursor() const { return input_cursor_; }
    FsRef cwd() const { return FsRef{cwd_drive_, cwd_node_}; }
    bool awaiting_format() const { return format_drive_ >= 0; }

    ShellRequest take_request();

    static void split_path(std::string_view path, FixedString<kTermInputMax + 1>& head,
                           FixedString<kTermInputMax + 1>& tail);

    void set_disk(i32 disk);
    i32 disk() const { return disk_; }
    void set_camera_mounted(bool on);
    void set_tower_linked(bool on);

private:
    struct Tokens {
        FixedString<kTermInputMax + 1> at[kShellMaxTokens];
        u32 count = 0;
    };

    static Tokens tokenize(std::string_view command);
    static bool is_command(std::string_view token, std::string_view name);

    bool check_path(std::string_view path, FsRef& out);
    bool resolve_target(std::string_view path, FsRef src, FsRef& out_dir,
                        FixedString<kTermInputMax + 1>& out_name);
    void report_transfer(FsError err, const char* verb);

    void submit();
    bool try_exe(std::string_view name);

    void cmd_dir(std::string_view path);
    void cmd_cd(std::string_view path);
    void cmd_type(std::string_view path);
    void cmd_view(std::string_view path);
    void cmd_copy(std::string_view src, std::string_view dst);
    void cmd_move(std::string_view src, std::string_view dst);
    void cmd_del(std::string_view path);
    void cmd_mkdir(std::string_view path);
    void cmd_rmdir(std::string_view path);
    void cmd_chkdsk(std::string_view arg);
    void cmd_format(std::string_view arg);
    void type_binary(std::string_view name);

    Fs fs_;
    Screen screen_;
    DiskStore* disks_ = nullptr;

    char input_[kTermInputMax + 1] = {};
    u32 input_len_ = 0;
    u32 input_cursor_ = 0;

    char history_[kTermHistory][kTermInputMax + 1] = {};
    u32 history_count_ = 0;
    i32 history_pos_ = -1;

    i32 cwd_drive_ = kFsDriveA;
    i32 cwd_node_ = 0;
    i32 format_drive_ = -1;
    i32 disk_ = -1;

    ShellRequest request_;
};

} // namespace anom
