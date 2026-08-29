#include "test.h"

#include "core/arena.h"
#include "terminal/disk_image.h"
#include "terminal/disks.h"
#include "terminal/fs.h"

using namespace anom;

namespace {

Fs mounted_fs()
{
    Fs fs;
    fs.init();
    return fs;
}

u32 child_count(const Fs& fs, FsRef dir)
{
    const FsDrive& d = fs.drive(dir.drive);
    u32 n = 0;
    for (i32 i = 1; i < kFsDriveNodes; i++) {
        if (d.nodes[i].used && d.nodes[i].parent == dir.node) {
            n++;
        }
    }
    return n;
}

} // namespace

TEST(fs, a_fresh_filesystem_has_one_mounted_drive)
{
    Fs fs = mounted_fs();
    CHECK(fs.mounted(kFsDriveA));
    CHECK(!fs.mounted(kFsDriveB));
    CHECK(fs.valid(Fs::root(kFsDriveA)));
    CHECK(fs.node(Fs::root(kFsDriveA))->is_dir);
    CHECK(fs.used_bytes(kFsDriveA) == 0);
    CHECK(fs.free_bytes(kFsDriveA) == kFsSystemCapacity);
}

TEST(fs, names_follow_eight_dot_three)
{
    CHECK(fs_name_valid("README.TXT"));
    CHECK(fs_name_valid("SURVEY"));
    CHECK(fs_name_valid("IMG_01.PIC"));
    CHECK(fs_name_valid("A"));

    CHECK(!fs_name_valid(""));
    CHECK(!fs_name_valid("readme.txt"));
    CHECK(!fs_name_valid("TOOLONGNAME.TXT"));
    CHECK(!fs_name_valid("NAME.TOOLONG"));
    CHECK(!fs_name_valid(".HIDDEN"));
    CHECK(!fs_name_valid("TWO.DOT.S"));
    CHECK(!fs_name_valid("SPACE BAR"));
}

TEST(fs, files_and_directories_nest)
{
    Fs fs = mounted_fs();
    const FsRef root = Fs::root(kFsDriveA);

    const FsRef dir = fs.mkdir(root, "SURVEY");
    CHECK(fs.valid(dir));
    CHECK(fs.node(dir)->is_dir);

    const FsRef file = fs.mkfile_data(dir, "NOTES.TXT", "hello");
    CHECK(fs.valid(file));
    CHECK(fs.text(file) == "hello");
    CHECK(fs.node(file)->size == 5);

    CHECK(fs.resolve(root, "SURVEY\\NOTES.TXT") == file);
    CHECK(fs.resolve(root, "A:\\SURVEY\\NOTES.TXT") == file);
    CHECK(fs.resolve(file, "..") == dir);
    CHECK(fs.resolve(dir, ".") == dir);
    CHECK(!fs.valid(fs.resolve(root, "SURVEY\\MISSING.TXT")));
}

TEST(fs, mkdir_twice_returns_the_same_directory)
{
    Fs fs = mounted_fs();
    const FsRef root = Fs::root(kFsDriveA);

    const FsRef a = fs.mkdir(root, "SURVEY");
    const FsRef b = fs.mkdir(root, "SURVEY");
    CHECK(a == b);
    CHECK(child_count(fs, root) == 1);
}

TEST(fs, a_path_string_round_trips)
{
    Fs fs = mounted_fs();
    const FsRef root = Fs::root(kFsDriveA);
    const FsRef dir = fs.mkdir(root, "SURVEY");
    const FsRef sub = fs.mkdir(dir, "OLD");
    const FsRef file = fs.mkfile_data(sub, "A.TXT", "x");

    char buf[kFsPathMax];
    fs.path_string(file, buf, sizeof(buf));
    CHECK(std::string_view(buf) == "A:\\SURVEY\\OLD\\A.TXT");
    CHECK(fs.resolve(root, buf) == file);

    fs.path_string(root, buf, sizeof(buf));
    CHECK(std::string_view(buf) == "A:\\");
}

