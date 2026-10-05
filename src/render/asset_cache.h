#pragma once

#include "core/fixed_string.h"
#include "core/types.h"
#include "engine/render/gl_handle.h"
#include "render/gpu_mesh.h"

#include <string_view>

namespace anom {

class Arena;
class FileWatcher;
struct MeshData;

class AssetCache {
public:
    static constexpr u32 kMaxTextures = 64;
    static constexpr u32 kMaxMeshes = 128;

    void init(FileWatcher& watcher, Arena& scratch);
    void set_instance_buffer(u32 vbo) { instance_vbo_ = vbo; }
    void shutdown();

    u32 texture_slot(std::string_view name);
    u32 texture_gl(u32 slot) const;
    u32 companion_slot(std::string_view material, std::string_view suffix);
    const GpuMesh* mesh(std::string_view name);
    const GpuMesh* create_mesh(std::string_view name, const MeshData& data);

    u32 reload_count() const { return reload_count_; }

private:
    struct TextureEntry {
        FixedString<32> name;
        FixedString<128> path;
        ghost::engine::GlTexture texture;
        bool srgb = true;
        AssetCache* owner = nullptr;
        bool used = false;
    };

    struct MeshEntry {
        FixedString<32> name;
        FixedString<128> path;
        GpuMesh mesh;
        AssetCache* owner = nullptr;
        bool used = false;
        bool runtime = false;
    };

    static void on_texture_changed(void* user, std::string_view path);
    static void on_mesh_changed(void* user, std::string_view path);

    bool upload_texture(TextureEntry& entry);
    bool upload_mesh(MeshEntry& entry);
    void upload_mesh_data(GpuMesh& mesh, const MeshData& data, bool flip_v);

    TextureEntry textures_[kMaxTextures];
    MeshEntry meshes_[kMaxMeshes];
    FileWatcher* watcher_ = nullptr;
    Arena* scratch_ = nullptr;
    u32 instance_vbo_ = 0;
    ghost::engine::GlTexture white_texture_;
    u32 reload_count_ = 0;
};

} // namespace anom
