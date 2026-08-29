#include "test.h"

#include "core/arena.h"
#include "terminal/disks.h"
#include "terminal/shell.h"

#include <cstring>

using namespace anom;

namespace {

struct Bench {
    Arena arena{megabytes(16)};
    DiskStore disks;
    Shell shell;

    Bench()
    {
        disks.init(arena);
        shell.init(&disks);
    }

    void run(std::string_view command)
    {
        shell.run(command);
        shell.screen().flush_pending();
    }

    void type(std::string_view text)
    {
        for (const char c : text) {
            shell.key_char(c);
        }
    }

    void enter()
    {
        shell.key(ShellKey::Enter);
        shell.screen().flush_pending();
    }

    bool said(std::string_view needle) const
    {
        for (u32 i = 0; i < shell.screen().line_count(); i++) {
            if (shell.screen().line(i).find(needle) != std::string_view::npos) {
                return true;
            }
        }
        return shell.screen().out_line().find(needle) != std::string_view::npos;
    }

    std::string_view last_line() const
    {
        const u32 n = shell.screen().line_count();
        return n ? shell.screen().line(n - 1) : std::string_view{};
    }
};

} // namespace

TEST(shell, it_boots_on_the_system_drive)
{
    Bench b;
    CHECK(b.shell.cwd().drive == kFsDriveA);
    CHECK(b.shell.cwd().node == 0);
    CHECK(b.shell.fs().mounted(kFsDriveA));

    char prompt[kTermPromptMax];
    b.shell.prompt(prompt, sizeof(prompt));
    CHECK(std::string_view(prompt) == "A:\\>");
}

TEST(shell, ls_lists_the_baked_programs)
{
    Bench b;
    b.run("LS");

    CHECK(b.said("VOLUME IN DRIVE A IS DR-OS SYSTEM"));
    CHECK(b.said("STATUS"));
    CHECK(b.said("EXE"));
    CHECK(b.said("BYTES FREE"));
}

TEST(shell, an_unknown_command_is_rejected)
{
    Bench b;
    b.run("FLURB");
    CHECK(b.said("Bad command or file name"));
}

TEST(shell, ver_and_help_answer)
{
    Bench b;
    b.run("VER");
    CHECK(b.said("DR-OS 2.2"));

    b.run("HELP");
    CHECK(b.said("SHELL COMMANDS"));
    CHECK(b.said("CHKDSK"));
}

TEST(shell, off_asks_the_host_to_power_down)
{
    Bench b;
    b.run("OFF");
    CHECK(b.said("SYSTEM HALTED"));

    const ShellRequest req = b.shell.take_request();
    CHECK(req.power_off);
    CHECK(!b.shell.take_request().power_off);
}

TEST(shell, typing_a_program_name_launches_it)
{
    Bench b;
    b.run("STATUS");
    CHECK(b.shell.take_request().launch == FsExe::Status);

    b.run("RUN MAP.EXE");
    CHECK(b.shell.take_request().launch == FsExe::Map);

    b.run("RUN NOPE");
    CHECK(b.said("FILE NOT FOUND"));
}

TEST(shell, type_prints_a_text_file)
{
    Bench b;
    b.run("TYPE README.TXT");
    CHECK(b.said("DR-OS"));

    b.run("TYPE MISSING.TXT");
    CHECK(b.said("FILE NOT FOUND"));
}

TEST(shell, type_on_a_program_prints_binary_noise)
{
    Bench b;
    b.run("TYPE STATUS.EXE");
    CHECK(b.said("MZ"));
}

TEST(shell, directories_can_be_made_entered_and_removed)
{
    Bench b;
    b.run("MKDIR WORK");
    b.run("CD WORK");

    char prompt[kTermPromptMax];
    b.shell.prompt(prompt, sizeof(prompt));
    CHECK(std::string_view(prompt) == "A:\\WORK>");

    b.run("RMDIR A:\\WORK");
    CHECK(b.said("CANNOT REMOVE CURRENT DIRECTORY"));

    b.run("CD ..");
    b.run("RMDIR WORK");
    b.run("CD WORK");
    CHECK(b.said("INVALID DIRECTORY"));
}

TEST(shell, mkdir_rejects_a_bad_name)
{
    Bench b;
    b.run("MKDIR TOOLONGNAME");
    CHECK(b.said("BAD DIRECTORY NAME"));
}

