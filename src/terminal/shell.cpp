#include "terminal/shell.h"
#include "terminal/disks.h"

#include <cstdio>
#include <cstring>

namespace anom {
namespace {

constexpr std::string_view kHelpText =
    "DR-OS 2.2 SHELL COMMANDS:\n"
    "  LS [PATH]       list directory\n"
    "  CD PATH         change directory\n"
    "  TYPE FILE       display file contents\n"
    "  VIEW FILE       display image file\n"
    "  COPY SRC DST    copy file\n"
    "  MOVE SRC DST    move file\n"
    "  MKDIR PATH      create directory\n"
    "  RMDIR PATH      remove empty directory\n"
    "  DEL FILE        delete file\n"
    "  RUN FILE        run program (or just type its name)\n"
    "  CHKDSK [X:]     disk space report\n"
    "  FORMAT X:       erase a drive\n"
    "  CLS / VER / OFF\n"
    "PROGRAMS ARE .EXE FILES ON DISK. USE LS TO SEE THEM.\n";

char upper(char c)
{
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

bool is_separator(char c)
{
    return c == '\\' || c == '/';
}

} // namespace

void Shell::init(DiskStore* disks)
{
    disks_ = disks;
    reset();
}

void Shell::reset()
{
    fs_.init();
    if (disks_) {
        disks_->mount_system(fs_);
    }
    screen_.reset();

    input_[0] = 0;
    input_len_ = 0;
    input_cursor_ = 0;
    history_count_ = 0;
    history_pos_ = -1;
    cwd_drive_ = kFsDriveA;
    cwd_node_ = 0;
    format_drive_ = -1;
    disk_ = -1;
    request_ = ShellRequest{};
}

ShellRequest Shell::take_request()
{
    const ShellRequest out = request_;
    request_ = ShellRequest{};
    return out;
}

void Shell::set_disk(i32 disk)
{
    if (disk == disk_) {
        return;
    }
    if (disk_ >= 0 && disks_ && fs_.mounted(kFsDriveB)) {
        disks_->store(disk_, fs_.drive(kFsDriveB));
    }
    disk_ = disk;

    if (disk_ >= 0 && disks_) {
        disks_->load(disk_, fs_.drive(kFsDriveB));
        screen_.printf("DRIVE B: MEDIA INSERTED (%.*s)\n",
                       static_cast<int>(disk_label(disk_).size()), disk_label(disk_).data());
    } else {
        fs_.unmount(kFsDriveB);
        screen_.print("DRIVE B: MEDIA REMOVED\n");
        if (cwd_drive_ == kFsDriveB) {
            cwd_drive_ = kFsDriveA;
            cwd_node_ = 0;
        }
    }
}

void Shell::set_camera_mounted(bool on)
{
    if (on == fs_.mounted(kFsDriveC)) {
        return;
    }
    if (on) {
        if (disks_) {
            disks_->load_camera(fs_.drive(kFsDriveC));
        }
        screen_.print("DRIVE C: CAMERA FILM MOUNTED\n");
        return;
    }
    if (disks_) {
        disks_->store_camera(fs_.drive(kFsDriveC));
    }
    fs_.unmount(kFsDriveC);
    screen_.print("DRIVE C: DISCONNECTED\n");
    if (cwd_drive_ == kFsDriveC) {
        cwd_drive_ = kFsDriveA;
        cwd_node_ = 0;
    }
}

void Shell::set_tower_linked(bool on)
{
    if (on == fs_.mounted(kFsDriveD)) {
        return;
    }
    if (on) {
        if (disks_) {
            disks_->load_tower(fs_.drive(kFsDriveD));
        }
        screen_.print("DRIVE D: RELAY R-4 LINKED\n");
        screen_.print("RUN MAP TO PULL THE SURVEY CACHE.\n");
        return;
    }
    fs_.unmount(kFsDriveD);
    screen_.print("DRIVE D: DISCONNECTED\n");
    if (cwd_drive_ == kFsDriveD) {
        cwd_drive_ = kFsDriveA;
        cwd_node_ = 0;
    }
}

void Shell::prompt(char* buf, u32 size) const
{
    char path[kFsPathMax];
    fs_.path_string(cwd(), path, sizeof(path));
    std::snprintf(buf, size, "%s>", path);
}

bool Shell::is_command(std::string_view token, std::string_view name)
{
    if (token.size() != name.size()) {
        return false;
    }
    for (u32 i = 0; i < token.size(); i++) {
        if (upper(token[i]) != name[i]) {
            return false;
        }
    }
    return true;
}

Shell::Tokens Shell::tokenize(std::string_view command)
{
    Tokens out;
    u32 i = 0;
    while (i < command.size() && out.count < kShellMaxTokens) {
        while (i < command.size() && command[i] == ' ') {
            i++;
        }
        if (i >= command.size()) {
            break;
        }
        const u32 start = i;
        while (i < command.size() && command[i] != ' ') {
            i++;
        }
        out.at[out.count++].assign(command.substr(start, i - start));
    }
    return out;
}

void Shell::split_path(std::string_view path, FixedString<kTermInputMax + 1>& head,
                       FixedString<kTermInputMax + 1>& tail)
{
    std::size_t cut = std::string_view::npos;
    for (std::size_t i = 0; i < path.size(); i++) {
        if (is_separator(path[i])) {
            cut = i;
        }
    }
    if (cut == std::string_view::npos && path.size() >= 2 && path[1] == ':') {
        cut = 1;
    }
    if (cut == std::string_view::npos) {
        head.clear();
        tail.assign(path);
        return;
    }
    head.assign(path.substr(0, cut + 1));
    tail.assign(path.substr(cut + 1));
}

bool Shell::check_path(std::string_view path, FsRef& out)
{
    const i32 pd = Fs::path_drive(path);
    if (pd == -2) {
        screen_.print("INVALID DRIVE SPECIFICATION\n");
        return false;
    }
    if (pd >= 0 && !fs_.mounted(pd)) {
        screen_.printf("DRIVE %c: NOT READY\n", 'A' + pd);
        return false;
    }
    out = fs_.resolve(cwd(), path);
    return true;
}

bool Shell::resolve_target(std::string_view path, FsRef src, FsRef& out_dir,
                           FixedString<kTermInputMax + 1>& out_name)
{
    FsRef dst;
    if (!check_path(path, dst)) {
        return false;
    }
    if (fs_.valid(dst) && fs_.node(dst)->is_dir) {
        out_dir = dst;
        out_name.assign(fs_.node(src)->name.view());
        return true;
    }

    FixedString<kTermInputMax + 1> head;
    split_path(path, head, out_name);
    if (head.empty()) {
        out_dir = cwd();
        return true;
    }
    if (!check_path(head.view(), out_dir)) {
        return false;
    }
    if (!fs_.valid(out_dir) || !fs_.node(out_dir)->is_dir) {
        screen_.print("PATH NOT FOUND\n");
        return false;
    }
    return true;
}

void Shell::report_transfer(FsError err, const char* verb)
{
    switch (err) {
    case FsError::Ok:
        screen_.printf("        1 FILE(S) %s\n", verb);
        break;
    case FsError::NoSpace:
        screen_.printf("INSUFFICIENT DISK SPACE\n        0 FILE(S) %s\n", verb);
        break;
    case FsError::Self:
        screen_.printf("FILE CANNOT BE %s ONTO ITSELF\n", verb);
        break;
    case FsError::BadName:
        screen_.print("BAD FILE NAME\n");
        break;
    case FsError::IsDir:
    case FsError::NotDir:
        screen_.print("ACCESS DENIED\n");
        break;
    case FsError::Full:
        screen_.print("DIRECTORY FULL\n");
        break;
    default:
        screen_.printf("%s FAILED\n", verb);
        break;
    }
}

void Shell::cmd_dir(std::string_view path)
{
    FsRef dir = cwd();
    if (!path.empty() && !check_path(path, dir)) {
        return;
    }
    if (!fs_.mounted(dir.drive)) {
        screen_.printf("DRIVE %c: NOT READY\n", 'A' + dir.drive);
        return;
    }
    if (!fs_.valid(dir) || !fs_.node(dir)->is_dir) {
        screen_.print("PATH NOT FOUND\n");
        return;
    }

    const FsDrive& d = fs_.drive(dir.drive);
    char pathstr[kFsPathMax];
    fs_.path_string(dir, pathstr, sizeof(pathstr));
    screen_.printf(" VOLUME IN DRIVE %c IS %s\n", 'A' + dir.drive, d.label.c_str());
    screen_.printf(" DIRECTORY OF %s\n\n", pathstr);

    u32 files = 0;
    u32 bytes = 0;
    for (i32 pass = 0; pass < 2; pass++) {
        for (i32 i = 1; i < kFsDriveNodes; i++) {
            const FsNode& n = d.nodes[i];
            if (!n.used || n.parent != dir.node || (pass == 0) != n.is_dir) {
                continue;
            }
            if (n.is_dir) {
                screen_.printf(" %-12s   <DIR>\n", n.name.c_str());
                continue;
            }
            const std::string_view name = n.name.view();
            const std::size_t dot = name.rfind('.');
            char base[9] = {};
            char ext[4] = {};
            if (dot == std::string_view::npos) {
                std::snprintf(base, sizeof(base), "%.*s", static_cast<int>(name.size()),
                              name.data());
            } else {
                std::snprintf(base, sizeof(base), "%.*s", static_cast<int>(dot), name.data());
                std::snprintf(ext, sizeof(ext), "%.*s",
                              static_cast<int>(name.size() - dot - 1), name.data() + dot + 1);
            }
            screen_.printf(" %-8s %-3s %10u\n", base, ext, n.size);
            files++;
            bytes += n.size;
        }
    }
    screen_.printf("%9u FILE(S) %10u BYTES\n", files, bytes);
    screen_.printf("%20u BYTES FREE\n", fs_.free_bytes(dir.drive));
}

void Shell::cmd_cd(std::string_view path)
{
    if (path.empty()) {
        char pathstr[kFsPathMax];
        fs_.path_string(cwd(), pathstr, sizeof(pathstr));
        screen_.printf("%s\n", pathstr);
        return;
    }
    FsRef dir;
    if (!check_path(path, dir)) {
        return;
    }
    if (!fs_.valid(dir) || !fs_.node(dir)->is_dir) {
        screen_.print("INVALID DIRECTORY\n");
        return;
    }
    cwd_drive_ = dir.drive;
    cwd_node_ = dir.node;
}

void Shell::type_binary(std::string_view name)
{
    u32 h = 2166136261u;
    for (const char c : name) {
        h = (h ^ static_cast<u32>(static_cast<u8>(c))) * 16777619u;
    }
    screen_.print("MZ");
    for (i32 r = 0; r < 3; r++) {
        char line[59];
        for (i32 i = 0; i < 58; i++) {
            h = h * 1664525u + 1013904223u;
            line[i] = static_cast<char>(33 + (h >> 20) % 92);
        }
        line[58] = 0;
        screen_.printf("%s\n", line);
    }
}

void Shell::cmd_type(std::string_view path)
{
    FsRef ref;
    if (!check_path(path, ref)) {
        return;
    }
    if (!fs_.valid(ref)) {
        screen_.print("FILE NOT FOUND\n");
        return;
    }
    const FsNode* n = fs_.node(ref);
    if (n->is_dir) {
        screen_.print("ACCESS DENIED\n");
        return;
    }
    const std::string_view text = fs_.text(ref);
    if (text.empty() || n->corrupted) {
        type_binary(n->name.view());
        return;
    }
    screen_.print(text);
    screen_.print("\n");
}

void Shell::cmd_view(std::string_view path)
{
    FsRef ref;
    if (!check_path(path, ref)) {
        return;
    }
    if (!fs_.valid(ref)) {
        screen_.print("FILE NOT FOUND\n");
        return;
    }
    const FsNode* n = fs_.node(ref);
    if (n->is_dir || n->pic <= 0) {
        screen_.print("NOT AN IMAGE FILE\n");
        return;
    }
    if (n->corrupted) {
        screen_.print("IMAGE DATA CORRUPTED\n");
        return;
    }
    if (disks_ && !disks_->photo(n->pic)) {
        screen_.print("IMAGE DATA MISSING\n");
        return;
    }
    request_.view_pic = n->pic;
    request_.view_name = n->name;
}

void Shell::cmd_copy(std::string_view src_path, std::string_view dst_path)
{
    FsRef src;
    if (!check_path(src_path, src)) {
        return;
    }
    if (!fs_.valid(src)) {
        screen_.print("FILE NOT FOUND\n");
        return;
    }
    if (fs_.node(src)->is_dir) {
        screen_.print("CANNOT COPY A DIRECTORY\n");
        return;
    }
    FsRef dst_dir;
    FixedString<kTermInputMax + 1> dst_name;
    if (!resolve_target(dst_path, src, dst_dir, dst_name)) {
        return;
    }
    report_transfer(fs_.copy(src, dst_dir, dst_name.view()), "COPIED");
}

void Shell::cmd_move(std::string_view src_path, std::string_view dst_path)
{
    FsRef src;
    if (!check_path(src_path, src)) {
        return;
    }
    if (!fs_.valid(src)) {
        screen_.print("FILE NOT FOUND\n");
        return;
    }
    if (fs_.node(src)->is_dir) {
        screen_.print("CANNOT MOVE A DIRECTORY\n");
        return;
    }
    FsRef dst_dir;
    FixedString<kTermInputMax + 1> dst_name;
    if (!resolve_target(dst_path, src, dst_dir, dst_name)) {
        return;
    }
    report_transfer(fs_.move(src, dst_dir, dst_name.view()), "MOVED");
}

void Shell::cmd_del(std::string_view path)
{
    FsRef ref;
    if (!check_path(path, ref)) {
        return;
    }
    if (!fs_.valid(ref)) {
        screen_.print("FILE NOT FOUND\n");
        return;
    }
    if (fs_.remove(ref) == FsError::IsDir) {
        screen_.print("CANNOT DELETE A DIRECTORY\n");
    }
}

void Shell::cmd_mkdir(std::string_view path)
{
    FsRef ref;
    if (!check_path(path, ref)) {
        return;
    }
    if (fs_.valid(ref)) {
        screen_.print(fs_.node(ref)->is_dir ? "DIRECTORY ALREADY EXISTS\n"
                                            : "A FILE BY THAT NAME EXISTS\n");
        return;
    }

    FixedString<kTermInputMax + 1> head;
    FixedString<kTermInputMax + 1> name;
    split_path(path, head, name);

    FsRef dir = cwd();
    if (!head.empty()) {
        if (!check_path(head.view(), dir)) {
            return;
        }
        if (!fs_.valid(dir) || !fs_.node(dir)->is_dir) {
            screen_.print("PATH NOT FOUND\n");
            return;
        }
    }
    if (name.size() > kFsNameMax || !fs_name_valid(name.view())) {
        screen_.print("BAD DIRECTORY NAME\n");
        return;
    }
    if (!fs_.valid(fs_.mkdir(dir, name.view()))) {
        screen_.print("CANNOT CREATE DIRECTORY\n");
    }
}

void Shell::cmd_rmdir(std::string_view path)
{
    FsRef ref;
    if (!check_path(path, ref)) {
        return;
    }
    if (!fs_.valid(ref)) {
        screen_.print("PATH NOT FOUND\n");
        return;
    }
    if (!fs_.node(ref)->is_dir) {
        screen_.print("NOT A DIRECTORY\n");
        return;
    }
    if (ref == cwd()) {
        screen_.print("CANNOT REMOVE CURRENT DIRECTORY\n");
        return;
    }

    const FsError err = fs_.rmdir(ref);
    if (err == FsError::NotEmpty) {
        screen_.print("DIRECTORY NOT EMPTY\n");
    } else if (err == FsError::Self) {
        screen_.print("CANNOT REMOVE ROOT DIRECTORY\n");
    } else if (err != FsError::Ok) {
        screen_.print("RMDIR FAILED\n");
    }
}

void Shell::cmd_chkdsk(std::string_view arg)
{
    i32 drive = cwd_drive_;
    if (!arg.empty()) {
        drive = Fs::path_drive(arg);
        if (drive < 0 || arg.size() != 2) {
            screen_.print("INVALID DRIVE SPECIFICATION\n");
            return;
        }
    }
    if (!fs_.mounted(drive)) {
        screen_.printf("DRIVE %c: NOT READY\n", 'A' + drive);
        return;
    }

    const FsDrive& d = fs_.drive(drive);
    u32 files = 0;
    u32 dirs = 0;
    for (i32 i = 1; i < kFsDriveNodes; i++) {
        if (d.nodes[i].used) {
            (d.nodes[i].is_dir ? dirs : files)++;
        }
    }
    screen_.printf("VOLUME %s   DRIVE %c:\n\n", d.label.c_str(), 'A' + drive);
    screen_.printf("%10u BYTES TOTAL DISK SPACE\n", d.capacity);
    screen_.printf("%10u BYTES IN %u FILE(S), %u DIRECTORY(S)\n", fs_.used_bytes(drive), files,
                   dirs);
    screen_.printf("%10u BYTES AVAILABLE ON DISK\n", fs_.free_bytes(drive));
    screen_.print("\n    655360 BYTES TOTAL MEMORY\n    598016 BYTES FREE\n");
}

void Shell::cmd_format(std::string_view arg)
{
    const i32 drive = Fs::path_drive(arg);
    if (drive < 0 || arg.size() != 2) {
        screen_.print("INVALID DRIVE SPECIFICATION\n");
        return;
    }
    if (!fs_.mounted(drive)) {
        screen_.printf("DRIVE %c: NOT READY\n", 'A' + drive);
        return;
    }
    format_drive_ = drive;
    screen_.printf("WARNING: ALL DATA ON %sDRIVE %c: WILL BE LOST!\n",
                   drive == kFsDriveA ? "NON-REMOVABLE " : "", 'A' + drive);
    screen_.print("PROCEED WITH FORMAT (Y/N)?\n");
}

bool Shell::try_exe(std::string_view name)
{
    FixedString<kTermInputMax + 8> full;
    if (name.size() > 4 && name.substr(name.size() - 4) == ".EXE") {
        full.assign(name);
    } else {
        full.format("%.*s.EXE", static_cast<int>(name.size()), name.data());
    }

    FsRef ref = fs_.resolve(cwd(), full.view());
    if (!fs_.valid(ref) || fs_.node(ref)->is_dir) {
        ref = fs_.resolve(Fs::root(kFsDriveA), full.view());
    }
    if (!fs_.valid(ref) || fs_.node(ref)->is_dir) {
        return false;
    }
    request_.launch = fs_.node(ref)->exe;
    request_.launch_ref = ref;
    return true;
}

void Shell::run(std::string_view command)
{
    if (format_drive_ >= 0) {
        const i32 drive = format_drive_;
        format_drive_ = -1;
        if (command.size() == 1 && upper(command[0]) == 'Y') {
            fs_.format(drive, {});
            if (cwd_drive_ == drive) {
                cwd_node_ = 0;
            }
            screen_.printf("FORMAT COMPLETE.\n%u BYTES FREE\n", fs_.free_bytes(drive));
        } else {
            screen_.print("FORMAT ABORTED.\n");
        }
        return;
    }

    if (!fs_.mounted(cwd_drive_)) {
        cwd_drive_ = kFsDriveA;
        cwd_node_ = 0;
    }

    const Tokens tok = tokenize(command);
    if (tok.count == 0) {
        return;
    }
    const std::string_view head = tok.at[0].view();
    const std::string_view arg1 = tok.count > 1 ? tok.at[1].view() : std::string_view{};
    const std::string_view arg2 = tok.count > 2 ? tok.at[2].view() : std::string_view{};

    if (head.size() == 2 && head[1] == ':' && tok.count == 1) {
        const i32 drive = Fs::path_drive(head);
        if (drive == -2) {
            screen_.print("INVALID DRIVE SPECIFICATION\n");
        } else if (!fs_.mounted(drive)) {
            screen_.printf("DRIVE %c: NOT READY\n", 'A' + drive);
        } else {
            cwd_drive_ = drive;
            cwd_node_ = 0;
        }
        return;
    }

    if (is_command(head, "HELP")) {
        screen_.print(kHelpText);
    } else if (is_command(head, "LS")) {
        cmd_dir(arg1);
    } else if (is_command(head, "CD") || is_command(head, "CHDIR")) {
        cmd_cd(arg1);
    } else if (is_command(head, "TYPE")) {
        tok.count < 2 ? screen_.print("SYNTAX: TYPE FILE\n") : cmd_type(arg1);
    } else if (is_command(head, "VIEW")) {
        tok.count < 2 ? screen_.print("SYNTAX: VIEW FILE\n") : cmd_view(arg1);
    } else if (is_command(head, "COPY")) {
        tok.count < 3 ? screen_.print("SYNTAX: COPY SRC DST\n") : cmd_copy(arg1, arg2);
    } else if (is_command(head, "DEL") || is_command(head, "ERASE")) {
        tok.count < 2 ? screen_.print("SYNTAX: DEL FILE\n") : cmd_del(arg1);
    } else if (is_command(head, "MOVE") || is_command(head, "MV")) {
        tok.count < 3 ? screen_.print("SYNTAX: MOVE SRC DST\n") : cmd_move(arg1, arg2);
    } else if (is_command(head, "MKDIR") || is_command(head, "MD")) {
        tok.count < 2 ? screen_.print("SYNTAX: MKDIR PATH\n") : cmd_mkdir(arg1);
    } else if (is_command(head, "RMDIR") || is_command(head, "RD")) {
        tok.count < 2 ? screen_.print("SYNTAX: RMDIR PATH\n") : cmd_rmdir(arg1);
    } else if (is_command(head, "CHKDSK")) {
        cmd_chkdsk(arg1);
    } else if (is_command(head, "FORMAT")) {
        tok.count < 2 ? screen_.print("SYNTAX: FORMAT X:\n") : cmd_format(arg1);
    } else if (is_command(head, "RUN")) {
        if (tok.count < 2) {
            screen_.print("SYNTAX: RUN FILE\n");
        } else if (!try_exe(arg1)) {
            screen_.print("FILE NOT FOUND\n");
        }
    } else if (is_command(head, "CLS")) {
        screen_.reset();
    } else if (is_command(head, "VER")) {
        screen_.print("DR-OS 2.2 (REDLINE SYSTEMS 1988)\n");
    } else if (is_command(head, "OFF")) {
        screen_.print("SYSTEM HALTED.\n");
        request_.power_off = true;
    } else if (!try_exe(head)) {
        screen_.print("Bad command or file name\n");
    }
}

void Shell::submit()
{
    char prompt_buf[kTermPromptMax];
    prompt(prompt_buf, sizeof(prompt_buf));

    char command[kTermInputMax + 1];
    std::snprintf(command, sizeof(command), "%s", input_);
    screen_.printf("%s%s\n", prompt_buf, command);

    if (input_len_ > 0) {
        std::snprintf(history_[history_count_ % kTermHistory], kTermInputMax + 1, "%s", input_);
        history_count_++;
    }
    history_pos_ = -1;
    input_[0] = 0;
    input_len_ = 0;
    input_cursor_ = 0;

    run(command);
}

void Shell::key_char(char c)
{
    if (c < 32 || c > 126 || input_len_ >= kTermInputMax) {
        return;
    }
    c = upper(c);
    std::memmove(&input_[input_cursor_ + 1], &input_[input_cursor_],
                 input_len_ - input_cursor_ + 1);
    input_[input_cursor_] = c;
    input_cursor_++;
    input_len_++;
    history_pos_ = -1;
}

void Shell::key(ShellKey k)
{
    switch (k) {
    case ShellKey::Enter:
        submit();
        break;
    case ShellKey::Backspace:
        if (input_cursor_ > 0) {
            std::memmove(&input_[input_cursor_ - 1], &input_[input_cursor_],
                         input_len_ - input_cursor_ + 1);
            input_cursor_--;
            input_len_--;
        }
        break;
    case ShellKey::Delete:
        if (input_cursor_ < input_len_) {
            std::memmove(&input_[input_cursor_], &input_[input_cursor_ + 1],
                         input_len_ - input_cursor_);
            input_len_--;
        }
        break;
    case ShellKey::Left:
        if (input_cursor_ > 0) {
            input_cursor_--;
        }
        break;
    case ShellKey::Right:
        if (input_cursor_ < input_len_) {
            input_cursor_++;
        }
        break;
    case ShellKey::Home:
        input_cursor_ = 0;
        break;
    case ShellKey::End:
        input_cursor_ = input_len_;
        break;
    case ShellKey::Up: {
        if (history_count_ == 0) {
            break;
        }
        const i32 depth = static_cast<i32>(history_count_ < kTermHistory ? history_count_
                                                                         : kTermHistory);
        if (history_pos_ < depth - 1) {
            history_pos_++;
        }
        const u32 slot = (history_count_ - 1 - static_cast<u32>(history_pos_)) % kTermHistory;
        std::snprintf(input_, sizeof(input_), "%s", history_[slot]);
        input_len_ = static_cast<u32>(std::strlen(input_));
        input_cursor_ = input_len_;
        break;
    }
    case ShellKey::Down:
        history_pos_ = -1;
        input_[0] = 0;
        input_len_ = 0;
        input_cursor_ = 0;
        break;
    }
}

} // namespace anom
