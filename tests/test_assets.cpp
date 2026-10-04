#include "test.h"

#include "assets/amsh.h"
#include "assets/image.h"
#include "assets/mesh_data.h"
#include "assets/watcher.h"
#include "core/arena.h"

#include <cstring>
#include <vector>

using namespace anom;

namespace {

std::vector<u8> build_mesh(u32 vertex_count, u32 index_count, u32 submesh_count,
                           u32 magic = kAmshMagic, u32 version = kAmshVersion)
{
    AmshHeader header;
    header.magic = magic;
    header.version = version;
    header.vertex_count = vertex_count;
    header.index_count = index_count;
    header.submesh_count = submesh_count;

    std::vector<u8> bytes(sizeof(AmshHeader)
                          + vertex_count * sizeof(AmshVertex)
                          + index_count * sizeof(u32)
                          + submesh_count * sizeof(AmshSubmesh));

    std::memcpy(bytes.data(), &header, sizeof(header));

    u8* cursor = bytes.data() + sizeof(AmshHeader);
    for (u32 i = 0; i < vertex_count; i++) {
        AmshVertex v{};
        v.pos[0] = static_cast<f32>(i);
        v.pos[1] = static_cast<f32>(i) * 2.0f;
        v.pos[2] = -static_cast<f32>(i);
        std::memcpy(cursor, &v, sizeof(v));
        cursor += sizeof(v);
    }
    for (u32 i = 0; i < index_count; i++) {
        const u32 index = vertex_count ? i % vertex_count : 0;
        std::memcpy(cursor, &index, sizeof(index));
        cursor += sizeof(index);
    }
    for (u32 i = 0; i < submesh_count; i++) {
        AmshSubmesh sub{};
        sub.first_index = 0;
        sub.index_count = index_count;
        std::memcpy(cursor, &sub, sizeof(sub));
        cursor += sizeof(sub);
    }
    return bytes;
}

struct WatchCounter {
    u32 hits = 0;
    FixedString<192> last;
};

void on_change(void* user, std::string_view path)
{
    auto* counter = static_cast<WatchCounter*>(user);
    counter->hits++;
    counter->last.assign(path);
}

i64 g_fake_mtime = 100;

i64 fake_stat(std::string_view)
{
    return g_fake_mtime;
}

} // namespace

TEST(mesh, parses_a_valid_buffer)
{
    Arena arena(megabytes(4));
    const std::vector<u8> bytes = build_mesh(6, 6, 2);

    MeshData mesh;
    CHECK(parse_mesh(bytes, arena, mesh) == MeshParseError::Ok);
    CHECK(mesh.vertices.size() == 6);
    CHECK(mesh.indices.size() == 6);
    CHECK(mesh.submeshes.size() == 2);
    CHECK(mesh.bounds.max.x > mesh.bounds.min.x);
    CHECK_NEAR(mesh.bounds.min.x, 0.0f, 1e-6);
    CHECK_NEAR(mesh.bounds.max.x, 5.0f, 1e-6);
}

TEST(mesh, rejects_truncated_file)
{
    Arena arena(megabytes(1));
    MeshData mesh;
    const u8 tiny[4] = {};
    CHECK(parse_mesh(tiny, arena, mesh) == MeshParseError::TooSmall);
}

TEST(mesh, rejects_bad_magic_and_version)
{
    Arena arena(megabytes(1));
    MeshData mesh;

    const std::vector<u8> bad_magic = build_mesh(3, 3, 1, 0xBADF00Du);
    CHECK(parse_mesh(bad_magic, arena, mesh) == MeshParseError::BadMagic);

    const std::vector<u8> bad_version = build_mesh(3, 3, 1, kAmshMagic, 99u);
    CHECK(parse_mesh(bad_version, arena, mesh) == MeshParseError::BadVersion);
}

