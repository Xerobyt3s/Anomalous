#include "assets/assets.h"
#include "core/arena.h"
#include "core/log.h"
#include "platform/platform.h"
#include "platform/gl_loader.h"

#include <string.h>
#include <stdio.h>

static void* stbi_arena_realloc(void* old_ptr, u64 old_size, u64 new_size);
#define STBI_MALLOC(size) arena_push_size(&g_frame_arena, (size), 16)
#define STBI_REALLOC_SIZED(ptr, old_size, new_size) stbi_arena_realloc((ptr), (old_size), (new_size))
#define STBI_FREE(ptr) ((void)(ptr))
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ASSERT(x) ASSERT(x)
#pragma warning(push, 0)
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#pragma warning(pop)

#define MAX_TEXTURES 64
#define MAX_MESHES 64
#define ASSET_PATH_MAX 128
#define HOT_RELOAD_INTERVAL 1.0

static void* stbi_arena_realloc(void* old_ptr, u64 old_size, u64 new_size)
{
    void* fresh = arena_push_size(&g_frame_arena, new_size, 16);
    if (old_ptr && old_size) {
        memcpy(fresh, old_ptr, old_size < new_size ? old_size : new_size);
    }
    return fresh;
}

typedef struct TextureEntry {
    char name[32];
    char path[ASSET_PATH_MAX];
    u32 gl_texture;
    i64 mtime;
    b32 used;
} TextureEntry;

typedef struct MeshEntry {
    char name[32];
    char path[ASSET_PATH_MAX];
    GpuMesh mesh;
    i64 mtime;
    b32 used;
} MeshEntry;

static TextureEntry s_textures[MAX_TEXTURES];
static MeshEntry s_meshes[MAX_MESHES];
static b32 s_gpu_enabled;
static u32 s_white_texture;
static f64 s_next_poll;

void assets_init(b32 gpu_enabled)
{
    s_gpu_enabled = gpu_enabled;
    if (!gpu_enabled) {
        return;
    }
    u8 white[4] = { 255, 255, 255, 255 };
    glCreateTextures(GL_TEXTURE_2D, 1, &s_white_texture);
    glTextureStorage2D(s_white_texture, 1, GL_RGBA8, 1, 1);
    glTextureSubImage2D(s_white_texture, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white);
}

void assets_shutdown(void)
{
    if (!s_gpu_enabled) {
        return;
    }
    for (u32 i = 0; i < MAX_TEXTURES; i++) {
        if (s_textures[i].used) {
            glDeleteTextures(1, &s_textures[i].gl_texture);
        }
    }
    for (u32 i = 0; i < MAX_MESHES; i++) {
        if (s_meshes[i].used && s_meshes[i].mesh.loaded) {
            glDeleteVertexArrays(1, &s_meshes[i].mesh.vao);
            glDeleteBuffers(1, &s_meshes[i].mesh.vbo);
            glDeleteBuffers(1, &s_meshes[i].mesh.ebo);
        }
    }
    glDeleteTextures(1, &s_white_texture);
}

b32 assets_load_mesh_data(struct Arena* arena, const char* path, MeshData* out)
{
    FileData file = platform_read_entire_file(arena, path);
    b32 ok = 0;
    if (file.data && file.size >= sizeof(AmshHeader)) {
        const AmshHeader* header = (const AmshHeader*)file.data;
        u64 expected = sizeof(AmshHeader)
                     + (u64)header->vertex_count * sizeof(AmshVertex)
                     + (u64)header->index_count * sizeof(u32)
                     + (u64)header->submesh_count * sizeof(AmshSubmesh);
        if (header->magic == AMSH_MAGIC && header->version == AMSH_VERSION
            && header->submesh_count <= AMSH_MAX_SUBMESHES
            && header->index_count % 3 == 0
            && expected == file.size) {
            const u8* cursor = file.data + sizeof(AmshHeader);
            out->vertex_count = header->vertex_count;
            out->index_count = header->index_count;
            out->submesh_count = header->submesh_count;
            out->vertices = arena_push_array(arena, AmshVertex, header->vertex_count);
            memcpy(out->vertices, cursor, (u64)header->vertex_count * sizeof(AmshVertex));
            cursor += (u64)header->vertex_count * sizeof(AmshVertex);
            out->indices = arena_push_array(arena, u32, header->index_count);
            memcpy(out->indices, cursor, (u64)header->index_count * sizeof(u32));
            cursor += (u64)header->index_count * sizeof(u32);
            out->submeshes = arena_push_array(arena, AmshSubmesh, header->submesh_count);
            memcpy(out->submeshes, cursor, (u64)header->submesh_count * sizeof(AmshSubmesh));

            ok = 1;
            out->bounds = aabb_empty();
            for (u32 i = 0; i < out->vertex_count; i++) {
                out->bounds = aabb_expand(out->bounds, v3(out->vertices[i].pos[0],
                                                          out->vertices[i].pos[1],
                                                          out->vertices[i].pos[2]));
            }
            for (u32 i = 0; i < out->index_count; i++) {
                if (out->indices[i] >= out->vertex_count) {
                    ok = 0;
                }
            }
            for (u32 i = 0; i < out->submesh_count; i++) {
                const AmshSubmesh* sub = &out->submeshes[i];
                if ((u64)sub->first_index + sub->index_count > out->index_count) {
                    ok = 0;
                }
            }
        }
    }
    if (!ok) {
        log_error("assets: invalid or unreadable mesh %s", path);
    }
    return ok;
}