TEST(shell, rmdir_refuses_a_directory_with_contents)
{
    Bench b;
    b.run("MKDIR WORK");
    b.run("COPY README.TXT WORK");
    b.run("RMDIR WORK");
    CHECK(b.said("DIRECTORY NOT EMPTY"));
}

TEST(shell, copy_and_del_move_real_bytes)
{
    Bench b;
    b.run("COPY README.TXT NOTES.TXT");
    CHECK(b.said("1 FILE(S) COPIED"));

    const FsRef copied = b.shell.fs().resolve(b.shell.cwd(), "NOTES.TXT");
    CHECK(b.shell.fs().valid(copied));
    CHECK(b.shell.fs().text(copied).find("DR-OS") != std::string_view::npos);

    b.run("DEL NOTES.TXT");
    CHECK(!b.shell.fs().valid(b.shell.fs().resolve(b.shell.cwd(), "NOTES.TXT")));
}

TEST(shell, copying_a_file_onto_itself_is_refused)
{
    Bench b;
    b.run("COPY README.TXT README.TXT");
    CHECK(b.said("CANNOT BE COPIED ONTO ITSELF"));
}

TEST(shell, move_renames_within_a_drive)
{
    Bench b;
    b.run("COPY README.TXT A.TXT");
    b.run("MOVE A.TXT B.TXT");
    CHECK(b.said("1 FILE(S) MOVED"));
    CHECK(!b.shell.fs().valid(b.shell.fs().resolve(b.shell.cwd(), "A.TXT")));
    CHECK(b.shell.fs().valid(b.shell.fs().resolve(b.shell.cwd(), "B.TXT")));
}

TEST(shell, del_will_not_remove_a_directory)
{
    Bench b;
    b.run("MKDIR WORK");
    b.run("DEL WORK");
    CHECK(b.said("CANNOT DELETE A DIRECTORY"));
}

TEST(shell, chkdsk_reports_the_drive)
{
    Bench b;
    b.run("CHKDSK");
    CHECK(b.said("VOLUME DR-OS SYSTEM"));
    CHECK(b.said("BYTES TOTAL DISK SPACE"));

    b.run("CHKDSK Z:");
    CHECK(b.said("INVALID DRIVE SPECIFICATION"));

    b.run("CHKDSK B:");
    CHECK(b.said("DRIVE B: NOT READY"));
}

TEST(shell, format_confirms_before_erasing)
{
    Bench b;
    b.run("MKDIR WORK");
    b.run("FORMAT A:");
    CHECK(b.said("PROCEED WITH FORMAT"));
    CHECK(b.shell.awaiting_format());
    CHECK(b.shell.fs().valid(b.shell.fs().resolve(b.shell.cwd(), "WORK")));

    b.run("N");
    CHECK(b.said("FORMAT ABORTED"));
    CHECK(!b.shell.awaiting_format());
    CHECK(b.shell.fs().valid(b.shell.fs().resolve(b.shell.cwd(), "WORK")));

    b.run("FORMAT A:");
    b.run("Y");
    CHECK(b.said("FORMAT COMPLETE"));
    CHECK(!b.shell.fs().valid(b.shell.fs().resolve(b.shell.cwd(), "WORK")));
    CHECK(!b.shell.fs().valid(b.shell.fs().resolve(b.shell.cwd(), "README.TXT")));
}

TEST(shell, inserting_a_disk_mounts_drive_b)
{
    Bench b;
    b.run("B:");
    CHECK(b.said("DRIVE B: NOT READY"));

    b.shell.set_disk(DISK_MASTER);
    b.shell.screen().flush_pending();
    CHECK(b.said("MEDIA INSERTED (DR-OS MASTER)"));

    b.run("B:");
    CHECK(b.shell.cwd().drive == kFsDriveB);
    b.run("LS");
    CHECK(b.said("VOLUME IN DRIVE B IS DR-OS MASTER"));
}

TEST(shell, ejecting_a_disk_returns_you_to_drive_a)
{
    Bench b;
    b.shell.set_disk(DISK_MASTER);
    b.run("B:");
    CHECK(b.shell.cwd().drive == kFsDriveB);

    b.shell.set_disk(-1);
    b.shell.screen().flush_pending();
    CHECK(b.said("MEDIA REMOVED"));
    CHECK(b.shell.cwd().drive == kFsDriveA);
}