TEST(fs, deleting_a_file_reclaims_its_pool_bytes)
{
    Fs fs = mounted_fs();
    const FsRef root = Fs::root(kFsDriveA);

    const FsRef a = fs.mkfile_data(root, "A.TXT", "aaaa");
    const FsRef b = fs.mkfile_data(root, "B.TXT", "bbbbbbbb");
    CHECK(fs.valid(a));
    CHECK(fs.valid(b));
    const u32 pool_after_two = fs.drive(kFsDriveA).pool_used;

    CHECK(fs.remove(a) == FsError::Ok);
    CHECK(fs.drive(kFsDriveA).pool_used == pool_after_two - 5);
    CHECK(fs.text(b) == "bbbbbbbb");
}

TEST(fs, a_directory_needs_rmdir_and_must_be_empty)
{
    Fs fs = mounted_fs();
    const FsRef root = Fs::root(kFsDriveA);
    const FsRef dir = fs.mkdir(root, "SURVEY");
    const FsRef file = fs.mkfile_data(dir, "A.TXT", "x");

    CHECK(fs.remove(dir) == FsError::IsDir);
    CHECK(fs.rmdir(dir) == FsError::NotEmpty);
    CHECK(fs.rmdir(file) == FsError::NotDir);
    CHECK(fs.rmdir(root) == FsError::Self);

    CHECK(fs.remove(file) == FsError::Ok);
    CHECK(fs.rmdir(dir) == FsError::Ok);
    CHECK(child_count(fs, root) == 0);
}

TEST(fs, copying_duplicates_content_between_drives)
{
    Fs fs = mounted_fs();
    fs.mount(kFsDriveB, "SCRATCH", 4096);

    const FsRef src = fs.mkfile_data(Fs::root(kFsDriveA), "A.TXT", "payload");
    CHECK(fs.copy(src, Fs::root(kFsDriveB), "B.TXT") == FsError::Ok);

    const FsRef dst = fs.resolve(Fs::root(kFsDriveB), "B.TXT");
    CHECK(fs.valid(dst));
    CHECK(fs.text(dst) == "payload");
    CHECK(fs.text(src) == "payload");
}

TEST(fs, moving_across_drives_removes_the_source)
{
    Fs fs = mounted_fs();
    fs.mount(kFsDriveB, "SCRATCH", 4096);

    const FsRef src = fs.mkfile_data(Fs::root(kFsDriveA), "A.TXT", "payload");
    CHECK(fs.move(src, Fs::root(kFsDriveB), "A.TXT") == FsError::Ok);

    CHECK(!fs.valid(fs.resolve(Fs::root(kFsDriveA), "A.TXT")));
    CHECK(fs.text(fs.resolve(Fs::root(kFsDriveB), "A.TXT")) == "payload");
}

TEST(fs, renaming_in_place_keeps_the_node)
{
    Fs fs = mounted_fs();
    const FsRef root = Fs::root(kFsDriveA);
    const FsRef src = fs.mkfile_data(root, "A.TXT", "payload");

    CHECK(fs.move(src, root, "B.TXT") == FsError::Ok);
    CHECK(fs.resolve(root, "B.TXT") == src);
    CHECK(!fs.valid(fs.resolve(root, "A.TXT")));
}

TEST(fs, a_move_onto_itself_is_refused)
{
    Fs fs = mounted_fs();
    const FsRef root = Fs::root(kFsDriveA);
    const FsRef src = fs.mkfile_data(root, "A.TXT", "payload");

    CHECK(fs.move(src, root, "A.TXT") == FsError::Self);
    CHECK(fs.copy(src, root, "A.TXT") == FsError::Self);
    CHECK(fs.text(src) == "payload");
}