u16* assets_load_image_16(struct Arena* arena, const char* path, u32* out_width, u32* out_height)
{
    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    FileData file = platform_read_entire_file(&g_frame_arena, path);
    u16* result = 0;
    if (file.data) {
        int width, height, channels;
        u16* pixels = stbi_load_16_from_memory(file.data, (int)file.size, &width, &height, &channels, 1);
        if (pixels) {
            result = arena_push_array(arena, u16, (u64)width * height);
            memcpy(result, pixels, (u64)width * height * sizeof(u16));
            *out_width = (u32)width;
            *out_height = (u32)height;
        } else {
            log_error("assets: failed to decode 16-bit image %s", path);
        }
    }
    arena_temp_end(temp);
    return result;
}

u8* assets_load_image_8(struct Arena* arena, const char* path, u32* out_width, u32* out_height)
{
    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    FileData file = platform_read_entire_file(&g_frame_arena, path);
    u8* result = 0;
    if (file.data) {
        int width, height, channels;
        u8* pixels = stbi_load_from_memory(file.data, (int)file.size, &width, &height, &channels, 1);
        if (pixels) {
            result = arena_push_array(arena, u8, (u64)width * height);
            memcpy(result, pixels, (u64)width * height);
            *out_width = (u32)width;
            *out_height = (u32)height;
        } else {
            log_error("assets: failed to decode image %s", path);
        }
    }
    arena_temp_end(temp);
    return result;
}