TEST(shell, writes_to_a_floppy_survive_an_eject_and_reinsert)
{
    Bench b;
    b.shell.set_disk(DISK_SCRATCH);
    b.run("COPY A:\\README.TXT B:\\SAVED.TXT");
    CHECK(b.said("1 FILE(S) COPIED"));

    b.shell.set_disk(-1);
    b.shell.set_disk(DISK_SCRATCH);
    b.run("B:");
    b.run("LS");
    CHECK(b.said("SAVED"));
}

TEST(shell, the_tower_link_mounts_drive_d)
{
    Bench b;
    b.shell.set_tower_linked(true);
    b.shell.screen().flush_pending();
    CHECK(b.said("RELAY R-4 LINKED"));

    b.run("D:");
    b.run("LS");
    CHECK(b.said("GATE"));

    b.shell.set_tower_linked(false);
    b.shell.screen().flush_pending();
    CHECK(b.shell.cwd().drive == kFsDriveA);
}

TEST(shell, input_editing_moves_the_cursor)
{
    Bench b;
    b.type("HELO");
    CHECK(b.shell.input() == "HELO");
    CHECK(b.shell.cursor() == 4);

    b.shell.key(ShellKey::Left);
    b.type("L");
    CHECK(b.shell.input() == "HELLO");

    b.shell.key(ShellKey::Home);
    CHECK(b.shell.cursor() == 0);
    b.shell.key(ShellKey::Delete);
    CHECK(b.shell.input() == "ELLO");

    b.shell.key(ShellKey::End);
    b.shell.key(ShellKey::Backspace);
    CHECK(b.shell.input() == "ELL");
}

TEST(shell, typed_input_is_upper_cased_and_bounded)
{
    Bench b;
    b.type("ls");
    CHECK(b.shell.input() == "LS");

    for (u32 i = 0; i < kTermInputMax * 2; i++) {
        b.shell.key_char('X');
    }
    CHECK(b.shell.input().size() == kTermInputMax);
}

TEST(shell, enter_echoes_the_prompt_and_runs_the_line)
{
    Bench b;
    b.type("VER");
    b.enter();

    CHECK(b.said("A:\\>VER"));
    CHECK(b.said("REDLINE SYSTEMS 1988"));
    CHECK(b.shell.input().empty());
}

TEST(shell, history_recalls_previous_commands)
{
    Bench b;
    b.type("VER");
    b.enter();
    b.type("CHKDSK");
    b.enter();

    b.shell.key(ShellKey::Up);
    CHECK(b.shell.input() == "CHKDSK");
    b.shell.key(ShellKey::Up);
    CHECK(b.shell.input() == "VER");
    b.shell.key(ShellKey::Up);
    CHECK(b.shell.input() == "VER");

    b.shell.key(ShellKey::Down);
    CHECK(b.shell.input().empty());
}

TEST(shell, an_empty_line_is_not_recorded_in_history)
{
    Bench b;
    b.type("VER");
    b.enter();
    b.enter();

    b.shell.key(ShellKey::Up);
    CHECK(b.shell.input() == "VER");
}

TEST(shell, cls_clears_the_scrollback)
{
    Bench b;
    b.run("VER");
    CHECK(b.shell.screen().line_count() > 0);

    b.run("CLS");
    CHECK(b.shell.screen().line_count() == 0);
}

TEST(shell, mission_raises_a_request)
{
    Bench b;
    b.run("MISSION");
    CHECK(b.shell.take_request().mission);
}

TEST(shell, view_needs_an_image_file)
{
    Bench b;
    b.run("VIEW README.TXT");
    CHECK(b.said("NOT AN IMAGE FILE"));
    CHECK(b.shell.take_request().view_pic < 0);
}

TEST(shell, a_syntax_error_names_the_form)
{
    Bench b;
    b.run("COPY");
    CHECK(b.said("SYNTAX: COPY SRC DST"));
    b.run("TYPE");
    CHECK(b.said("SYNTAX: TYPE FILE"));
    b.run("FORMAT");
    CHECK(b.said("SYNTAX: FORMAT X:"));
}

TEST(shell, commands_are_case_insensitive)
{
    Bench b;
    b.run("ver");
    CHECK(b.said("REDLINE SYSTEMS 1988"));
    b.run("Ver");
    CHECK(b.said("REDLINE SYSTEMS 1988"));
}