TEST(mesh, rejects_non_triangle_index_count)
{
    Arena arena(megabytes(1));
    MeshData mesh;
    const std::vector<u8> bytes = build_mesh(4, 4, 1);
    CHECK(parse_mesh(bytes, arena, mesh) == MeshParseError::IndexCountNotTriangles);
}

TEST(mesh, rejects_size_mismatch)
{
    Arena arena(megabytes(1));
    MeshData mesh;
    std::vector<u8> bytes = build_mesh(3, 3, 1);
    bytes.push_back(0);
    CHECK(parse_mesh(bytes, arena, mesh) == MeshParseError::SizeMismatch);
}

TEST(mesh, rejects_too_many_submeshes)
{
    Arena arena(megabytes(1));
    MeshData mesh;
    const std::vector<u8> bytes = build_mesh(3, 3, kAmshMaxSubmeshes + 1);
    CHECK(parse_mesh(bytes, arena, mesh) == MeshParseError::TooManySubmeshes);
}

TEST(mesh, rejects_out_of_range_index)
{
    Arena arena(megabytes(1));
    MeshData mesh;
    std::vector<u8> bytes = build_mesh(3, 3, 1);

    const u32 bad = 99;
    std::memcpy(bytes.data() + sizeof(AmshHeader) + 3 * sizeof(AmshVertex), &bad, sizeof(bad));
    CHECK(parse_mesh(bytes, arena, mesh) == MeshParseError::IndexOutOfRange);
}

TEST(mesh, rejects_submesh_past_index_buffer)
{
    Arena arena(megabytes(1));
    MeshData mesh;
    std::vector<u8> bytes = build_mesh(3, 3, 1);

    const std::size_t submesh_offset = sizeof(AmshHeader) + 3 * sizeof(AmshVertex)
                                     + 3 * sizeof(u32);
    AmshSubmesh sub{};
    sub.first_index = 2;
    sub.index_count = 10;
    std::memcpy(bytes.data() + submesh_offset, &sub, sizeof(sub));
    CHECK(parse_mesh(bytes, arena, mesh) == MeshParseError::SubmeshOutOfRange);
}

TEST(mesh, loads_the_shipped_garage_mesh)
{
    Arena arena(megabytes(16));
    MeshData mesh;
    CHECK(load_mesh("assets/meshes/garage.amsh", arena, mesh) == MeshParseError::Ok);
    CHECK(!mesh.vertices.empty());
    CHECK(!mesh.indices.empty());
    CHECK(mesh.submeshes.size() == 2);
    CHECK(mesh.bounds.max.x > mesh.bounds.min.x);
    CHECK(mesh.bounds.max.y > mesh.bounds.min.y);
}

TEST(mesh, the_telescope_ships_with_a_collision_hull_under_its_structure)
{
    Arena arena(megabytes(32));
    MeshData render;
    MeshData hull;
    CHECK(load_mesh("assets/meshes/telescope.amsh", arena, render) == MeshParseError::Ok);
    CHECK(load_mesh("assets/meshes/telescope_col.amsh", arena, hull) == MeshParseError::Ok);
    CHECK(render.submeshes.size() >= 3);
    CHECK(!hull.indices.empty());

    CHECK(render.bounds.max.y > 18.0f);
    CHECK(render.bounds.max.x - render.bounds.min.x > 20.0f);
    CHECK(hull.bounds.min.x < -5.0f && hull.bounds.max.x > 5.0f);
    CHECK(hull.bounds.min.z < -5.0f && hull.bounds.max.z > 5.0f);
    CHECK(hull.bounds.max.y < render.bounds.max.y);
}

TEST(mesh, missing_file_reports_read_failure)
{
    Arena arena(megabytes(1));
    MeshData mesh;
    CHECK(load_mesh("assets/meshes/nope.amsh", arena, mesh) == MeshParseError::ReadFailed);
}

TEST(image, decodes_the_zone_heightmap)
{
    Arena arena(megabytes(64));
    Arena scratch(megabytes(64));
    const Image16 height = load_image_gray16(arena, scratch,
                                             "assets/zones/testzone/heightmap.png");
    CHECK(height.valid());
    CHECK(height.width > 0);
    CHECK(height.height > 0);
}