static b32 texture_upload(TextureEntry* entry)
{
    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    FileData file = platform_read_entire_file(&g_frame_arena, entry->path);
    b32 ok = 0;
    if (file.data) {
        int width, height, channels;
        u8* pixels = stbi_load_from_memory(file.data, (int)file.size, &width, &height, &channels, 4);
        if (pixels) {
            if (entry->gl_texture) {
                glDeleteTextures(1, &entry->gl_texture);
            }
            u32 levels = 1;
            u32 dim = (u32)(width > height ? width : height);
            while (dim >>= 1) {
                levels++;
            }
            glCreateTextures(GL_TEXTURE_2D, 1, &entry->gl_texture);
            glTextureStorage2D(entry->gl_texture, (GLsizei)levels, GL_RGBA8, width, height);
            glTextureSubImage2D(entry->gl_texture, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            glGenerateTextureMipmap(entry->gl_texture);
            glTextureParameteri(entry->gl_texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTextureParameteri(entry->gl_texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(entry->gl_texture, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTextureParameteri(entry->gl_texture, GL_TEXTURE_WRAP_T, GL_REPEAT);
            ok = 1;
        } else {
            log_error("assets: failed to decode texture %s", entry->path);
        }
    }
    arena_temp_end(temp);
    return ok;
}

u32 asset_texture_slot(const char* name)
{
    for (u32 i = 0; i < MAX_TEXTURES; i++) {
        if (s_textures[i].used && strcmp(s_textures[i].name, name) == 0) {
            return i;
        }
    }
    for (u32 i = 0; i < MAX_TEXTURES; i++) {
        TextureEntry* entry = &s_textures[i];
        if (entry->used) {
            continue;
        }
        snprintf(entry->name, sizeof(entry->name), "%s", name);
        snprintf(entry->path, sizeof(entry->path), "assets/textures/%s.png", name);
        entry->mtime = platform_file_mtime(entry->path);
        entry->gl_texture = 0;
        entry->used = 1;
        if (s_gpu_enabled && !texture_upload(entry)) {
            entry->gl_texture = 0;
        }
        return i;
    }
    ASSERT(0);
    return 0;
}

u32 asset_texture_gl(u32 slot)
{
    if (slot < MAX_TEXTURES && s_textures[slot].used && s_textures[slot].gl_texture) {
        return s_textures[slot].gl_texture;
    }
    return s_white_texture;
}

static b32 mesh_upload(MeshEntry* entry)
{
    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    MeshData data;
    b32 ok = assets_load_mesh_data(&g_frame_arena, entry->path, &data);
    if (ok) {
        GpuMesh* mesh = &entry->mesh;
        if (mesh->loaded) {
            glDeleteVertexArrays(1, &mesh->vao);
            glDeleteBuffers(1, &mesh->vbo);
            glDeleteBuffers(1, &mesh->ebo);
        }
        glCreateBuffers(1, &mesh->vbo);
        glNamedBufferStorage(mesh->vbo, (GLsizeiptr)((u64)data.vertex_count * sizeof(AmshVertex)),
                             data.vertices, 0);
        glCreateBuffers(1, &mesh->ebo);
        glNamedBufferStorage(mesh->ebo, (GLsizeiptr)((u64)data.index_count * sizeof(u32)),
                             data.indices, 0);
        glCreateVertexArrays(1, &mesh->vao);
        glVertexArrayVertexBuffer(mesh->vao, 0, mesh->vbo, 0, sizeof(AmshVertex));
        glVertexArrayElementBuffer(mesh->vao, mesh->ebo);
        glEnableVertexArrayAttrib(mesh->vao, 0);
        glVertexArrayAttribFormat(mesh->vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(AmshVertex, pos));
        glVertexArrayAttribBinding(mesh->vao, 0, 0);
        glEnableVertexArrayAttrib(mesh->vao, 1);
        glVertexArrayAttribFormat(mesh->vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(AmshVertex, normal));
        glVertexArrayAttribBinding(mesh->vao, 1, 0);
        glEnableVertexArrayAttrib(mesh->vao, 2);
        glVertexArrayAttribFormat(mesh->vao, 2, 2, GL_FLOAT, GL_FALSE, offsetof(AmshVertex, uv));
        glVertexArrayAttribBinding(mesh->vao, 2, 0);

        mesh->submesh_count = data.submesh_count;
        for (u32 i = 0; i < data.submesh_count; i++) {
            mesh->submeshes[i].first_index = data.submeshes[i].first_index;
            mesh->submeshes[i].index_count = data.submeshes[i].index_count;
            mesh->submeshes[i].texture_slot = asset_texture_slot(data.submeshes[i].material);
        }
        mesh->bounds = data.bounds;
        mesh->loaded = 1;
    }
    arena_temp_end(temp);
    return ok;
}

const GpuMesh* asset_mesh(const char* name)
{
    for (u32 i = 0; i < MAX_MESHES; i++) {
        if (s_meshes[i].used && strcmp(s_meshes[i].name, name) == 0) {
            return &s_meshes[i].mesh;
        }
    }
    for (u32 i = 0; i < MAX_MESHES; i++) {
        MeshEntry* entry = &s_meshes[i];
        if (entry->used) {
            continue;
        }
        snprintf(entry->name, sizeof(entry->name), "%s", name);
        snprintf(entry->path, sizeof(entry->path), "assets/meshes/%s.amsh", name);
        entry->mtime = platform_file_mtime(entry->path);
        entry->used = 1;
        entry->mesh.loaded = 0;
        if (s_gpu_enabled) {
            mesh_upload(entry);
        }
        return &entry->mesh;
    }
    ASSERT(0);
    return 0;
}

void assets_hot_reload_poll(f64 now)
{
    if (!s_gpu_enabled || now < s_next_poll) {
        return;
    }
    s_next_poll = now + HOT_RELOAD_INTERVAL;
    for (u32 i = 0; i < MAX_TEXTURES; i++) {
        TextureEntry* entry = &s_textures[i];
        if (!entry->used) {
            continue;
        }
        i64 mtime = platform_file_mtime(entry->path);
        if (mtime != entry->mtime) {
            entry->mtime = mtime;
            if (texture_upload(entry)) {
                log_info("assets: texture reloaded %s", entry->name);
            }
        }
    }
    for (u32 i = 0; i < MAX_MESHES; i++) {
        MeshEntry* entry = &s_meshes[i];
        if (!entry->used) {
            continue;
        }
        i64 mtime = platform_file_mtime(entry->path);
        if (mtime != entry->mtime) {
            entry->mtime = mtime;
            if (mesh_upload(entry)) {
                log_info("assets: mesh reloaded %s", entry->name);
            }
        }
    }
}

void assets_selftest(void)
{
    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    MeshData data;
    b32 ok = assets_load_mesh_data(&g_frame_arena, "assets/meshes/garage.amsh", &data);
    ASSERT(ok);
    ASSERT(data.vertex_count > 0 && data.index_count > 0 && data.submesh_count == 2);
    ASSERT(data.bounds.max.x > data.bounds.min.x);
    arena_temp_end(temp);
    log_info("assets selftest passed");
}
