#pragma once

#include "core/types.h"
#include "math/vmath.h"
#include "assets/mesh_format.h"

struct Arena;

typedef struct MeshData {
    AmshVertex* vertices;
    u32* indices;
    AmshSubmesh* submeshes;
    u32 vertex_count;
    u32 index_count;
    u32 submesh_count;
    Aabb bounds;
} MeshData;

typedef struct GpuSubmesh {
    u32 first_index;
    u32 index_count;
    u32 texture_slot;
} GpuSubmesh;

typedef struct GpuMesh {
    u32 vao;
    u32 vbo;
    u32 ebo;
    GpuSubmesh submeshes[AMSH_MAX_SUBMESHES];
    u32 submesh_count;
    Aabb bounds;
    b32 loaded;
} GpuMesh;

void assets_init(b32 gpu_enabled);
void assets_shutdown(void);
void assets_hot_reload_poll(f64 now);

b32  assets_load_mesh_data(struct Arena* arena, const char* path, MeshData* out);
u16* assets_load_image_16(struct Arena* arena, const char* path, u32* out_width, u32* out_height);
u8*  assets_load_image_8(struct Arena* arena, const char* path, u32* out_width, u32* out_height);

const GpuMesh* asset_mesh(const char* name);
u32  asset_texture_slot(const char* name);
u32  asset_texture_gl(u32 slot);

void assets_selftest(void);
