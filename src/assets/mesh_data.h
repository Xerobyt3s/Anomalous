#pragma once

#include "assets/amsh.h"
#include "core/types.h"
#include "math/vmath.h"

#include <span>
#include <string_view>

namespace anom {

class Arena;

enum class MeshParseError {
    Ok,
    TooSmall,
    BadMagic,
    BadVersion,
    TooManySubmeshes,
    IndexCountNotTriangles,
    SizeMismatch,
    IndexOutOfRange,
    SubmeshOutOfRange,
    ReadFailed,
};

const char* mesh_parse_error_name(MeshParseError error);

struct MeshData {
    std::span<const AmshVertex> vertices;
    std::span<const u32> indices;
    std::span<const AmshSubmesh> submeshes;
    Aabb bounds = aabb_empty();
};

MeshParseError parse_mesh(std::span<const u8> bytes, Arena& arena, MeshData& out);
MeshParseError load_mesh(std::string_view path, Arena& arena, MeshData& out);

} // namespace anom
