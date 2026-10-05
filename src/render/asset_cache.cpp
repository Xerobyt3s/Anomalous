#include "render/asset_cache.h"
#include "assets/image.h"
#include "assets/mesh_data.h"
#include "assets/watcher.h"
#include "core/arena.h"
#include "core/log.h"
#include "platform/filesystem.h"
#include "platform/gl_loader.h"

#include <cstddef>

namespace anom {
namespace {

constexpr std::string_view kGroundPrefix = "island_";
constexpr std::string_view kProceduralMaterial = "island_proc";

} // namespace

void AssetCache::init(FileWatcher& watcher, Arena& scratch)
{
    watcher_ = &watcher;
    scratch_ = &scratch;

    const u8 white[4] = {255, 255, 255, 255};
    glCreateTextures(GL_TEXTURE_2D, 1, &white_texture_);
    glTextureStorage2D(white_texture_, 1, GL_RGBA8, 1, 1);
    glTextureSubImage2D(white_texture_, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white);
}

void AssetCache::shutdown()
{
    for (u32 i = 0; i < kMaxTextures; i++) {
        if (textures_[i].used && textures_[i].gl_texture) {
            glDeleteTextures(1, &textures_[i].gl_texture);
        }
        textures_[i] = TextureEntry{};
    }
    for (u32 i = 0; i < kMaxMeshes; i++) {
        GpuMesh& mesh = meshes_[i].mesh;
        if (meshes_[i].used && mesh.loaded) {
            glDeleteVertexArrays(1, &mesh.vao);
            glDeleteBuffers(1, &mesh.vbo);
            glDeleteBuffers(1, &mesh.ebo);
        }
        meshes_[i] = MeshEntry{};
    }
    if (white_texture_) {
        glDeleteTextures(1, &white_texture_);
        white_texture_ = 0;
    }
}

bool AssetCache::upload_texture(TextureEntry& entry)
{
    ArenaScope scope(*scratch_);
    const Image8 image = load_image_rgba(*scratch_, *scratch_, entry.path.view());
    if (!image.valid()) {
        return false;
    }

    if (entry.gl_texture) {
        glDeleteTextures(1, &entry.gl_texture);
    }

    u32 levels = 1;
    u32 dim = image.width > image.height ? image.width : image.height;
    while (dim >>= 1) {
        levels++;
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &entry.gl_texture);
    glTextureStorage2D(entry.gl_texture, static_cast<GLsizei>(levels), GL_RGBA8,
                       static_cast<GLsizei>(image.width), static_cast<GLsizei>(image.height));
    glTextureSubImage2D(entry.gl_texture, 0, 0, 0, static_cast<GLsizei>(image.width),
                        static_cast<GLsizei>(image.height), GL_RGBA, GL_UNSIGNED_BYTE,
                        image.pixels);
    glGenerateTextureMipmap(entry.gl_texture);
    glTextureParameteri(entry.gl_texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(entry.gl_texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(entry.gl_texture, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(entry.gl_texture, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return true;
}

void AssetCache::on_texture_changed(void* user, std::string_view)
{
    auto* entry = static_cast<TextureEntry*>(user);
    if (entry->owner->upload_texture(*entry)) {
        entry->owner->reload_count_++;
        log_info("assets: texture reloaded %s", entry->name.c_str());
    }
}

u32 AssetCache::texture_slot(std::string_view name)
{
    for (u32 i = 0; i < kMaxTextures; i++) {
        if (textures_[i].used && textures_[i].name == name) {
            return i;
        }
    }
    for (u32 i = 0; i < kMaxTextures; i++) {
        TextureEntry& entry = textures_[i];
        if (entry.used) {
            continue;
        }
        entry.name.assign(name);
        entry.path.format("assets/textures/%.*s.png", static_cast<int>(name.size()), name.data());
        entry.owner = this;
        entry.used = true;
        upload_texture(entry);
        if (watcher_) {
            watcher_->add(entry.path.view(), &on_texture_changed, &entry);
        }
        return i;
    }
    log_error("assets: texture cache exhausted at %u, cannot load %.*s", kMaxTextures,
              static_cast<int>(name.size()), name.data());
    return 0;
}

u32 AssetCache::texture_gl(u32 slot) const
{
    if (slot < kMaxTextures && textures_[slot].used && textures_[slot].gl_texture) {
        return textures_[slot].gl_texture;
    }
    return white_texture_;
}

u32 AssetCache::companion_slot(std::string_view material, std::string_view suffix)
{
    FixedString<32> name;
    name.format("%.*s%.*s", static_cast<int>(material.size()), material.data(),
                static_cast<int>(suffix.size()), suffix.data());
    FixedString<128> path;
    path.format("assets/textures/%s.png", name.c_str());
    if (!fs::exists(path.view())) {
        return kNoTexture;
    }
    return texture_slot(name.view());
}

bool AssetCache::upload_mesh(MeshEntry& entry)
{
    ArenaScope scope(*scratch_);
    MeshData data;
    if (load_mesh(entry.path.view(), *scratch_, data) != MeshParseError::Ok) {
        return false;
    }
    upload_mesh_data(entry.mesh, data);
    return true;
}

void AssetCache::upload_mesh_data(GpuMesh& mesh, const MeshData& data)
{
    if (mesh.loaded) {
        glDeleteVertexArrays(1, &mesh.vao);
        glDeleteBuffers(1, &mesh.vbo);
        glDeleteBuffers(1, &mesh.ebo);
    }

    glCreateBuffers(1, &mesh.vbo);
    glNamedBufferStorage(mesh.vbo,
                         static_cast<GLsizeiptr>(data.vertices.size_bytes()),
                         data.vertices.data(), 0);
    glCreateBuffers(1, &mesh.ebo);
    glNamedBufferStorage(mesh.ebo,
                         static_cast<GLsizeiptr>(data.indices.size_bytes()),
                         data.indices.data(), 0);

    glCreateVertexArrays(1, &mesh.vao);
    glVertexArrayVertexBuffer(mesh.vao, 0, mesh.vbo, 0, sizeof(AmshVertex));
    glVertexArrayElementBuffer(mesh.vao, mesh.ebo);
    glEnableVertexArrayAttrib(mesh.vao, 0);
    glVertexArrayAttribFormat(mesh.vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(AmshVertex, pos));
    glVertexArrayAttribBinding(mesh.vao, 0, 0);
    glEnableVertexArrayAttrib(mesh.vao, 1);
    glVertexArrayAttribFormat(mesh.vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(AmshVertex, normal));
    glVertexArrayAttribBinding(mesh.vao, 1, 0);
    glEnableVertexArrayAttrib(mesh.vao, 2);
    glVertexArrayAttribFormat(mesh.vao, 2, 2, GL_FLOAT, GL_FALSE, offsetof(AmshVertex, uv));
    glVertexArrayAttribBinding(mesh.vao, 2, 0);

    if (instance_vbo_) {
        glVertexArrayVertexBuffer(mesh.vao, 1, instance_vbo_, 0, sizeof(Mat4));
        glVertexArrayBindingDivisor(mesh.vao, 1, 1);
        for (u32 col = 0; col < 4; col++) {
            const u32 attrib = 3 + col;
            glEnableVertexArrayAttrib(mesh.vao, attrib);
            glVertexArrayAttribFormat(mesh.vao, attrib, 4, GL_FLOAT, GL_FALSE,
                                      col * 4 * sizeof(f32));
            glVertexArrayAttribBinding(mesh.vao, attrib, 1);
        }
    }

    mesh.submesh_count = static_cast<u32>(data.submeshes.size());
    for (u32 i = 0; i < mesh.submesh_count; i++) {
        mesh.submeshes[i].first_index = data.submeshes[i].first_index;
        mesh.submeshes[i].index_count = data.submeshes[i].index_count;
        const bool procedural = std::string_view(data.submeshes[i].material) == kProceduralMaterial;
        mesh.submeshes[i].texture_slot = procedural ? kMaxTextures : texture_slot(data.submeshes[i].material);
        mesh.submeshes[i].normal_slot = companion_slot(data.submeshes[i].material, "_n");
        mesh.submeshes[i].surface_slot = companion_slot(data.submeshes[i].material, "_s");
        mesh.submeshes[i].blend_slot = companion_slot(data.submeshes[i].material, "_g");
        mesh.submeshes[i].ground = std::string_view(data.submeshes[i].material).starts_with(kGroundPrefix);
        mesh.submeshes[i].procedural = procedural;
    }
    mesh.bounds = data.bounds;
    mesh.loaded = true;
}

const GpuMesh* AssetCache::create_mesh(std::string_view name, const MeshData& data)
{
    MeshEntry* entry = nullptr;
    for (u32 i = 0; i < kMaxMeshes && !entry; i++) {
        if (meshes_[i].used && meshes_[i].name == name) {
            entry = &meshes_[i];
        }
    }
    for (u32 i = 0; i < kMaxMeshes && !entry; i++) {
        if (!meshes_[i].used) {
            entry = &meshes_[i];
            entry->name.assign(name);
            entry->path.assign("");
            entry->owner = this;
            entry->used = true;
        }
    }
    if (!entry) {
        log_error("assets: mesh cache exhausted at %u, cannot create %.*s", kMaxMeshes,
                  static_cast<int>(name.size()), name.data());
        return nullptr;
    }
    entry->runtime = true;
    upload_mesh_data(entry->mesh, data);
    return &entry->mesh;
}

void AssetCache::on_mesh_changed(void* user, std::string_view)
{
    auto* entry = static_cast<MeshEntry*>(user);
    if (entry->runtime) {
        return;
    }
    if (entry->owner->upload_mesh(*entry)) {
        entry->owner->reload_count_++;
        log_info("assets: mesh reloaded %s", entry->name.c_str());
    }
}

const GpuMesh* AssetCache::mesh(std::string_view name)
{
    for (u32 i = 0; i < kMaxMeshes; i++) {
        if (meshes_[i].used && meshes_[i].name == name) {
            return &meshes_[i].mesh;
        }
    }
    for (u32 i = 0; i < kMaxMeshes; i++) {
        MeshEntry& entry = meshes_[i];
        if (entry.used) {
            continue;
        }
        entry.name.assign(name);
        entry.path.format("assets/meshes/%.*s.amsh", static_cast<int>(name.size()), name.data());
        entry.owner = this;
        entry.used = true;
        upload_mesh(entry);
        if (watcher_) {
            watcher_->add(entry.path.view(), &on_mesh_changed, &entry);
        }
        return &entry.mesh;
    }
    log_error("assets: mesh cache exhausted at %u, cannot load %.*s", kMaxMeshes,
              static_cast<int>(name.size()), name.data());
    return nullptr;
}

} // namespace anom
