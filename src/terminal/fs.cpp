#include "terminal/fs.h"

#include <cstdio>
#include <cstring>

namespace anom {
namespace {

struct ExeName {
    FsExe exe;
    std::string_view name;
};

constexpr ExeName kExeNames[] = {
    {FsExe::Status, "status"}, {FsExe::Map, "map"},       {FsExe::Link, "link"},
    {FsExe::Comms, "comms"},   {FsExe::Av, "av"},         {FsExe::Toy, "toy"},
    {FsExe::Video, "video"},   {FsExe::Breach, "breach"}, {FsExe::Gate, "gate"},
    {FsExe::Tapes, "tapes"},   {FsExe::Dev, "dev"},      {FsExe::Travel, "travel"},
};

bool is_separator(char c)
{
    return c == '\\' || c == '/';
}

} // namespace

FsExe fs_exe_from_name(std::string_view name)
{
    for (const ExeName& entry : kExeNames) {
        if (entry.name == name) {
            return entry.exe;
        }
    }
    return FsExe::None;
}

std::string_view fs_exe_name(FsExe exe)
{
    for (const ExeName& entry : kExeNames) {
        if (entry.exe == exe) {
            return entry.name;
        }
    }
    return "none";
}

std::string_view fs_error_text(FsError err)
{
    switch (err) {
    case FsError::Ok:
        return "ok";
    case FsError::NotFound:
        return "file not found";
    case FsError::NotReady:
        return "drive not ready";
    case FsError::IsDir:
        return "is a directory";
    case FsError::NotDir:
        return "not a directory";
    case FsError::NotEmpty:
        return "directory not empty";
    case FsError::BadName:
        return "bad file name";
    case FsError::Full:
        return "directory full";
    case FsError::NoSpace:
        return "insufficient disk space";
    default:
        return "cannot do that to itself";
    }
}

bool fs_name_valid(std::string_view name)
{
    if (name.empty()) {
        return false;
    }
    u32 base = 0;
    u32 ext = 0;
    bool dot = false;

    for (u32 i = 0; i < name.size(); i++) {
        const char c = name[i];
        if (c == '.') {
            if (dot || i == 0) {
                return false;
            }
            dot = true;
            continue;
        }
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
        if (dot) {
            ext++;
        } else {
            base++;
        }
    }
    if (base < 1 || base > 8) {
        return false;
    }
    return !dot || (ext >= 1 && ext <= 3);
}

i32 Fs::find_child(const FsDrive& d, i32 dir, std::string_view name)
{
    for (i32 i = 1; i < kFsDriveNodes; i++) {
        if (d.nodes[i].used && d.nodes[i].parent == dir && d.nodes[i].name.view() == name) {
            return i;
        }
    }
    return -1;
}

i32 Fs::alloc_node(FsDrive& d)
{
    for (i32 i = 1; i < kFsDriveNodes; i++) {
        if (!d.nodes[i].used) {
            d.nodes[i] = FsNode{};
            return i;
        }
    }
    return -1;
}

void Fs::reset_drive(FsDrive& d)
{
    for (FsNode& n : d.nodes) {
        n = FsNode{};
    }
    d.pool_used = 0;

    FsNode& root_node = d.nodes[0];
    root_node.used = true;
    root_node.is_dir = true;
    root_node.parent = -1;
    root_node.name.clear();
}

void Fs::init()
{
    for (i32 i = 0; i < kFsDriveCount; i++) {
        drives_[i] = FsDrive{};
    }
    mount(kFsDriveA, "DR-OS SYSTEM", kFsSystemCapacity);
}

void Fs::mount(i32 drive, std::string_view label, u32 capacity)
{
    FsDrive& d = drives_[drive];
    d = FsDrive{};
    d.mounted = true;
    d.capacity = capacity;
    d.label.assign(label);
    reset_drive(d);
}

void Fs::unmount(i32 drive)
{
    drives_[drive].mounted = false;
}

void Fs::format(i32 drive, std::string_view label)
{
    FsDrive& d = drives_[drive];
    if (!d.mounted) {
        return;
    }
    if (!label.empty()) {
        d.label.assign(label);
    }
    reset_drive(d);
}

bool Fs::mounted(i32 drive) const
{
    return drive >= 0 && drive < kFsDriveCount && drives_[drive].mounted;
}

u32 Fs::used_bytes(i32 drive) const
{
    const FsDrive& d = drives_[drive];
    u32 total = 0;
    for (i32 i = 1; i < kFsDriveNodes; i++) {
        if (d.nodes[i].used) {
            total += d.nodes[i].size;
        }
    }
    return total;
}

u32 Fs::free_bytes(i32 drive) const
{
    const FsDrive& d = drives_[drive];
    const u32 used = used_bytes(drive);
    return used >= d.capacity ? 0 : d.capacity - used;
}

bool Fs::valid(FsRef ref) const
{
    return ref.drive >= 0 && ref.drive < kFsDriveCount && drives_[ref.drive].mounted
        && ref.node >= 0 && ref.node < kFsDriveNodes && drives_[ref.drive].nodes[ref.node].used;
}

const FsNode* Fs::node(FsRef ref) const
{
    return &drives_[ref.drive].nodes[ref.node];
}

FsNode* Fs::node_mut(FsRef ref)
{
    return &drives_[ref.drive].nodes[ref.node];
}

i32 Fs::path_drive(std::string_view path)
{
    if (path.size() < 2 || path[1] != ':') {
        return -1;
    }
    if (path[0] >= 'A' && path[0] < 'A' + kFsDriveCount) {
        return path[0] - 'A';
    }
    return -2;
}

FsRef Fs::resolve(FsRef cwd, std::string_view path) const
{
    i32 drive = cwd.drive;
    i32 node_index = cwd.node;

    if (path.size() >= 2 && path[1] == ':') {
        const i32 pd = path_drive(path);
        if (pd < 0) {
            return FsRef{drive, -1};
        }
        drive = pd;
        node_index = 0;
        path.remove_prefix(2);
    }

    const FsRef bad{drive, -1};
    if (!drives_[drive].mounted) {
        return bad;
    }
    if (!path.empty() && is_separator(path[0])) {
        node_index = 0;
        path.remove_prefix(1);
    }

    const FsDrive& d = drives_[drive];
    while (!path.empty()) {
        std::size_t end = 0;
        while (end < path.size() && !is_separator(path[end])) {
            end++;
        }
        if (end > kFsNameMax) {
            return bad;
        }
        const std::string_view comp = path.substr(0, end);
        path.remove_prefix(end < path.size() ? end + 1 : end);

        if (comp.empty() || comp == ".") {
            continue;
        }
        if (comp == "..") {
            const i32 parent = d.nodes[node_index].parent;
            node_index = parent >= 0 ? parent : 0;
            continue;
        }
        if (!d.nodes[node_index].is_dir) {
            return bad;
        }
        const i32 child = find_child(d, node_index, comp);
        if (child < 0) {
            return bad;
        }
        node_index = child;
    }
    return FsRef{drive, node_index};
}

void Fs::path_string(FsRef ref, char* buf, u32 size) const
{
    std::snprintf(buf, size, "%c:\\", 'A' + (ref.drive >= 0 ? ref.drive : 0));
    if (!valid(ref)) {
        return;
    }

    const FsDrive& d = drives_[ref.drive];
    i32 chain[kFsPathDepth];
    u32 count = 0;
    for (i32 n = ref.node; n > 0 && count < kFsPathDepth; n = d.nodes[n].parent) {
        chain[count++] = n;
    }

    u32 len = static_cast<u32>(std::strlen(buf));
    for (i32 i = static_cast<i32>(count) - 1; i >= 0; i--) {
        const char* sep = i == static_cast<i32>(count) - 1 ? "" : "\\";
        const int wrote = std::snprintf(buf + len, size - len, "%s%s", sep,
                                        d.nodes[chain[i]].name.c_str());
        if (wrote < 0 || static_cast<u32>(wrote) >= size - len) {
            return;
        }
        len += static_cast<u32>(wrote);
    }
}

std::string_view Fs::text(FsRef ref) const
{
    const FsNode* n = node(ref);
    if (!n->rom_text.empty()) {
        return n->rom_text;
    }
    if (n->pool_len > 0) {
        return std::string_view(drives_[ref.drive].pool + n->pool_off, n->pool_len - 1);
    }
    return {};
}

FsRef Fs::mkdir(FsRef dir, std::string_view name)
{
    const FsRef bad{dir.drive, -1};
    if (!valid(dir) || !node(dir)->is_dir || !fs_name_valid(name)) {
        return bad;
    }
    FsDrive& d = drives_[dir.drive];

    const i32 existing = find_child(d, dir.node, name);
    if (existing >= 0) {
        return d.nodes[existing].is_dir ? FsRef{dir.drive, existing} : bad;
    }

    const i32 slot = alloc_node(d);
    if (slot < 0) {
        return bad;
    }
    FsNode& n = d.nodes[slot];
    n.used = true;
    n.is_dir = true;
    n.parent = dir.node;
    n.name.assign(name);
    return FsRef{dir.drive, slot};
}

FsRef Fs::mkfile_rom(FsRef dir, std::string_view name, std::string_view rom_text, u32 size,
                     FsExe exe)
{
    const FsRef bad{dir.drive, -1};
    if (!valid(dir) || !node(dir)->is_dir || !fs_name_valid(name)) {
        return bad;
    }
    FsDrive& d = drives_[dir.drive];
    if (find_child(d, dir.node, name) >= 0) {
        return bad;
    }
    if (size == 0 && !rom_text.empty()) {
        size = static_cast<u32>(rom_text.size());
    }
    if (size > free_bytes(dir.drive)) {
        return bad;
    }

    const i32 slot = alloc_node(d);
    if (slot < 0) {
        return bad;
    }
    FsNode& n = d.nodes[slot];
    n.used = true;
    n.parent = dir.node;
    n.size = size;
    n.exe = exe;
    n.rom_text = rom_text;
    n.name.assign(name);
    return FsRef{dir.drive, slot};
}

FsRef Fs::mkfile_data(FsRef dir, std::string_view name, std::string_view data)
{
    const FsRef bad{dir.drive, -1};
    if (!valid(dir) || !node(dir)->is_dir || !fs_name_valid(name)) {
        return bad;
    }
    FsDrive& d = drives_[dir.drive];
    if (find_child(d, dir.node, name) >= 0) {
        return bad;
    }

    const u32 len = static_cast<u32>(data.size());
    if (len > free_bytes(dir.drive) || d.pool_used + len + 1 > kFsPoolSize) {
        return bad;
    }

    const i32 slot = alloc_node(d);
    if (slot < 0) {
        return bad;
    }
    FsNode& n = d.nodes[slot];
    n.used = true;
    n.parent = dir.node;
    n.size = len;
    n.pool_off = d.pool_used;
    n.pool_len = len + 1;
    if (len) {
        std::memcpy(d.pool + d.pool_used, data.data(), len);
    }
    d.pool[d.pool_used + len] = 0;
    d.pool_used += len + 1;
    n.name.assign(name);
    return FsRef{dir.drive, slot};
}

FsError Fs::remove(FsRef ref)
{
    if (!valid(ref)) {
        return FsError::NotFound;
    }
    FsDrive& d = drives_[ref.drive];
    FsNode& n = d.nodes[ref.node];
    if (n.is_dir) {
        return FsError::IsDir;
    }

    if (n.pool_len > 0) {
        const u32 off = n.pool_off;
        const u32 len = n.pool_len;
        std::memmove(d.pool + off, d.pool + off + len, d.pool_used - off - len);
        d.pool_used -= len;
        for (i32 i = 1; i < kFsDriveNodes; i++) {
            if (d.nodes[i].used && d.nodes[i].pool_len > 0 && d.nodes[i].pool_off > off) {
                d.nodes[i].pool_off -= len;
            }
        }
    }
    n.used = false;
    return FsError::Ok;
}

FsError Fs::rmdir(FsRef ref)
{
    if (!valid(ref)) {
        return FsError::NotFound;
    }
    if (ref.node == 0) {
        return FsError::Self;
    }
    FsDrive& d = drives_[ref.drive];
    FsNode& n = d.nodes[ref.node];
    if (!n.is_dir) {
        return FsError::NotDir;
    }
    for (i32 i = 1; i < kFsDriveNodes; i++) {
        if (d.nodes[i].used && d.nodes[i].parent == ref.node) {
            return FsError::NotEmpty;
        }
    }
    n.used = false;
    return FsError::Ok;
}

FsError Fs::copy(FsRef src, FsRef dst_dir, std::string_view dst_name)
{
    if (!valid(src)) {
        return FsError::NotFound;
    }
    if (!valid(dst_dir)) {
        return FsError::NotReady;
    }

    FsDrive& sd = drives_[src.drive];
    FsNode& sn = sd.nodes[src.node];
    FsDrive& dd = drives_[dst_dir.drive];
    if (sn.is_dir) {
        return FsError::IsDir;
    }
    if (!dd.nodes[dst_dir.node].is_dir) {
        return FsError::NotDir;
    }
    if (!fs_name_valid(dst_name)) {
        return FsError::BadName;
    }

    const i32 existing = find_child(dd, dst_dir.node, dst_name);
    u32 freed = 0;
    u32 pool_freed = 0;
    if (existing >= 0) {
        if (dst_dir.drive == src.drive && existing == src.node) {
            return FsError::Self;
        }
        if (dd.nodes[existing].is_dir) {
            return FsError::IsDir;
        }
        freed = dd.nodes[existing].size;
        pool_freed = dd.nodes[existing].pool_len;
    }
    if (sn.size > free_bytes(dst_dir.drive) + freed) {
        return FsError::NoSpace;
    }
    if (sn.pool_len > 0 && dd.pool_used - pool_freed + sn.pool_len > kFsPoolSize) {
        return FsError::NoSpace;
    }
    if (existing >= 0) {
        remove(FsRef{dst_dir.drive, existing});
    }

    const i32 slot = alloc_node(dd);
    if (slot < 0) {
        return FsError::Full;
    }
    FsNode& dn = dd.nodes[slot];
    dn.used = true;
    dn.parent = dst_dir.node;
    dn.size = sn.size;
    dn.exe = sn.exe;
    dn.pic = sn.pic;
    dn.trk = sn.trk;
    dn.infected = sn.infected;
    dn.corrupted = sn.corrupted;
    dn.run_text = sn.run_text;
    dn.rom_text = sn.rom_text;
    dn.name.assign(dst_name);

    if (sn.pool_len > 0) {
        dn.pool_off = dd.pool_used;
        dn.pool_len = sn.pool_len;
        std::memcpy(dd.pool + dd.pool_used, sd.pool + sn.pool_off, sn.pool_len);
        dd.pool_used += sn.pool_len;
    }
    return FsError::Ok;
}

FsError Fs::move(FsRef src, FsRef dst_dir, std::string_view dst_name)
{
    if (!valid(src)) {
        return FsError::NotFound;
    }
    if (!valid(dst_dir) || !node(dst_dir)->is_dir) {
        return FsError::NotReady;
    }
    if (!fs_name_valid(dst_name)) {
        return FsError::BadName;
    }

    FsDrive& sd = drives_[src.drive];
    FsNode& sn = sd.nodes[src.node];
    if (sn.is_dir) {
        return FsError::IsDir;
    }

    if (src.drive != dst_dir.drive) {
        const FsError err = copy(src, dst_dir, dst_name);
        return err != FsError::Ok ? err : remove(src);
    }

    const i32 existing = find_child(sd, dst_dir.node, dst_name);
    if (existing == src.node) {
        return FsError::Self;
    }
    if (existing >= 0) {
        if (sd.nodes[existing].is_dir) {
            return FsError::IsDir;
        }
        const FsError err = remove(FsRef{dst_dir.drive, existing});
        if (err != FsError::Ok) {
            return err;
        }
    }
    sn.parent = dst_dir.node;
    sn.name.assign(dst_name);
    return FsError::Ok;
}

} // namespace anom
