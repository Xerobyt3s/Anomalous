#include "editor/brush.h"
#include "assets/amsh.h"
#include "core/arena.h"
#include "core/log.h"
#include "platform/filesystem.h"

#include <cstdio>
#include <cstring>

namespace anom {
namespace {

constexpr u32 kMaxFragments = 256;

void bake_emit(BakeOutput& out, const BrushPoly& poly)
{
    if (poly.n >= 3 && out.count < out.cap) {
        out.polys[out.count++] = poly;
    }
}

u32 clip_out_volume(const BrushPoly& poly, const Vec3* plane_n, const f32* plane_d,
                    u32 plane_count, BrushPoly* out, u32 out_cap)
{
    BrushPoly current = poly;
    u32 emitted = 0;
    for (u32 p = 0; p < plane_count; p++) {
        BrushPoly outside;
        if (bpoly_clip(current, plane_n[p], plane_d[p], 1.0f, outside) && emitted < out_cap) {
            out[emitted++] = outside;
        }
        BrushPoly inside;
        if (!bpoly_clip(current, plane_n[p], plane_d[p], -1.0f, inside)) {
            return emitted;
        }
        current = inside;
    }
    return emitted;
}

void face_vs_subtracts(BakeOutput& out, Arena& scratch, const BrushPoly& face, u32 skip_index,
                       const Brush* brushes, u32 count)
{
    ArenaScope scope(scratch);
    BrushPoly* frags = scratch.push_array<BrushPoly>(kMaxFragments);
    BrushPoly* next = scratch.push_array<BrushPoly>(kMaxFragments);
    if (!frags || !next) {
        return;
    }

    u32 frag_count = 1;
    frags[0] = face;
    for (u32 s = 0; s < count && frag_count; s++) {
        const Brush& sub = brushes[s];
        if (!sub.subtract || s == skip_index) {
            continue;
        }
        Vec3 plane_n[kBrushMaxFaces];
        f32 plane_d[kBrushMaxFaces];
        const u32 plane_count = brush_planes(sub, plane_n, plane_d);

        u32 next_count = 0;
        for (u32 f = 0; f < frag_count; f++) {
            next_count += clip_out_volume(frags[f], plane_n, plane_d, plane_count,
                                          next + next_count, kMaxFragments - next_count);
        }
        BrushPoly* swap = frags;
        frags = next;
        next = swap;
        frag_count = next_count;
    }

    for (u32 f = 0; f < frag_count; f++) {
        bake_emit(out, frags[f]);
    }
}

void face_uv(Vec3 normal, Vec3 p, f32& out_u, f32& out_v)
{
    const f32 ax = f_abs(normal.x);
    const f32 ay = f_abs(normal.y);
    const f32 az = f_abs(normal.z);
    if (ax >= ay && ax >= az) {
        out_u = p.z * 0.5f;
        out_v = p.y * 0.5f;
    } else if (ay >= ax && ay >= az) {
        out_u = p.x * 0.5f;
        out_v = p.z * 0.5f;
    } else {
        out_u = p.x * 0.5f;
        out_v = p.y * 0.5f;
    }
}

} // namespace

f32 poly_area2(std::span<const Vec2> points)
{
    const u32 n = static_cast<u32>(points.size());
    f32 sum = 0.0f;
    for (u32 i = 0; i < n; i++) {
        const Vec2 a = points[i];
        const Vec2 b = points[(i + 1) % n];
        sum += a.x * b.y - b.x * a.y;
    }
    return sum;
}

bool poly_convex(std::span<const Vec2> points)
{
    const u32 n = static_cast<u32>(points.size());
    f32 sign = 0.0f;
    for (u32 i = 0; i < n; i++) {
        const Vec2 a = points[i];
        const Vec2 b = points[(i + 1) % n];
        const Vec2 c = points[(i + 2) % n];
        const f32 cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
        if (f_abs(cross) < 1e-5f) {
            continue;
        }
        if (sign == 0.0f) {
            sign = cross;
        } else if (sign * cross < 0.0f) {
            return false;
        }
    }
    return true;
}

void poly_normalize(Brush& brush)
{
    const u32 n = brush.point_count;
    if (n < 3) {
        return;
    }
    if (poly_area2({brush.points, n}) < 0.0f) {
        for (u32 i = 0; i < n / 2; i++) {
            const Vec2 tmp = brush.points[i];
            brush.points[i] = brush.points[n - 1 - i];
            brush.points[n - 1 - i] = tmp;
        }
    }

    f32 min_x = 1e30f;
    f32 max_x = -1e30f;
    f32 min_z = 1e30f;
    f32 max_z = -1e30f;
    for (u32 i = 0; i < n; i++) {
        min_x = f_min(min_x, brush.points[i].x);
        max_x = f_max(max_x, brush.points[i].x);
        min_z = f_min(min_z, brush.points[i].y);
        max_z = f_max(max_z, brush.points[i].y);
    }
    const f32 cx = (min_x + max_x) * 0.5f;
    const f32 cz = (min_z + max_z) * 0.5f;
    for (u32 i = 0; i < n; i++) {
        brush.points[i].x -= cx;
        brush.points[i].y -= cz;
    }

    const f32 c = std::cos(brush.yaw);
    const f32 s = std::sin(brush.yaw);
    brush.pos.x += cx * c + cz * s;
    brush.pos.z += -cx * s + cz * c;
    brush.size.x = max_x - min_x;
    brush.size.z = max_z - min_z;
}

void poly_from_box(Brush& brush)
{
    const f32 hx = brush.size.x * 0.5f;
    const f32 hz = brush.size.z * 0.5f;
    brush.kind = BrushKind::Poly;
    brush.point_count = 4;
    brush.points[0] = Vec2{-hx, -hz};
    brush.points[1] = Vec2{hx, -hz};
    brush.points[2] = Vec2{hx, hz};
    brush.points[3] = Vec2{-hx, hz};
    brush.hollow = 0.0f;
}

u32 brush_edit_points(const Brush& brush, Vec2* out)
{
    if (brush.kind == BrushKind::Poly) {
        for (u32 i = 0; i < brush.point_count; i++) {
            out[i] = brush.points[i];
        }
        return brush.point_count;
    }
    if (brush.kind == BrushKind::Box) {
        const f32 hx = brush.size.x * 0.5f;
        const f32 hz = brush.size.z * 0.5f;
        out[0] = Vec2{-hx, -hz};
        out[1] = Vec2{hx, -hz};
        out[2] = Vec2{hx, hz};
        out[3] = Vec2{-hx, hz};
        return 4;
    }
    return 0;
}

u32 brush_faces(const Brush& brush, BrushPoly* out)
{
    const Vec3 h = brush.size * 0.5f;
    const f32 x = h.x;
    const f32 y = h.y;
    const f32 z = h.z;
    u32 count = 0;

    if (brush.kind == BrushKind::Poly && brush.point_count >= 3) {
        const u32 pc = brush.point_count;
        out[count].n = pc;
        for (u32 i = 0; i < pc; i++) {
            out[count].v[i] = Vec3{brush.points[i].x, -y, brush.points[i].y};
        }
        count++;

        out[count].n = pc;
        for (u32 i = 0; i < pc; i++) {
            const Vec2 p = brush.points[pc - 1 - i];
            out[count].v[i] = Vec3{p.x, y, p.y};
        }
        count++;

        for (u32 i = 0; i < pc; i++) {
            const Vec2 a = brush.points[i];
            const Vec2 b = brush.points[(i + 1) % pc];
            out[count].n = 4;
            out[count].v[0] = Vec3{a.x, -y, a.y};
            out[count].v[1] = Vec3{a.x, y, a.y};
            out[count].v[2] = Vec3{b.x, y, b.y};
            out[count].v[3] = Vec3{b.x, -y, b.y};
            count++;
        }
    } else if (brush.kind == BrushKind::Box) {
        const Vec3 quads[6][4] = {
            {{x, -y, -z}, {x, y, -z}, {x, y, z}, {x, -y, z}},
            {{-x, -y, -z}, {-x, -y, z}, {-x, y, z}, {-x, y, -z}},
            {{-x, y, -z}, {-x, y, z}, {x, y, z}, {x, y, -z}},
            {{-x, -y, -z}, {x, -y, -z}, {x, -y, z}, {-x, -y, z}},
            {{-x, -y, z}, {x, -y, z}, {x, y, z}, {-x, y, z}},
            {{-x, -y, -z}, {-x, y, -z}, {x, y, -z}, {x, -y, -z}},
        };
        for (const auto& quad : quads) {
            out[count].n = 4;
            for (u32 i = 0; i < 4; i++) {
                out[count].v[i] = quad[i];
            }
            count++;
        }
    } else {
        out[count].n = 4;
        out[count].v[0] = Vec3{-x, -y, -z};
        out[count].v[1] = Vec3{x, -y, -z};
        out[count].v[2] = Vec3{x, -y, z};
        out[count].v[3] = Vec3{-x, -y, z};
        count++;

        out[count].n = 4;
        out[count].v[0] = Vec3{-x, -y, z};
        out[count].v[1] = Vec3{x, -y, z};
        out[count].v[2] = Vec3{x, y, z};
        out[count].v[3] = Vec3{-x, y, z};
        count++;

        out[count].n = 4;
        out[count].v[0] = Vec3{-x, -y, -z};
        out[count].v[1] = Vec3{-x, y, z};
        out[count].v[2] = Vec3{x, y, z};
        out[count].v[3] = Vec3{x, -y, -z};
        count++;

        out[count].n = 3;
        out[count].v[0] = Vec3{-x, -y, -z};
        out[count].v[1] = Vec3{-x, -y, z};
        out[count].v[2] = Vec3{-x, y, z};
        count++;

        out[count].n = 3;
        out[count].v[0] = Vec3{x, -y, -z};
        out[count].v[1] = Vec3{x, y, z};
        out[count].v[2] = Vec3{x, -y, z};
        count++;
    }

    const Quat rot = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, brush.yaw);
    for (u32 f = 0; f < count; f++) {
        for (u32 i = 0; i < out[f].n; i++) {
            out[f].v[i] = brush.pos + rotate(rot, out[f].v[i]);
        }
        const Vec3 e1 = out[f].v[1] - out[f].v[0];
        const Vec3 e2 = out[f].v[2] - out[f].v[0];
        out[f].normal = normalize(cross(e1, e2));
        out[f].material = brush.material;
    }
    return count;
}

