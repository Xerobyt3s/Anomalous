#include "terminal/disk_image.h"
#include "core/log.h"

namespace anom {
namespace {

#include "terminal/content/disk_arcade.inc"
#include "terminal/content/disk_fieldnotes.inc"
#include "terminal/content/disk_internal.inc"
#include "terminal/content/disk_master.inc"
#include "terminal/content/disk_rescue.inc"
#include "terminal/content/disk_scratch.inc"

constexpr DiskImage kImages[] = {
    kInternalImage, kMasterImage, kFieldNotesImage,
    kScratchImage,  kRescueImage, kArcadeImage,
};

void mount_files(Fs& fs, FsRef dir, std::span<const RomFile> files)
{
    for (const RomFile& file : files) {
        const FsRef ref = fs.mkfile_rom(dir, file.name, file.text, file.size, file.exe);
        if (!fs.valid(ref)) {
            log_warn("disks: could not add %.*s", static_cast<int>(file.name.size()),
                     file.name.data());
            continue;
        }
        FsNode* node = fs.node_mut(ref);
        node->infected = file.infected;
        node->run_text = file.run_text;
    }
}

} // namespace

std::span<const DiskImage> disk_images()
{
    return kImages;
}

const DiskImage* disk_image(std::string_view id)
{
    for (const DiskImage& image : kImages) {
        if (image.id == id) {
            return &image;
        }
    }
    return nullptr;
}

void fs_mount_image(Fs& fs, i32 drive, const DiskImage& image)
{
    fs.mount(drive, image.label, image.capacity);

    const FsRef root = Fs::root(drive);
    mount_files(fs, root, image.files);

    for (const RomDir& dir : image.dirs) {
        const FsRef sub = fs.mkdir(root, dir.name);
        if (!fs.valid(sub)) {
            log_warn("disks: could not add dir %.*s", static_cast<int>(dir.name.size()),
                     dir.name.data());
            continue;
        }
        mount_files(fs, sub, dir.files);
    }
}

} // namespace anom
