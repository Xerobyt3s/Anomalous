#pragma once

#include "core/fixed_string.h"
#include "core/types.h"
#include "render/gpu_mesh.h"

#include <string_view>

namespace anom {

class Arena;
class FileWatcher;

class AssetCache {
public:
    static constexpr u32 kMaxTextures = 64;
    static constexpr u32 kMaxMeshes = 64;

    void init(FileWatcher& watcher, Arena& scratch);
    void shutdown();

    u32 texture_slot(std::string_view name);
    u32 texture_gl(u32 slot) const;
    const GpuMesh* mesh(std::string_view name);

    u32 reload_count() const { return reload_count_; }

private:
    struct TextureEntry {
        FixedString<32> name;
        FixedString<128> path;
        u32 gl_texture = 0;
        AssetCache* owner = nullptr;
        bool used = false;
    };

    struct MeshEntry {
        FixedString<32> name;
        FixedString<128> path;
        GpuMesh mesh;
        AssetCache* owner = nullptr;
        bool used = false;
    };

    static void on_texture_changed(void* user, std::string_view path);
    static void on_mesh_changed(void* user, std::string_view path);

    bool upload_texture(TextureEntry& entry);
    bool upload_mesh(MeshEntry& entry);

    TextureEntry textures_[kMaxTextures];
    MeshEntry meshes_[kMaxMeshes];
    FileWatcher* watcher_ = nullptr;
    Arena* scratch_ = nullptr;
    u32 white_texture_ = 0;
    u32 reload_count_ = 0;
};

} // namespace anom
