#include "test.h"

#include "assets/amsh.h"
#include "assets/mesh_data.h"
#include "core/arena.h"
#include "editor/brush.h"
#include "editor/editor.h"
#include "editor/editor_brush.h"
#include "platform/filesystem.h"

#include <cstdlib>

using namespace anom;

namespace {

Brush make_box(Vec3 pos, Vec3 size, bool subtract = false)
{
    Brush b;
    b.kind = BrushKind::Box;
    b.pos = pos;
    b.size = size;
    b.subtract = subtract;
    b.material.assign("concrete");
    return b;
}

struct Bake {
    Arena arena{megabytes(64)};
    BrushPoly* polys = nullptr;
    BakeOutput out;

    Bake()
    {
        polys = arena.push_array<BrushPoly>(kBakeMaxPolys);
        out.polys = polys;
        out.cap = kBakeMaxPolys;
        out.count = 0;
    }

    void run(const Structure& st)
    {
        Brush expanded[kStructMaxBrushes * 2];
        const u32 count = bake_expand_hollow(st, expanded);
        out.count = 0;
        bake_structure(out, arena, expanded, count);
    }

    Aabb bounds() const
    {
        Aabb box = aabb_empty();
        for (u32 i = 0; i < out.count; i++) {
            for (u32 k = 0; k < out.polys[i].n; k++) {
                box = expand(box, out.polys[i].v[k]);
            }
        }
        return box;
    }

    f32 area() const
    {
        f32 total = 0.0f;
        for (u32 i = 0; i < out.count; i++) {
            const BrushPoly& p = out.polys[i];
            for (u32 k = 2; k < p.n; k++) {
                total += 0.5f * length(cross(p.v[k - 1] - p.v[0], p.v[k] - p.v[0]));
            }
        }
        return total;
    }
};

FixedString<256> temp_path(const char* name)
{
    char* root = nullptr;
    std::size_t len = 0;
    _dupenv_s(&root, &len, "TEMP");
    FixedString<256> path;
    path.format("%s/anomalous_%s.amsh", root ? root : ".", name);
    std::free(root);
    return path;
}

} // namespace

TEST(brush, a_box_bakes_to_six_quads)
{
    Structure st;
    st.brushes[st.count++] = make_box(Vec3{}, Vec3{2.0f, 2.0f, 2.0f});

    Bake bake;
    bake.run(st);

    CHECK(bake.out.count == 6);
    for (u32 i = 0; i < bake.out.count; i++) {
        CHECK(bake.out.polys[i].n == 4);
    }
    CHECK_NEAR(bake.area(), 24.0f, 1e-3);
}

TEST(brush, box_faces_point_outward)
{
    Brush b = make_box(Vec3{3.0f, 1.0f, -2.0f}, Vec3{2.0f, 4.0f, 6.0f});
    BrushPoly faces[kBrushMaxFaces];
    const u32 count = brush_faces(b, faces);

    CHECK(count == 6);
    for (u32 f = 0; f < count; f++) {
        Vec3 centroid{};
        for (u32 i = 0; i < faces[f].n; i++) {
            centroid += faces[f].v[i];
        }
        centroid *= 1.0f / static_cast<f32>(faces[f].n);
        CHECK(dot(faces[f].normal, centroid - b.pos) > 0.0f);
    }
}

TEST(brush, a_wedge_has_five_faces)
{
    Brush b = make_box(Vec3{}, Vec3{2.0f, 2.0f, 2.0f});
    b.kind = BrushKind::Wedge;

    BrushPoly faces[kBrushMaxFaces];
    CHECK(brush_faces(b, faces) == 5);
}

TEST(brush, a_subtraction_carves_a_notch)
{
    Structure solid;
    solid.brushes[solid.count++] = make_box(Vec3{}, Vec3{4.0f, 4.0f, 4.0f});

    Bake full;
    full.run(solid);
    const f32 solid_area = full.area();

    Structure carved;
    carved.brushes[carved.count++] = make_box(Vec3{}, Vec3{4.0f, 4.0f, 4.0f});
    carved.brushes[carved.count++] = make_box(Vec3{2.0f, 0.0f, 0.0f}, Vec3{2.0f, 2.0f, 2.0f},
                                              true);

    Bake notched;
    notched.run(carved);

    CHECK(notched.out.count > full.out.count);
    CHECK(notched.area() > solid_area * 0.9f);

    const Aabb box = notched.bounds();
    CHECK_NEAR(box.min.x, -2.0f, 1e-3);
    CHECK_NEAR(box.max.x, 2.0f, 1e-3);
}

