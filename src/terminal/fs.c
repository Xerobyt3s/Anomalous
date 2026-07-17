#include "terminal/fs.h"

#include <stdio.h>
#include <string.h>

#define FS_A_CAPACITY 362496
#define FS_PATH_DEPTH 16

static i32 fs_find_child(const FsDrive* d, i32 dir, const char* name)
{
    for (i32 i = 1; i < FS_DRIVE_NODES; i++) {
        if (d->nodes[i].used && d->nodes[i].parent == dir
            && strcmp(d->nodes[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static i32 fs_node_alloc(FsDrive* d)
{
    for (i32 i = 1; i < FS_DRIVE_NODES; i++) {
        if (!d->nodes[i].used) {
            FsNode zero = {0};
            d->nodes[i] = zero;
            return i;
        }
    }
    return -1;
}

static void fs_drive_reset(FsDrive* d)
{
    memset(d->nodes, 0, sizeof(d->nodes));
    d->pool_used = 0;
    FsNode* root = &d->nodes[0];
    root->used = 1;
    root->is_dir = 1;
    root->parent = -1;
    root->name[0] = 0;
}

void fs_mount(Fs* fs, i32 drive, const char* label, u32 capacity)
{
    FsDrive* d = &fs->drives[drive];
    memset(d, 0, sizeof(*d));
    d->mounted = 1;
    d->capacity = capacity;
    snprintf(d->label, sizeof(d->label), "%s", label);
    fs_drive_reset(d);
}

void fs_unmount(Fs* fs, i32 drive)
{
    fs->drives[drive].mounted = 0;
}

void fs_format(Fs* fs, i32 drive, const char* label)
{
    FsDrive* d = &fs->drives[drive];
    if (!d->mounted) {
        return;
    }
    if (label) {
        snprintf(d->label, sizeof(d->label), "%s", label);
    }
    fs_drive_reset(d);
}

b32 fs_drive_mounted(const Fs* fs, i32 drive)
{
    return drive >= 0 && drive < FS_DRIVE_COUNT && fs->drives[drive].mounted;
}

u32 fs_used_bytes(const Fs* fs, i32 drive)
{
    const FsDrive* d = &fs->drives[drive];
    u32 total = 0;
    for (i32 i = 1; i < FS_DRIVE_NODES; i++) {
        if (d->nodes[i].used) {
            total += d->nodes[i].size;
        }
    }
    return total;
}

u32 fs_free_bytes(const Fs* fs, i32 drive)
{
    const FsDrive* d = &fs->drives[drive];
    u32 used = fs_used_bytes(fs, drive);
    return used >= d->capacity ? 0 : d->capacity - used;
}

FsRef fs_root(i32 drive)
{
    FsRef ref = { drive, 0 };
    return ref;
}

b32 fs_ref_valid(const Fs* fs, FsRef ref)
{
    return ref.drive >= 0 && ref.drive < FS_DRIVE_COUNT
           && fs->drives[ref.drive].mounted
           && ref.node >= 0 && ref.node < FS_DRIVE_NODES
           && fs->drives[ref.drive].nodes[ref.node].used;
}

const FsNode* fs_node(const Fs* fs, FsRef ref)
{
    return &fs->drives[ref.drive].nodes[ref.node];
}

FsNode* fs_node_mut(Fs* fs, FsRef ref)
{
    return &fs->drives[ref.drive].nodes[ref.node];
}

i32 fs_path_drive(const char* path)
{
    if (!path[0] || path[1] != ':') {
        return -1;
    }
    if (path[0] >= 'A' && path[0] < 'A' + FS_DRIVE_COUNT) {
        return path[0] - 'A';
    }
    return -2;
}

FsRef fs_resolve(const Fs* fs, FsRef cwd, const char* path)
{
    i32 drive = cwd.drive;
    i32 node = cwd.node;
    const char* p = path;
    if (p[0] && p[1] == ':') {
        i32 pd = fs_path_drive(p);
        if (pd < 0) {
            FsRef bad = { drive, -1 };
            return bad;
        }
        drive = pd;
        node = 0;
        p += 2;
    }
    FsRef bad = { drive, -1 };
    if (!fs->drives[drive].mounted) {
        return bad;
    }
    if (*p == '\\' || *p == '/') {
        node = 0;
        p++;
    }
    const FsDrive* d = &fs->drives[drive];
    while (*p) {
        char comp[FS_NAME_MAX + 1];
        u32 n = 0;
        while (*p && *p != '\\' && *p != '/') {
            if (n >= FS_NAME_MAX) {
                return bad;
            }
            comp[n++] = *p++;
        }
        comp[n] = 0;
        if (*p) {
            p++;
        }
        if (n == 0 || strcmp(comp, ".") == 0) {
            continue;
        }
        if (strcmp(comp, "..") == 0) {
            i32 parent = d->nodes[node].parent;
            node = parent >= 0 ? parent : 0;
            continue;
        }
        if (!d->nodes[node].is_dir) {
            return bad;
        }
        i32 child = fs_find_child(d, node, comp);
        if (child < 0) {
            return bad;
        }
        node = child;
    }
    FsRef out = { drive, node };
    return out;
}

void fs_path_string(const Fs* fs, FsRef ref, char* buf, u32 size)
{
    snprintf(buf, size, "%c:\\", 'A' + (ref.drive >= 0 ? ref.drive : 0));
    if (!fs_ref_valid(fs, ref)) {
        return;
    }
    const FsDrive* d = &fs->drives[ref.drive];
    i32 chain[FS_PATH_DEPTH];
    i32 count = 0;
    for (i32 n = ref.node; n > 0 && count < FS_PATH_DEPTH; n = d->nodes[n].parent) {
        chain[count++] = n;
    }
    u32 len = (u32)strlen(buf);
    for (i32 i = count - 1; i >= 0; i--) {
        const char* sep = i == count - 1 ? "" : "\\";
        i32 wrote = snprintf(buf + len, size - len, "%s%s", sep, d->nodes[chain[i]].name);
        if (wrote < 0 || (u32)wrote >= size - len) {
            return;
        }
        len += (u32)wrote;
    }
}

const char* fs_text(const Fs* fs, FsRef ref)
{
    const FsNode* n = fs_node(fs, ref);
    if (n->rom_text) {
        return n->rom_text;
    }
    if (n->pool_len > 0) {
        return fs->drives[ref.drive].pool + n->pool_off;
    }
    return 0;
}

b32 fs_name_valid(const char* name)
{
    if (!name[0]) {
        return 0;
    }
    u32 base = 0;
    u32 ext = 0;
    b32 dot = 0;
    for (const char* p = name; *p; p++) {
        char c = *p;
        if (c == '.') {
            if (dot || p == name) {
                return 0;
            }
            dot = 1;
            continue;
        }
        b32 ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            return 0;
        }
        if (dot) {
            ext++;
        } else {
            base++;
        }
    }
    if (base < 1 || base > 8) {
        return 0;
    }
    if (dot && (ext < 1 || ext > 3)) {
        return 0;
    }
    return 1;
}

FsRef fs_mkdir(Fs* fs, FsRef dir, const char* name)
{
    FsRef bad = { dir.drive, -1 };
    if (!fs_ref_valid(fs, dir) || !fs_node(fs, dir)->is_dir || !fs_name_valid(name)) {
        return bad;
    }
    FsDrive* d = &fs->drives[dir.drive];
    i32 existing = fs_find_child(d, dir.node, name);
    if (existing >= 0) {
        if (d->nodes[existing].is_dir) {
            FsRef ref = { dir.drive, existing };
            return ref;
        }
        return bad;
    }
    i32 slot = fs_node_alloc(d);
    if (slot < 0) {
        return bad;
    }
    FsNode* n = &d->nodes[slot];
    n->used = 1;
    n->is_dir = 1;
    n->parent = dir.node;
    snprintf(n->name, sizeof(n->name), "%s", name);
    FsRef ref = { dir.drive, slot };
    return ref;
}

FsRef fs_mkfile_rom(Fs* fs, FsRef dir, const char* name, const char* text, u32 size, i32 exe)
{
    FsRef bad = { dir.drive, -1 };
    if (!fs_ref_valid(fs, dir) || !fs_node(fs, dir)->is_dir || !fs_name_valid(name)) {
        return bad;
    }
    FsDrive* d = &fs->drives[dir.drive];
    if (fs_find_child(d, dir.node, name) >= 0) {
        return bad;
    }
    if (size == 0 && text) {
        size = (u32)strlen(text);
    }
    if (size > fs_free_bytes(fs, dir.drive)) {
        return bad;
    }
    i32 slot = fs_node_alloc(d);
    if (slot < 0) {
        return bad;
    }
    FsNode* n = &d->nodes[slot];
    n->used = 1;
    n->parent = dir.node;
    n->size = size;
    n->exe = exe;
    n->rom_text = text;
    snprintf(n->name, sizeof(n->name), "%s", name);
    FsRef ref = { dir.drive, slot };
    return ref;
}

FsRef fs_mkfile_data(Fs* fs, FsRef dir, const char* name, const char* text, u32 len)
{
    FsRef bad = { dir.drive, -1 };
    if (!fs_ref_valid(fs, dir) || !fs_node(fs, dir)->is_dir || !fs_name_valid(name)) {
        return bad;
    }
    FsDrive* d = &fs->drives[dir.drive];
    if (fs_find_child(d, dir.node, name) >= 0) {
        return bad;
    }
    if (len == 0 && text) {
        len = (u32)strlen(text);
    }
    if (len > fs_free_bytes(fs, dir.drive) || d->pool_used + len + 1 > FS_POOL_SIZE) {
        return bad;
    }
    i32 slot = fs_node_alloc(d);
    if (slot < 0) {
        return bad;
    }
    FsNode* n = &d->nodes[slot];
    n->used = 1;
    n->parent = dir.node;
    n->size = len;
    n->pool_off = d->pool_used;
    n->pool_len = len + 1;
    memcpy(d->pool + d->pool_used, text, len);
    d->pool[d->pool_used + len] = 0;
    d->pool_used += len + 1;
    snprintf(n->name, sizeof(n->name), "%s", name);
    FsRef ref = { dir.drive, slot };
    return ref;
}

FsError fs_delete(Fs* fs, FsRef ref)
{
    if (!fs_ref_valid(fs, ref)) {
        return FS_ERR_NOT_FOUND;
    }
    FsDrive* d = &fs->drives[ref.drive];
    FsNode* n = &d->nodes[ref.node];
    if (n->is_dir) {
        return FS_ERR_IS_DIR;
    }
    if (n->pool_len > 0) {
        u32 off = n->pool_off;
        u32 len = n->pool_len;
        memmove(d->pool + off, d->pool + off + len, d->pool_used - off - len);
        d->pool_used -= len;
        for (i32 i = 1; i < FS_DRIVE_NODES; i++) {
            if (d->nodes[i].used && d->nodes[i].pool_len > 0 && d->nodes[i].pool_off > off) {
                d->nodes[i].pool_off -= len;
            }
        }
    }
    n->used = 0;
    return FS_OK;
}

FsError fs_copy(Fs* fs, FsRef src, FsRef dst_dir, const char* dst_name)
{
    if (!fs_ref_valid(fs, src)) {
        return FS_ERR_NOT_FOUND;
    }
    if (!fs_ref_valid(fs, dst_dir)) {
        return FS_ERR_NOT_READY;
    }
    FsDrive* sd = &fs->drives[src.drive];
    FsNode* sn = &sd->nodes[src.node];
    FsDrive* dd = &fs->drives[dst_dir.drive];
    if (sn->is_dir) {
        return FS_ERR_IS_DIR;
    }
    if (!dd->nodes[dst_dir.node].is_dir) {
        return FS_ERR_NOT_FOUND;
    }
    if (!fs_name_valid(dst_name)) {
        return FS_ERR_BAD_NAME;
    }
    i32 existing = fs_find_child(dd, dst_dir.node, dst_name);
    u32 freed = 0;
    u32 pool_freed = 0;
    if (existing >= 0) {
        if (dst_dir.drive == src.drive && existing == src.node) {
            return FS_ERR_SELF;
        }
        if (dd->nodes[existing].is_dir) {
            return FS_ERR_IS_DIR;
        }
        freed = dd->nodes[existing].size;
        pool_freed = dd->nodes[existing].pool_len;
    }
    if (sn->size > fs_free_bytes(fs, dst_dir.drive) + freed) {
        return FS_ERR_NO_SPACE;
    }
    if (sn->pool_len > 0 && dd->pool_used - pool_freed + sn->pool_len > FS_POOL_SIZE) {
        return FS_ERR_NO_SPACE;
    }
    if (existing >= 0) {
        FsRef ex = { dst_dir.drive, existing };
        fs_delete(fs, ex);
    }
    i32 slot = fs_node_alloc(dd);
    if (slot < 0) {
        return FS_ERR_FULL;
    }
    FsNode* dn = &dd->nodes[slot];
    dn->used = 1;
    dn->parent = dst_dir.node;
    dn->size = sn->size;
    dn->exe = sn->exe;
    dn->pic = sn->pic;
    dn->infected = sn->infected;
    dn->corrupted = sn->corrupted;
    dn->run_text = sn->run_text;
    dn->rom_text = sn->rom_text;
    snprintf(dn->name, sizeof(dn->name), "%s", dst_name);
    if (sn->pool_len > 0) {
        dn->pool_off = dd->pool_used;
        dn->pool_len = sn->pool_len;
        memcpy(dd->pool + dd->pool_used, sd->pool + sn->pool_off, sn->pool_len);
        dd->pool_used += sn->pool_len;
    }
    return FS_OK;
}

void fs_init(Fs* fs)
{
    memset(fs, 0, sizeof(*fs));
    fs_mount(fs, FS_DRIVE_A, "DR-OS SYSTEM", FS_A_CAPACITY);
}
