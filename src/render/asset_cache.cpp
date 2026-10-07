#include "render/asset_cache.h"
#include "assets/image.h"
#include "assets/mesh_data.h"
#include "assets/watcher.h"
#include "core/arena.h"
#include "core/log.h"
#include "platform/filesystem.h"
#include "engine/assets/asset_path.h"
#include "engine/render/texture.h"
#include "platform/gl_loader.h"

#include <cstddef>
#include <vector>

namespace anom {
namespace {

constexpr std::string_view kGroundPrefix = "island_";
constexpr std::string_view kProceduralMaterial = "island_proc";

bool is_data_map(std::string_view name)
{
    return name.ends_with("_n") || name.ends_with("_s") || name.ends_with("_g");
}

static_assert(sizeof(AmshVertex) == sizeof(ghost::engine::Vertex));

} // namespace

void AssetCache::init(FileWatcher& watcher, Arena& scratch)
{
    watcher_ = &watcher;
    scratch_ = &scratch;
    if (const auto text = ghost::engine::readAsset("assets/data/glow.json")) {
        glow_ = parse_mesh_glow(*text);
    }

    const u8 white[4] = {255, 255, 255, 255};
    GLuint id = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &id);
    glTextureStorage2D(id, 1, GL_RGBA8, 1, 1);
    glTextureSubImage2D(id, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white);
    white_texture_ = ghost::engine::GlTexture(id);
}

void AssetCache::shutdown()
{
    for (u32 i = 0; i < kMaxTextures; i++) {
        textures_[i] = TextureEntry{};
    }
    for (u32 i = 0; i < kMaxMeshes; i++) {
        meshes_[i] = MeshEntry{};
    }
    white_texture_.reset();
}

bool AssetCache::upload_texture(TextureEntry& entry)
{
    ghost::engine::GlTexture texture =
        ghost::engine::loadTexture(ghost::engine::assetPath(entry.path.view().substr(7)), entry.srgb);
    if (!texture) {
        return false;
    }
    entry.texture = std::move(texture);
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
        entry.srgb = !is_data_map(name);
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
    if (slot < kMaxTextures && textures_[slot].used && textures_[slot].texture) {
        return textures_[slot].texture.id();
    }
    return white_texture_.id();
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
    upload_mesh_data(entry.mesh, data, true);
    entry.mesh.glow = mesh_glow(glow_, entry.name.view());
    return true;
}

void AssetCache::upload_mesh_data(GpuMesh& mesh, const MeshData& data, bool flip_v)
{
    std::vector<ghost::engine::Vertex> vertices(data.vertices.size());
    for (size_t i = 0; i < data.vertices.size(); i++) {
        const AmshVertex& v = data.vertices[i];
        vertices[i].position = glm::vec3(v.pos[0], v.pos[1], v.pos[2]);
        vertices[i].normal = glm::vec3(v.normal[0], v.normal[1], v.normal[2]);
        vertices[i].uv = glm::vec2(v.uv[0], flip_v ? 1.0f - v.uv[1] : v.uv[1]);
    }
    mesh.gpu.reset();
    mesh.gpu.emplace(vertices, std::span<const std::uint32_t>(data.indices.data(), data.indices.size()));
    const GLuint vao = mesh.gpu->vao();

    if (instance_vbo_ && vao) {
        glVertexArrayVertexBuffer(vao, 1, instance_vbo_, 0, sizeof(Mat4));
        glVertexArrayBindingDivisor(vao, 1, 1);
        for (u32 col = 0; col < 4; col++) {
            const u32 attrib = 3 + col;
            glEnableVertexArrayAttrib(vao, attrib);
            glVertexArrayAttribFormat(vao, attrib, 4, GL_FLOAT, GL_FALSE, col * 4 * sizeof(f32));
            glVertexArrayAttribBinding(vao, attrib, 1);
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
    upload_mesh_data(entry->mesh, data, false);
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