TEST(brush, a_subtraction_outside_the_solid_changes_nothing)
{
    Structure a;
    a.brushes[a.count++] = make_box(Vec3{}, Vec3{2.0f, 2.0f, 2.0f});

    Structure b;
    b.brushes[b.count++] = make_box(Vec3{}, Vec3{2.0f, 2.0f, 2.0f});
    b.brushes[b.count++] = make_box(Vec3{50.0f, 0.0f, 0.0f}, Vec3{2.0f, 2.0f, 2.0f}, true);

    Bake plain;
    Bake with_far_cut;
    plain.run(a);
    with_far_cut.run(b);

    CHECK(plain.out.count == with_far_cut.out.count);
    CHECK_NEAR(plain.area(), with_far_cut.area(), 1e-3);
}

TEST(brush, hollow_expands_into_a_cavity_brush)
{
    Structure st;
    Brush& b = st.brushes[st.count++];
    b = make_box(Vec3{}, Vec3{4.0f, 4.0f, 4.0f});
    b.hollow = 0.5f;

    Brush expanded[kStructMaxBrushes * 2];
    const u32 count = bake_expand_hollow(st, expanded);

    CHECK(count == 2);
    CHECK(!expanded[0].subtract);
    CHECK(expanded[1].subtract);
    CHECK_NEAR(expanded[1].size.x, 3.0f, 1e-5);
    CHECK_NEAR(expanded[1].size.y, 3.0f, 1e-5);
    CHECK(expanded[1].hollow == 0.0f);
}

TEST(brush, a_hollow_box_keeps_its_inner_shell)
{
    Structure st;
    Brush& b = st.brushes[st.count++];
    b = make_box(Vec3{}, Vec3{4.0f, 4.0f, 4.0f});
    b.hollow = 0.5f;

    Bake bake;
    bake.run(st);

    CHECK(bake.out.count > 6);
    CHECK(bake.area() > 6.0f * 16.0f);
}

TEST(brush, a_hollow_thicker_than_the_box_is_ignored)
{
    Structure st;
    Brush& b = st.brushes[st.count++];
    b = make_box(Vec3{}, Vec3{1.0f, 1.0f, 1.0f});
    b.hollow = 2.0f;

    Brush expanded[kStructMaxBrushes * 2];
    CHECK(bake_expand_hollow(st, expanded) == 1);
}

TEST(brush, clipping_a_quad_in_half_keeps_four_corners)
{
    BrushPoly quad;
    quad.n = 4;
    quad.v[0] = Vec3{-1.0f, 0.0f, -1.0f};
    quad.v[1] = Vec3{1.0f, 0.0f, -1.0f};
    quad.v[2] = Vec3{1.0f, 0.0f, 1.0f};
    quad.v[3] = Vec3{-1.0f, 0.0f, 1.0f};
    quad.normal = Vec3{0.0f, 1.0f, 0.0f};

    BrushPoly out;
    CHECK(bpoly_clip(quad, Vec3{1.0f, 0.0f, 0.0f}, 0.0f, 1.0f, out));
    CHECK(out.n == 4);

    f32 min_x = 1e30f;
    for (u32 i = 0; i < out.n; i++) {
        min_x = f_min(min_x, out.v[i].x);
    }
    CHECK_NEAR(min_x, 0.0f, 1e-4);
}

TEST(brush, clipping_a_quad_entirely_away_fails)
{
    BrushPoly quad;
    quad.n = 4;
    quad.v[0] = Vec3{-1.0f, 0.0f, -1.0f};
    quad.v[1] = Vec3{1.0f, 0.0f, -1.0f};
    quad.v[2] = Vec3{1.0f, 0.0f, 1.0f};
    quad.v[3] = Vec3{-1.0f, 0.0f, 1.0f};
    quad.normal = Vec3{0.0f, 1.0f, 0.0f};

    BrushPoly out;
    CHECK(!bpoly_clip(quad, Vec3{1.0f, 0.0f, 0.0f}, 5.0f, 1.0f, out));
}

TEST(brush, polygon_winding_is_normalised_and_recentred)
{
    Brush b;
    b.kind = BrushKind::Poly;
    b.pos = Vec3{10.0f, 0.0f, 10.0f};
    b.point_count = 4;
    b.points[0] = Vec2{2.0f, 2.0f};
    b.points[1] = Vec2{2.0f, 6.0f};
    b.points[2] = Vec2{8.0f, 6.0f};
    b.points[3] = Vec2{8.0f, 2.0f};
    CHECK(poly_area2({b.points, b.point_count}) < 0.0f);

    poly_normalize(b);

    CHECK(poly_area2({b.points, b.point_count}) > 0.0f);
    CHECK_NEAR(b.size.x, 6.0f, 1e-4);
    CHECK_NEAR(b.size.z, 4.0f, 1e-4);
    CHECK_NEAR(b.pos.x, 15.0f, 1e-4);
    CHECK_NEAR(b.pos.z, 14.0f, 1e-4);

    f32 cx = 0.0f;
    for (u32 i = 0; i < b.point_count; i++) {
        cx += b.points[i].x;
    }
    CHECK_NEAR(cx, 0.0f, 1e-4);
}

