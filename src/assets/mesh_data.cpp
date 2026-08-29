#include "assets/mesh_data.h"
#include "core/arena.h"
#include "core/log.h"
#include "platform/filesystem.h"

#include <cstring>

namespace anom {

const char* mesh_parse_error_name(MeshParseError error)
{
    switch (error) {
    case MeshParseError::Ok: return "ok";
    case MeshParseError::TooSmall: return "file smaller than header";
    case MeshParseError::BadMagic: return "bad magic";
    case MeshParseError::BadVersion: return "unsupported version";
    case MeshParseError::TooManySubmeshes: return "submesh count over limit";
    case MeshParseError::IndexCountNotTriangles: return "index count not a multiple of 3";
    case MeshParseError::SizeMismatch: return "declared sizes do not match file length";
    case MeshParseError::IndexOutOfRange: return "index references a missing vertex";
    case MeshParseError::SubmeshOutOfRange: return "submesh range exceeds index buffer";
    case MeshParseError::ReadFailed: return "file could not be read";
    }
    return "unknown";
}

MeshParseError parse_mesh(std::span<const u8> bytes, Arena& arena, MeshData& out)
{
    if (bytes.size() < sizeof(AmshHeader)) {
        return MeshParseError::TooSmall;
    }

    AmshHeader header;
    std::memcpy(&header, bytes.data(), sizeof(header));

    if (header.magic != kAmshMagic) {
        return MeshParseError::BadMagic;
    }
    if (header.version != kAmshVersion) {
        return MeshParseError::BadVersion;
    }
    if (header.submesh_count > kAmshMaxSubmeshes) {
        return MeshParseError::TooManySubmeshes;
    }
    if (header.index_count % 3 != 0) {
        return MeshParseError::IndexCountNotTriangles;
    }

    const u64 expected = sizeof(AmshHeader)
                       + static_cast<u64>(header.vertex_count) * sizeof(AmshVertex)
                       + static_cast<u64>(header.index_count) * sizeof(u32)
                       + static_cast<u64>(header.submesh_count) * sizeof(AmshSubmesh);
    if (expected != bytes.size()) {
        return MeshParseError::SizeMismatch;
    }

    auto* vertices = arena.push_array<AmshVertex>(header.vertex_count);
    auto* indices = arena.push_array<u32>(header.index_count);
    auto* submeshes = arena.push_array<AmshSubmesh>(header.submesh_count);
    if ((header.vertex_count && !vertices) || (header.index_count && !indices)
        || (header.submesh_count && !submeshes)) {
        return MeshParseError::ReadFailed;
    }

    const u8* cursor = bytes.data() + sizeof(AmshHeader);
    const u64 vertex_bytes = static_cast<u64>(header.vertex_count) * sizeof(AmshVertex);
    const u64 index_bytes = static_cast<u64>(header.index_count) * sizeof(u32);
    const u64 submesh_bytes = static_cast<u64>(header.submesh_count) * sizeof(AmshSubmesh);

    std::memcpy(vertices, cursor, vertex_bytes);
    cursor += vertex_bytes;
    std::memcpy(indices, cursor, index_bytes);
    cursor += index_bytes;
    std::memcpy(submeshes, cursor, submesh_bytes);

    for (u32 i = 0; i < header.index_count; i++) {
        if (indices[i] >= header.vertex_count) {
            return MeshParseError::IndexOutOfRange;
        }
    }
    for (u32 i = 0; i < header.submesh_count; i++) {
        const u64 end = static_cast<u64>(submeshes[i].first_index) + submeshes[i].index_count;
        if (end > header.index_count) {
            return MeshParseError::SubmeshOutOfRange;
        }
    }

    out.vertices = {vertices, header.vertex_count};
    out.indices = {indices, header.index_count};
    out.submeshes = {submeshes, header.submesh_count};
    out.bounds = aabb_empty();
    for (u32 i = 0; i < header.vertex_count; i++) {
        out.bounds = expand(out.bounds,
                            Vec3{vertices[i].pos[0], vertices[i].pos[1], vertices[i].pos[2]});
    }
    return MeshParseError::Ok;
}

MeshParseError load_mesh(std::string_view path, Arena& arena, MeshData& out)
{
    const fs::FileData file = fs::read_entire_file(arena, path);
    if (!file.valid()) {
        return MeshParseError::ReadFailed;
    }
    const MeshParseError error = parse_mesh({file.data, file.size}, arena, out);
    if (error != MeshParseError::Ok) {
        log_error("mesh: %.*s: %s", static_cast<int>(path.size()), path.data(),
                  mesh_parse_error_name(error));
    }
    return error;
}

} // namespace anom
