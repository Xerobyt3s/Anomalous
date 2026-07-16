#pragma once

#include "core/types.h"

#define AMSH_MAGIC 0x48534D41u
#define AMSH_VERSION 1u
#define AMSH_MATERIAL_NAME_MAX 32
#define AMSH_MAX_SUBMESHES 16

typedef struct AmshHeader {
    u32 magic;
    u32 version;
    u32 vertex_count;
    u32 index_count;
    u32 submesh_count;
} AmshHeader;

typedef struct AmshVertex {
    f32 pos[3];
    f32 normal[3];
    f32 uv[2];
} AmshVertex;

typedef struct AmshSubmesh {
    u32 first_index;
    u32 index_count;
    char material[AMSH_MATERIAL_NAME_MAX];
} AmshSubmesh;