TEST(fs, a_file_larger_than_the_drive_is_refused)
{
    Fs fs;
    fs.init();
    fs.mount(kFsDriveB, "TINY", 8);

    const FsRef root = Fs::root(kFsDriveB);
    CHECK(!fs.valid(fs.mkfile_data(root, "BIG.TXT", "0123456789")));
    CHECK(fs.valid(fs.mkfile_data(root, "OK.TXT", "01234567")));
    CHECK(fs.free_bytes(kFsDriveB) == 0);
}

TEST(fs, formatting_empties_a_drive_but_keeps_it_mounted)
{
    Fs fs = mounted_fs();
    const FsRef root = Fs::root(kFsDriveA);
    fs.mkfile_data(root, "A.TXT", "payload");
    fs.mkdir(root, "SURVEY");
    CHECK(child_count(fs, root) == 2);

    fs.format(kFsDriveA, "WIPED");
    CHECK(fs.mounted(kFsDriveA));
    CHECK(fs.drive(kFsDriveA).label == "WIPED");
    CHECK(child_count(fs, root) == 0);
    CHECK(fs.drive(kFsDriveA).pool_used == 0);
}

TEST(fs, unmounting_invalidates_every_reference)
{
    Fs fs = mounted_fs();
    const FsRef file = fs.mkfile_data(Fs::root(kFsDriveA), "A.TXT", "x");
    CHECK(fs.valid(file));

    fs.unmount(kFsDriveA);
    CHECK(!fs.valid(file));
    CHECK(!fs.valid(fs.resolve(Fs::root(kFsDriveA), "A.TXT")));
}

TEST(fs, a_drive_runs_out_of_nodes_gracefully)
{
    Fs fs;
    fs.init();
    fs.mount(kFsDriveB, "MANY", 1u << 20);

    const FsRef root = Fs::root(kFsDriveB);
    u32 made = 0;
    for (u32 i = 0; i < kFsDriveNodes + 8; i++) {
        char name[kFsNameMax + 1];
        std::snprintf(name, sizeof(name), "F%05u", i);
        if (fs.valid(fs.mkfile_data(root, name, "x"))) {
            made++;
        }
    }
    CHECK(made == kFsDriveNodes - 1);
}

TEST(disks, every_baked_image_mounts)
{
    Fs fs;
    for (const DiskImage& image : disk_images()) {
        fs_mount_image(fs, kFsDriveB, image);
        CHECK(fs.mounted(kFsDriveB));
        CHECK(fs.drive(kFsDriveB).label == image.label);
        CHECK(fs.drive(kFsDriveB).capacity == image.capacity);
    }
}

TEST(disks, the_master_disk_carries_its_programs)
{
    const DiskImage* image = disk_image("master");
    CHECK(image != nullptr);

    Fs fs;
    fs_mount_image(fs, kFsDriveB, *image);
    const FsRef root = Fs::root(kFsDriveB);

    const FsRef status = fs.resolve(root, "STATUS.EXE");
    CHECK(fs.valid(status));
    CHECK(fs.node(status)->exe == FsExe::Status);
    CHECK(fs.node(status)->size > 0);

    const FsRef readme = fs.resolve(root, "README.TXT");
    CHECK(fs.valid(readme));
    CHECK(fs.text(readme).find("DR-OS") != std::string_view::npos);
    CHECK(fs.node(readme)->exe == FsExe::None);
}

TEST(disks, the_field_notes_disk_has_a_subdirectory)
{
    const DiskImage* image = disk_image("fieldnotes");
    CHECK(image != nullptr);
    CHECK(image->dirs.size() == 1);

    Fs fs;
    fs_mount_image(fs, kFsDriveB, *image);
    const FsRef dir = fs.resolve(Fs::root(kFsDriveB), "SURVEY");
    CHECK(fs.valid(dir));
    CHECK(fs.node(dir)->is_dir);
}