TEST(image, decodes_the_roadmask)
{
    Arena arena(megabytes(64));
    Arena scratch(megabytes(64));
    const Image8 mask = load_image_gray(arena, scratch, "assets/zones/testzone/roadmask.png");
    CHECK(mask.valid());
    CHECK(mask.width > 0);
    CHECK(mask.height > 0);
}

TEST(image, missing_file_is_invalid)
{
    Arena arena(megabytes(1));
    Arena scratch(megabytes(1));
    CHECK(!load_image_gray(arena, scratch, "assets/textures/nope.png").valid());
}

TEST(image, scratch_arena_is_released_after_decode)
{
    Arena arena(megabytes(64));
    Arena scratch(megabytes(64));
    const u64 before = scratch.used();
    const Image8 mask = load_image_gray(arena, scratch, "assets/zones/testzone/roadmask.png");
    CHECK(mask.valid());
    CHECK(scratch.used() == before);
}

TEST(watcher, fires_only_when_mtime_changes)
{
    FileWatcher watcher;
    watcher.set_stat_fn(&fake_stat);
    watcher.set_interval(0.0);

    WatchCounter counter;
    g_fake_mtime = 100;
    const u32 id = watcher.add("some/file.txt", &on_change, &counter);
    CHECK(id != FileWatcher::kInvalidId);
    CHECK(watcher.count() == 1);

    CHECK(watcher.check_all() == 0);
    CHECK(counter.hits == 0);

    g_fake_mtime = 200;
    CHECK(watcher.check_all() == 1);
    CHECK(counter.hits == 1);
    CHECK(counter.last == "some/file.txt");

    CHECK(watcher.check_all() == 0);
    CHECK(counter.hits == 1);
}

TEST(watcher, respects_the_poll_interval)
{
    FileWatcher watcher;
    watcher.set_stat_fn(&fake_stat);
    watcher.set_interval(1.0);

    WatchCounter counter;
    g_fake_mtime = 1;
    watcher.add("a.txt", &on_change, &counter);

    g_fake_mtime = 2;
    CHECK(watcher.poll(0.0) == 1);

    g_fake_mtime = 3;
    CHECK(watcher.poll(0.5) == 0);
    CHECK(counter.hits == 1);

    CHECK(watcher.poll(1.5) == 1);
    CHECK(counter.hits == 2);
}

TEST(watcher, remove_stops_callbacks)
{
    FileWatcher watcher;
    watcher.set_stat_fn(&fake_stat);
    watcher.set_interval(0.0);

    WatchCounter counter;
    g_fake_mtime = 1;
    const u32 id = watcher.add("b.txt", &on_change, &counter);
    CHECK(watcher.count() == 1);

    watcher.remove(id);
    CHECK(watcher.count() == 0);

    g_fake_mtime = 2;
    CHECK(watcher.check_all() == 0);
    CHECK(counter.hits == 0);
}

TEST(watcher, tracks_many_files_independently)
{
    FileWatcher watcher;
    watcher.set_stat_fn(&fake_stat);
    watcher.set_interval(0.0);

    WatchCounter a;
    WatchCounter b;
    g_fake_mtime = 1;
    watcher.add("a.txt", &on_change, &a);
    watcher.add("b.txt", &on_change, &b);
    CHECK(watcher.count() == 2);

    g_fake_mtime = 2;
    CHECK(watcher.check_all() == 2);
    CHECK(a.hits == 1);
    CHECK(b.hits == 1);
}

TEST(watcher, uses_real_mtime_by_default)
{
    FileWatcher watcher;
    watcher.set_interval(0.0);
    WatchCounter counter;
    CHECK(watcher.add("CMakeLists.txt", &on_change, &counter) != FileWatcher::kInvalidId);
    CHECK(watcher.check_all() == 0);
}