u32 brush_planes(const Brush& brush, Vec3* out_normal, f32* out_dist)
{
    BrushPoly faces[kBrushMaxFaces];
    const u32 count = brush_faces(brush, faces);
    for (u32 f = 0; f < count; f++) {
        out_normal[f] = faces[f].normal;
        out_dist[f] = dot(faces[f].normal, faces[f].v[0]);
    }
    return count;
}

bool bpoly_clip(const BrushPoly& in, Vec3 plane_n, f32 plane_d, f32 keep_sign, BrushPoly& out)
{
    out.n = 0;
    out.normal = in.normal;
    out.material = in.material;

    for (u32 i = 0; i < in.n; i++) {
        const Vec3 a = in.v[i];
        const Vec3 b = in.v[(i + 1) % in.n];
        const f32 da = keep_sign * (dot(plane_n, a) - plane_d);
        const f32 db = keep_sign * (dot(plane_n, b) - plane_d);

        if (da >= -kBrushEps && out.n < kBPolyMax) {
            out.v[out.n++] = a;
        }
        if ((da >= -kBrushEps) != (db >= -kBrushEps) && out.n < kBPolyMax) {
            out.v[out.n++] = lerp(a, b, da / (da - db));
        }
    }
    return out.n >= 3;
}

u32 bake_expand_hollow(const Structure& st, Brush* out)
{
    u32 n = 0;
    for (u32 i = 0; i < st.count; i++) {
        out[n++] = st.brushes[i];
    }
    for (u32 i = 0; i < st.count; i++) {
        const Brush& b = st.brushes[i];
        if (b.kind != BrushKind::Box || b.subtract || b.hollow <= 0.0f) {
            continue;
        }
        const Vec3 inner = b.size - Vec3{1.0f, 1.0f, 1.0f} * (b.hollow * 2.0f);
        if (inner.x < 0.05f || inner.y < 0.05f || inner.z < 0.05f) {
            continue;
        }
        Brush& cavity = out[n++];
        cavity = b;
        cavity.subtract = true;
        cavity.size = inner;
        cavity.hollow = 0.0f;
    }
    return n;
}