TEST(brush, convexity_is_detected)
{
    const Vec2 square[4] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
    CHECK(poly_convex(square));

    const Vec2 notched[5] = {{0.0f, 0.0f}, {2.0f, 0.0f}, {1.0f, 1.0f}, {2.0f, 2.0f},
                             {0.0f, 2.0f}};
    CHECK(!poly_convex(notched));
}

TEST(brush, a_baked_structure_reloads_as_a_mesh)
{
    Structure st;
    st.brushes[st.count++] = make_box(Vec3{}, Vec3{4.0f, 3.0f, 2.0f});
    st.brushes[st.count++] = make_box(Vec3{1.5f, 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f}, true);

    Bake bake;
    bake.run(st);

    const FixedString<256> path = temp_path("brush_bake");
    CHECK(bake_write_amsh(path.view(), bake.arena, bake.out));

    Arena reload(megabytes(16));
    MeshData data;
    CHECK(load_mesh(path.view(), reload, data) == MeshParseError::Ok);
    CHECK(data.submeshes.size() == 1);
    CHECK(std::string_view(data.submeshes[0].material) == "concrete");
    CHECK(data.indices.size() % 3 == 0);
    CHECK(data.indices.size() >= 36);
    for (u32 index : data.indices) {
        CHECK(index < data.vertices.size());
    }
}

TEST(brush, two_materials_become_two_submeshes)
{
    Structure st;
    st.brushes[st.count++] = make_box(Vec3{-4.0f, 0.0f, 0.0f}, Vec3{2.0f, 2.0f, 2.0f});
    Brush& second = st.brushes[st.count++];
    second = make_box(Vec3{4.0f, 0.0f, 0.0f}, Vec3{2.0f, 2.0f, 2.0f});
    second.material.assign("metal");

    Bake bake;
    bake.run(st);

    const FixedString<256> path = temp_path("brush_two_mat");
    CHECK(bake_write_amsh(path.view(), bake.arena, bake.out));

    Arena reload(megabytes(16));
    MeshData data;
    CHECK(load_mesh(path.view(), reload, data) == MeshParseError::Ok);
    CHECK(data.submeshes.size() == 2);
}

TEST(brush, baking_nothing_writes_nothing)
{
    Bake bake;
    const FixedString<256> path = temp_path("brush_empty");
    CHECK(!bake_write_amsh(path.view(), bake.arena, bake.out));
}

namespace {

struct Shop {
    Arena arena{megabytes(64)};
    Editor editor;
    BrushEditor brush;

    Shop()
    {
        editor.init(arena);
        brush.init(arena);
    }

    Brush& add(Vec3 pos, Vec3 size, bool subtract = false)
    {
        Brush& b = brush.structure().brushes[brush.structure().count++];
        b = make_box(pos, size, subtract);
        return b;
    }
};

} // namespace

TEST(brush_editor, undo_and_redo_walk_the_structure_history)
{
    Shop shop;
    shop.brush.push_undo(shop.brush.structure());
    shop.add(Vec3{}, Vec3{2.0f, 2.0f, 2.0f});
    CHECK(shop.brush.structure().count == 1);

    shop.brush.undo(shop.editor);
    CHECK(shop.brush.structure().count == 0);
    CHECK(shop.editor.status_text() == "undo");

    shop.brush.redo(shop.editor);
    CHECK(shop.brush.structure().count == 1);
    CHECK(shop.editor.status_text() == "redo");
}

TEST(brush_editor, an_empty_history_is_harmless)
{
    Shop shop;
    shop.brush.undo(shop.editor);
    CHECK(shop.editor.status_text() == "nothing to undo");
    shop.brush.redo(shop.editor);
    CHECK(shop.editor.status_text() == "nothing to redo");
}

TEST(brush_editor, the_history_drops_its_oldest_entry_when_full)
{
    Shop shop;
    for (u32 i = 0; i < kBrushUndoMax + 5; i++) {
        shop.brush.push_undo(shop.brush.structure());
        shop.add(Vec3{static_cast<f32>(i), 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f});
    }
    CHECK(shop.brush.undo_depth() == kBrushUndoMax);

    u32 undone = 0;
    while (shop.brush.undo_depth() > 0) {
        shop.brush.undo(shop.editor);
        undone++;
    }
    CHECK(undone == kBrushUndoMax);
    CHECK(shop.brush.structure().count == 5);
}