TEST(disks, baked_text_is_read_only_but_the_filesystem_is_not)
{
    const DiskImage* image = disk_image("master");
    Fs fs;
    fs_mount_image(fs, kFsDriveB, *image);
    const FsRef root = Fs::root(kFsDriveB);

    const FsRef readme = fs.resolve(root, "README.TXT");
    const std::string_view before = fs.text(readme);

    CHECK(fs.valid(fs.mkdir(root, "NEW")));
    const FsRef made = fs.mkfile_data(root, "MINE.TXT", "written at runtime");
    CHECK(fs.valid(made));
    CHECK(fs.text(made) == "written at runtime");
    CHECK(fs.remove(readme) == FsError::Ok);
    CHECK(!fs.valid(fs.resolve(root, "README.TXT")));

    Fs second;
    fs_mount_image(second, kFsDriveB, *image);
    CHECK(second.text(second.resolve(Fs::root(kFsDriveB), "README.TXT")) == before);
}

TEST(disks, the_store_hands_out_independent_copies)
{
    Arena arena(megabytes(16));
    DiskStore store;
    CHECK(store.init(arena));

    FsDrive first;
    FsDrive second;
    store.load(DISK_MASTER, first);
    store.load(DISK_MASTER, second);

    CHECK(first.mounted);
    CHECK(first.label == disk_label(DISK_MASTER));

    Fs fs;
    fs.drive(kFsDriveB) = first;
    CHECK(fs.remove(fs.resolve(Fs::root(kFsDriveB), "README.TXT")) == FsError::Ok);

    Fs other;
    other.drive(kFsDriveB) = second;
    CHECK(other.valid(other.resolve(Fs::root(kFsDriveB), "README.TXT")));
}

TEST(disks, writes_persist_when_a_disk_is_stored_back)
{
    Arena arena(megabytes(16));
    DiskStore store;
    CHECK(store.init(arena));

    Fs fs;
    FsDrive drive;
    store.load(DISK_SCRATCH, drive);
    fs.drive(kFsDriveB) = drive;

    CHECK(fs.valid(fs.mkfile_data(Fs::root(kFsDriveB), "LOG.TXT", "saved")));
    store.store(DISK_SCRATCH, fs.drive(kFsDriveB));

    Fs reloaded;
    FsDrive back;
    store.load(DISK_SCRATCH, back);
    reloaded.drive(kFsDriveB) = back;
    CHECK(reloaded.text(reloaded.resolve(Fs::root(kFsDriveB), "LOG.TXT")) == "saved");
}

TEST(disks, the_tower_drive_carries_its_gate_program)
{
    Arena arena(megabytes(16));
    DiskStore store;
    CHECK(store.init(arena));

    Fs fs;
    FsDrive drive;
    store.load_tower(drive);
    fs.drive(kFsDriveD) = drive;

    const FsRef root = Fs::root(kFsDriveD);
    CHECK(fs.node(fs.resolve(root, "GATE.EXE"))->exe == FsExe::Gate);
    CHECK(fs.text(fs.resolve(root, "NODELOG.TXT")).find("RELAY TOWER") == 0);
}

TEST(disks, the_camera_reports_its_remaining_exposures)
{
    Arena arena(megabytes(16));
    DiskStore store;
    CHECK(store.init(arena));

    CHECK(store.camera_exposures_left(nullptr) == kCameraCapacity / kPhotoFileBytes);
    CHECK(store.photo(1) == nullptr);
}

TEST(disks, labels_come_from_the_baked_images)
{
    CHECK(disk_label(DISK_MASTER) == "DR-OS MASTER");
    CHECK(disk_label(DISK_SCRATCH) == "SCRATCH DISK");
    CHECK(disk_label(-1) == "UNKNOWN MEDIA");
    CHECK(disk_label(DISK_COUNT) == "UNKNOWN MEDIA");
}

TEST(fs, exe_names_round_trip)
{
    CHECK(fs_exe_from_name("status") == FsExe::Status);
    CHECK(fs_exe_from_name("breach") == FsExe::Breach);
    CHECK(fs_exe_from_name("nope") == FsExe::None);
    CHECK(fs_exe_name(FsExe::Map) == "map");
    CHECK(fs_exe_name(FsExe::None) == "none");
}