void bake_structure(BakeOutput& out, Arena& scratch, const Brush* brushes, u32 count)
{
    for (u32 a = 0; a < count; a++) {
        const Brush& add = brushes[a];
        if (add.subtract) {
            continue;
        }
        BrushPoly faces[kBrushMaxFaces];
        const u32 face_count = brush_faces(add, faces);
        for (u32 f = 0; f < face_count; f++) {
            face_vs_subtracts(out, scratch, faces[f], 0xFFFFFFFFu, brushes, count);
        }
    }

    for (u32 s = 0; s < count; s++) {
        const Brush& sub = brushes[s];
        if (!sub.subtract) {
            continue;
        }
        BrushPoly faces[kBrushMaxFaces];
        const u32 face_count = brush_faces(sub, faces);
        for (u32 f = 0; f < face_count; f++) {
            for (u32 a = 0; a < count; a++) {
                const Brush& add = brushes[a];
                if (add.subtract) {
                    continue;
                }
                Vec3 plane_n[kBrushMaxFaces];
                f32 plane_d[kBrushMaxFaces];
                const u32 plane_count = brush_planes(add, plane_n, plane_d);

                BrushPoly piece = faces[f];
                bool alive = true;
                for (u32 p = 0; p < plane_count && alive; p++) {
                    BrushPoly clipped;
                    alive = bpoly_clip(piece, plane_n[p], plane_d[p], -1.0f, clipped);
                    piece = clipped;
                }
                if (!alive) {
                    continue;
                }

                BrushPoly flipped;
                flipped.n = piece.n;
                flipped.normal = -piece.normal;
                flipped.material = piece.material;
                for (u32 i = 0; i < piece.n; i++) {
                    flipped.v[i] = piece.v[piece.n - 1 - i];
                }
                face_vs_subtracts(out, scratch, flipped, s, brushes, count);
            }
        }
    }
}