TEST(brush_editor, a_new_edit_clears_the_redo_tail)
{
    Shop shop;
    shop.brush.push_undo(shop.brush.structure());
    shop.add(Vec3{}, Vec3{2.0f, 2.0f, 2.0f});
    shop.brush.undo(shop.editor);
    CHECK(shop.brush.redo_depth() == 1);

    shop.brush.push_undo(shop.brush.structure());
    CHECK(shop.brush.redo_depth() == 0);
}

TEST(brush_editor, a_structure_round_trips_through_its_file)
{
    Shop shop;
    shop.brush.structure().name.assign("test_roundtrip");

    Brush& box = shop.add(Vec3{1.0f, 2.0f, 3.0f}, Vec3{4.0f, 5.0f, 6.0f});
    box.yaw = 30.0f * kDegToRad;
    box.hollow = 0.25f;
    box.material.assign("metal");

    Brush& carve = shop.add(Vec3{-1.0f, 0.0f, 0.0f}, Vec3{2.0f, 2.0f, 2.0f}, true);
    carve.material.assign("concrete");

    Brush& wedge = shop.add(Vec3{5.0f, 0.0f, 5.0f}, Vec3{3.0f, 3.0f, 3.0f});
    wedge.kind = BrushKind::Wedge;

    CHECK(shop.brush.save(shop.editor, shop.arena));

    BrushEditor reloaded;
    reloaded.init(shop.arena);
    CHECK(reloaded.load(shop.editor, shop.arena, "test_roundtrip"));

    const Structure& st = reloaded.structure();
    CHECK(st.count == 3);
    CHECK(st.name == "test_roundtrip");

    CHECK(st.brushes[0].kind == BrushKind::Box);
    CHECK(st.brushes[0].material == "metal");
    CHECK_NEAR(st.brushes[0].pos.y, 2.0f, 1e-3);
    CHECK_NEAR(st.brushes[0].size.z, 6.0f, 1e-3);
    CHECK_NEAR(st.brushes[0].yaw, 30.0f * kDegToRad, 1e-3);
    CHECK_NEAR(st.brushes[0].hollow, 0.25f, 1e-3);

    CHECK(st.brushes[1].subtract);
    CHECK(st.brushes[2].kind == BrushKind::Wedge);
}

TEST(brush_editor, a_polygon_brush_round_trips_with_its_points)
{
    Shop shop;
    shop.brush.structure().name.assign("test_poly");

    Brush& b = shop.add(Vec3{2.0f, 1.0f, 2.0f}, Vec3{4.0f, 3.0f, 4.0f});
    poly_from_box(b);
    b.points[2] = Vec2{1.0f, 2.0f};
    poly_normalize(b);
    const u32 expected_points = b.point_count;
    const Vec3 expected_pos = b.pos;

    CHECK(shop.brush.save(shop.editor, shop.arena));

    BrushEditor reloaded;
    reloaded.init(shop.arena);
    CHECK(reloaded.load(shop.editor, shop.arena, "test_poly"));

    const Brush& back = reloaded.structure().brushes[0];
    CHECK(back.kind == BrushKind::Poly);
    CHECK(back.point_count == expected_points);
    CHECK_NEAR(back.pos.x, expected_pos.x, 1e-2);
    CHECK_NEAR(back.pos.z, expected_pos.z, 1e-2);
    CHECK_NEAR(back.size.y, 3.0f, 1e-3);
}

TEST(brush_editor, loading_a_missing_structure_fails_cleanly)
{
    Shop shop;
    CHECK(!shop.brush.load(shop.editor, shop.arena, "definitely_missing_structure"));
    CHECK(shop.editor.status_text() == "struct load failed");
}

TEST(brush_editor, baking_with_only_carve_brushes_is_refused)
{
    Shop shop;
    shop.brush.structure().name.assign("test_carve_only");
    shop.add(Vec3{}, Vec3{2.0f, 2.0f, 2.0f}, true);

    CHECK(!shop.brush.bake(shop.editor, shop.arena));
    CHECK(shop.editor.status_text() == "no solid brushes to bake");
}

TEST(brush_editor, structures_compare_by_value)
{
    Shop a;
    Shop b;
    CHECK(a.brush.structure() == b.brush.structure());

    a.add(Vec3{}, Vec3{2.0f, 2.0f, 2.0f});
    CHECK(a.brush.structure() != b.brush.structure());

    b.add(Vec3{}, Vec3{2.0f, 2.0f, 2.0f});
    CHECK(a.brush.structure() == b.brush.structure());

    b.brush.structure().brushes[0].hollow = 0.5f;
    CHECK(a.brush.structure() != b.brush.structure());
}