bool bake_write_amsh(std::string_view path, Arena& scratch, const BakeOutput& out)
{
    FixedString<32> materials[kAmshMaxSubmeshes];
    u32 material_count = 0;
    for (u32 i = 0; i < out.count; i++) {
        bool found = false;
        for (u32 m = 0; m < material_count; m++) {
            if (materials[m] == out.polys[i].material) {
                found = true;
                break;
            }
        }
        if (found) {
            continue;
        }
        if (material_count >= kAmshMaxSubmeshes) {
            log_warn("bake: more than %u materials", kAmshMaxSubmeshes);
            return false;
        }
        materials[material_count++] = out.polys[i].material;
    }
    if (!material_count) {
        return false;
    }

    u32 vertex_count = 0;
    u32 index_count = 0;
    for (u32 i = 0; i < out.count; i++) {
        vertex_count += out.polys[i].n;
        index_count += (out.polys[i].n - 2) * 3;
    }

    ArenaScope scope(scratch);
    AmshVertex* verts = scratch.push_array<AmshVertex>(vertex_count);
    u32* indices = scratch.push_array<u32>(index_count);
    AmshSubmesh* subs = scratch.push_array<AmshSubmesh>(material_count);
    if (!verts || !indices || !subs) {
        return false;
    }

    u32 vi = 0;
    u32 ii = 0;
    for (u32 m = 0; m < material_count; m++) {
        AmshSubmesh& sub = subs[m];
        std::memset(&sub, 0, sizeof(sub));
        sub.first_index = ii;
        std::snprintf(sub.material, sizeof(sub.material), "%s", materials[m].c_str());

        for (u32 i = 0; i < out.count; i++) {
            const BrushPoly& p = out.polys[i];
            if (p.material != materials[m]) {
                continue;
            }
            const u32 base = vi;
            for (u32 k = 0; k < p.n; k++) {
                AmshVertex& v = verts[vi++];
                v.pos[0] = p.v[k].x;
                v.pos[1] = p.v[k].y;
                v.pos[2] = p.v[k].z;
                v.normal[0] = p.normal.x;
                v.normal[1] = p.normal.y;
                v.normal[2] = p.normal.z;
                face_uv(p.normal, p.v[k], v.uv[0], v.uv[1]);
            }
            for (u32 k = 2; k < p.n; k++) {
                indices[ii++] = base;
                indices[ii++] = base + k - 1;
                indices[ii++] = base + k;
            }
        }
        sub.index_count = ii - sub.first_index;
    }

    std::FILE* file = fs::open(path, "wb");
    if (!file) {
        log_warn("bake: could not write %.*s", static_cast<int>(path.size()), path.data());
        return false;
    }

    AmshHeader header;
    header.magic = kAmshMagic;
    header.version = kAmshVersion;
    header.vertex_count = vertex_count;
    header.index_count = index_count;
    header.submesh_count = material_count;
    std::fwrite(&header, sizeof(header), 1, file);
    std::fwrite(verts, sizeof(AmshVertex), vertex_count, file);
    std::fwrite(indices, sizeof(u32), index_count, file);
    std::fwrite(subs, sizeof(AmshSubmesh), material_count, file);
    std::fclose(file);

    log_info("bake: wrote %.*s | %u verts %u tris %u submeshes",
             static_cast<int>(path.size()), path.data(), vertex_count, index_count / 3,
             material_count);
    return true;
}

} // namespace anom
