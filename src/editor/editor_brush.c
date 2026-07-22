#include "editor/editor_brush.h"
#include "editor/editor.h"
#include "assets/mesh_format.h"
#include "world/terrain.h"
#include "render/render.h"
#include "render/debug_draw.h"
#include "platform/platform.h"
#include "core/arena.h"
#include "core/config.h"
#include "core/log.h"
#include "ui/ui.h"

#include <stdio.h>
#include <string.h>

#define BPOLY_MAX 24
#define BRUSH_MAX_FACES (BRUSH_POLY_MAX + 2)
#define BAKE_MAX_POLYS 8192
#define BRUSH_EPS 1e-4f
#define STRUCT_DIR "assets/structures"
#define STRUCT_MAX_FILES 64
#define BRUSH_LIST_PAGE 8
#define BRUSH_SIDEBAR_W 276.0f
#define BRUSH_EDGE_PICK_PX 7.0f
#define BRUSH_MIN_SIZE 0.25f

typedef enum BrushView {
    VIEW_TOP,
    VIEW_FRONT,
    VIEW_SIDE,
} BrushView;

typedef struct BPoly {
    Vec3 v[BPOLY_MAX];
    u32 n;
    Vec3 normal;
    const char* material;
} BPoly;

typedef struct BakeOut {
    BPoly* polys;
    u32 count;
    u32 cap;
} BakeOut;

static Structure s_struct;
static char s_files[STRUCT_MAX_FILES][STRUCT_NAME_MAX];
static u32 s_file_count;
static i32 s_file_scroll;
static char s_name_buf[STRUCT_NAME_MAX];
static i32 s_mat_sync = -1;
static char s_mat_buf[32];

#define BRUSH_UNDO_MAX 32

static BrushView s_view;
static f32 s_pan_h;
static f32 s_pan_v;
static f32 s_zoom = 24.0f;
static i32 s_drag;
static f32 s_draw_h0;
static f32 s_draw_v0;
static f32 s_grab_dh;
static f32 s_grab_dv;
static i32 s_rs_h = -1;
static i32 s_rs_v = -1;
static b32 s_vert_mode;
static i32 s_vert_sel = -1;
static b32 s_vert_drag;
static f32 s_view_w;
static Structure s_undo_stack[BRUSH_UNDO_MAX];
static u32 s_undo_count;
static Structure s_redo_stack[BRUSH_UNDO_MAX];
static u32 s_redo_count;
static Structure s_gesture_pre;
static b32 s_gesture_open;

static void brush_push_undo(const Structure* pre)
{
    if (s_undo_count >= BRUSH_UNDO_MAX) {
        memmove(&s_undo_stack[0], &s_undo_stack[1],
                (BRUSH_UNDO_MAX - 1) * sizeof(Structure));
        s_undo_count--;
    }
    s_undo_stack[s_undo_count++] = *pre;
    s_redo_count = 0;
}

static void brush_undo(EditorState* ed)
{
    s_gesture_open = 0;
    if (!s_undo_count) {
        editor_status_msg(ed, "nothing to undo");
        return;
    }
    if (s_redo_count < BRUSH_UNDO_MAX) {
        s_redo_stack[s_redo_count++] = s_struct;
    }
    s_struct = s_undo_stack[--s_undo_count];
    if (ed->brush_sel >= (i32)s_struct.count) {
        ed->brush_sel = -1;
    }
    s_mat_sync = -1;
    snprintf(s_name_buf, sizeof(s_name_buf), "%s", s_struct.name);
    editor_status_msg(ed, "undo");
}

static void brush_redo(EditorState* ed)
{
    s_gesture_open = 0;
    if (!s_redo_count) {
        editor_status_msg(ed, "nothing to redo");
        return;
    }
    if (s_undo_count < BRUSH_UNDO_MAX) {
        s_undo_stack[s_undo_count++] = s_struct;
    }
    s_struct = s_redo_stack[--s_redo_count];
    if (ed->brush_sel >= (i32)s_struct.count) {
        ed->brush_sel = -1;
    }
    s_mat_sync = -1;
    snprintf(s_name_buf, sizeof(s_name_buf), "%s", s_struct.name);
    editor_status_msg(ed, "redo");
}

static void brush_scan_files(void)
{
    PlatformDirEntry entries[STRUCT_MAX_FILES];
    u32 n = platform_list_dir(STRUCT_DIR, entries, STRUCT_MAX_FILES);
    s_file_count = 0;
    for (u32 i = 0; i < n; i++) {
        u64 len = strlen(entries[i].name);
        if (entries[i].is_dir || len < 8 || strcmp(entries[i].name + len - 7, ".struct") != 0) {
            continue;
        }
        if (len - 7 >= STRUCT_NAME_MAX) {
            continue;
        }
        memcpy(s_files[s_file_count], entries[i].name, len - 7);
        s_files[s_file_count][len - 7] = 0;
        s_file_count++;
    }
}

void editor_brush_reset(void)
{
    brush_scan_files();
    s_file_scroll = 0;
    s_mat_sync = -1;
    s_drag = 0;
    if (!s_struct.name[0]) {
        snprintf(s_struct.name, sizeof(s_struct.name), "structure");
    }
    snprintf(s_name_buf, sizeof(s_name_buf), "%s", s_struct.name);
}

static void view_axes(i32* ha, i32* va, f32* vdir)
{
    if (s_view == VIEW_TOP) {
        *ha = 0;
        *va = 2;
        *vdir = 1.0f;
    } else if (s_view == VIEW_FRONT) {
        *ha = 0;
        *va = 1;
        *vdir = -1.0f;
    } else {
        *ha = 2;
        *va = 1;
        *vdir = -1.0f;
    }
}

static f32 vec3_axis(Vec3 v, i32 axis)
{
    return vec3_elem(v, axis);
}

static void vec3_set_axis(Vec3* v, i32 axis, f32 value)
{
    if (axis == 0) {
        v->x = value;
    } else if (axis == 1) {
        v->y = value;
    } else {
        v->z = value;
    }
}

static Vec2 brush_world_to_screen(f32 h, f32 v)
{
    Vec2 vp = r_viewport_size();
    i32 ha, va;
    f32 vdir;
    view_axes(&ha, &va, &vdir);
    Vec2 s;
    s.x = s_view_w * 0.5f + (h - s_pan_h) * s_zoom;
    s.y = vp.y * 0.5f + vdir * (v - s_pan_v) * s_zoom;
    return s;
}

static void brush_screen_to_world(f32 sx, f32 sy, f32* h, f32* v)
{
    Vec2 vp = r_viewport_size();
    i32 ha, va;
    f32 vdir;
    view_axes(&ha, &va, &vdir);
    *h = s_pan_h + (sx - s_view_w * 0.5f) / s_zoom;
    *v = s_pan_v + vdir * (sy - vp.y * 0.5f) / s_zoom;
}

static f32 brush_snap_step(const EditorState* ed)
{
    return ed->snap_pos > 0.0f ? ed->snap_pos : 1.0f;
}

static f32 brush_snap(f32 value, f32 step)
{
    return floorf(value / step + 0.5f) * step;
}

static f32 poly_area2(const Vec2* p, u32 n)
{
    f32 sum = 0.0f;
    for (u32 i = 0; i < n; i++) {
        Vec2 a = p[i];
        Vec2 b = p[(i + 1) % n];
        sum += a.x * b.y - b.x * a.y;
    }
    return sum;
}

static b32 poly_convex(const Vec2* p, u32 n)
{
    f32 sign = 0.0f;
    for (u32 i = 0; i < n; i++) {
        Vec2 a = p[i];
        Vec2 b = p[(i + 1) % n];
        Vec2 c = p[(i + 2) % n];
        f32 cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
        if (f_abs(cross) < 1e-5f) {
            continue;
        }
        if (sign == 0.0f) {
            sign = cross;
        } else if (sign * cross < 0.0f) {
            return 0;
        }
    }
    return 1;
}

static void poly_normalize(Brush* b)
{
    u32 n = b->point_count;
    if (n < 3) {
        return;
    }
    if (poly_area2(b->points, n) < 0.0f) {
        for (u32 i = 0; i < n / 2; i++) {
            Vec2 tmp = b->points[i];
            b->points[i] = b->points[n - 1 - i];
            b->points[n - 1 - i] = tmp;
        }
    }
    f32 min_x = 1e30f, max_x = -1e30f, min_z = 1e30f, max_z = -1e30f;
    for (u32 i = 0; i < n; i++) {
        min_x = f_min(min_x, b->points[i].x);
        max_x = f_max(max_x, b->points[i].x);
        min_z = f_min(min_z, b->points[i].y);
        max_z = f_max(max_z, b->points[i].y);
    }
    f32 cx = (min_x + max_x) * 0.5f;
    f32 cz = (min_z + max_z) * 0.5f;
    for (u32 i = 0; i < n; i++) {
        b->points[i].x -= cx;
        b->points[i].y -= cz;
    }
    f32 c = cosf(b->yaw), s = sinf(b->yaw);
    b->pos.x += cx * c + cz * s;
    b->pos.z += -cx * s + cz * c;
    b->size.x = max_x - min_x;
    b->size.z = max_z - min_z;
}

static void poly_from_box(Brush* b)
{
    f32 hx = b->size.x * 0.5f;
    f32 hz = b->size.z * 0.5f;
    b->kind = BRUSH_POLY;
    b->point_count = 4;
    b->points[0] = v2(-hx, -hz);
    b->points[1] = v2(hx, -hz);
    b->points[2] = v2(hx, hz);
    b->points[3] = v2(-hx, hz);
    b->hollow = 0.0f;
}

static u32 brush_edit_points(const Brush* b, Vec2* out)
{
    if (b->kind == BRUSH_POLY) {
        for (u32 i = 0; i < b->point_count; i++) {
            out[i] = b->points[i];
        }
        return b->point_count;
    }
    if (b->kind == BRUSH_BOX) {
        f32 hx = b->size.x * 0.5f;
        f32 hz = b->size.z * 0.5f;
        out[0] = v2(-hx, -hz);
        out[1] = v2(hx, -hz);
        out[2] = v2(hx, hz);
        out[3] = v2(-hx, hz);
        return 4;
    }
    return 0;
}

static u32 brush_faces(const Brush* b, BPoly* out)
{
    Vec3 h = vec3_scale(b->size, 0.5f);
    f32 x = h.x, y = h.y, z = h.z;
    u32 count = 0;
    if (b->kind == BRUSH_POLY && b->point_count >= 3) {
        u32 pc = b->point_count;
        out[count].n = pc;
        for (u32 i = 0; i < pc; i++) {
            out[count].v[i] = v3(b->points[i].x, -y, b->points[i].y);
        }
        count++;
        out[count].n = pc;
        for (u32 i = 0; i < pc; i++) {
            Vec2 p = b->points[pc - 1 - i];
            out[count].v[i] = v3(p.x, y, p.y);
        }
        count++;
        for (u32 i = 0; i < pc; i++) {
            Vec2 a = b->points[i];
            Vec2 bp = b->points[(i + 1) % pc];
            out[count].n = 4;
            out[count].v[0] = v3(a.x, -y, a.y);
            out[count].v[1] = v3(a.x, y, a.y);
            out[count].v[2] = v3(bp.x, y, bp.y);
            out[count].v[3] = v3(bp.x, -y, bp.y);
            count++;
        }
    } else if (b->kind == BRUSH_BOX) {
        Vec3 quads[6][4] = {
            { { x, -y, -z }, { x, y, -z }, { x, y, z }, { x, -y, z } },
            { { -x, -y, -z }, { -x, -y, z }, { -x, y, z }, { -x, y, -z } },
            { { -x, y, -z }, { -x, y, z }, { x, y, z }, { x, y, -z } },
            { { -x, -y, -z }, { x, -y, -z }, { x, -y, z }, { -x, -y, z } },
            { { -x, -y, z }, { x, -y, z }, { x, y, z }, { -x, y, z } },
            { { -x, -y, -z }, { -x, y, -z }, { x, y, -z }, { x, -y, -z } },
        };
        for (u32 f = 0; f < 6; f++) {
            out[count].n = 4;
            for (u32 i = 0; i < 4; i++) {
                out[count].v[i] = quads[f][i];
            }
            count++;
        }
    } else {
        out[count].n = 4;
        out[count].v[0] = v3(-x, -y, -z);
        out[count].v[1] = v3(x, -y, -z);
        out[count].v[2] = v3(x, -y, z);
        out[count].v[3] = v3(-x, -y, z);
        count++;
        out[count].n = 4;
        out[count].v[0] = v3(-x, -y, z);
        out[count].v[1] = v3(x, -y, z);
        out[count].v[2] = v3(x, y, z);
        out[count].v[3] = v3(-x, y, z);
        count++;
        out[count].n = 4;
        out[count].v[0] = v3(-x, -y, -z);
        out[count].v[1] = v3(-x, y, z);
        out[count].v[2] = v3(x, y, z);
        out[count].v[3] = v3(x, -y, -z);
        count++;
        out[count].n = 3;
        out[count].v[0] = v3(-x, -y, -z);
        out[count].v[1] = v3(-x, -y, z);
        out[count].v[2] = v3(-x, y, z);
        count++;
        out[count].n = 3;
        out[count].v[0] = v3(x, -y, -z);
        out[count].v[1] = v3(x, y, z);
        out[count].v[2] = v3(x, -y, z);
        count++;
    }
    Quat rot = quat_from_axis_angle(v3(0.0f, 1.0f, 0.0f), b->yaw);
    for (u32 f = 0; f < count; f++) {
        for (u32 i = 0; i < out[f].n; i++) {
            out[f].v[i] = vec3_add(b->pos, quat_rotate_vec3(rot, out[f].v[i]));
        }
        Vec3 e1 = vec3_sub(out[f].v[1], out[f].v[0]);
        Vec3 e2 = vec3_sub(out[f].v[2], out[f].v[0]);
        out[f].normal = vec3_normalize(vec3_cross(e1, e2));
        out[f].material = b->material;
    }
    return count;
}

static u32 brush_planes(const Brush* b, Vec3* pn, f32* pd)
{
    BPoly faces[BRUSH_MAX_FACES];
    u32 count = brush_faces(b, faces);
    for (u32 f = 0; f < count; f++) {
        pn[f] = faces[f].normal;
        pd[f] = vec3_dot(faces[f].normal, faces[f].v[0]);
    }
    return count;
}

static b32 bpoly_clip(const BPoly* in, Vec3 pn, f32 pd, f32 keep_sign, BPoly* out)
{
    out->n = 0;
    out->normal = in->normal;
    out->material = in->material;
    for (u32 i = 0; i < in->n; i++) {
        Vec3 a = in->v[i];
        Vec3 b = in->v[(i + 1) % in->n];
        f32 da = keep_sign * (vec3_dot(pn, a) - pd);
        f32 db = keep_sign * (vec3_dot(pn, b) - pd);
        if (da >= -BRUSH_EPS) {
            if (out->n < BPOLY_MAX) {
                out->v[out->n++] = a;
            }
        }
        if ((da >= -BRUSH_EPS) != (db >= -BRUSH_EPS)) {
            f32 t = da / (da - db);
            if (out->n < BPOLY_MAX) {
                out->v[out->n++] = vec3_lerp(a, b, t);
            }
        }
    }
    return out->n >= 3;
}

static void bake_emit(BakeOut* out, const BPoly* p)
{
    if (p->n >= 3 && out->count < out->cap) {
        out->polys[out->count++] = *p;
    }
}

static u32 bake_clip_out_volume(const BPoly* poly, const Vec3* pn, const f32* pd,
                                u32 plane_count, BPoly* out, u32 out_cap)
{
    BPoly cur = *poly;
    u32 emitted = 0;
    for (u32 p = 0; p < plane_count; p++) {
        BPoly outside;
        if (bpoly_clip(&cur, pn[p], pd[p], 1.0f, &outside) && emitted < out_cap) {
            out[emitted++] = outside;
        }
        BPoly inside;
        if (!bpoly_clip(&cur, pn[p], pd[p], -1.0f, &inside)) {
            return emitted;
        }
        cur = inside;
    }
    return emitted;
}

static void bake_face_vs_subtracts(BakeOut* out, const BPoly* face, u32 skip_index,
                                   const Brush* brushes, u32 count)
{
    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    BPoly* frags = arena_push_array(&g_frame_arena, BPoly, 256);
    BPoly* next = arena_push_array(&g_frame_arena, BPoly, 256);
    u32 frag_count = 1;
    frags[0] = *face;
    for (u32 s = 0; s < count && frag_count; s++) {
        const Brush* sub = &brushes[s];
        if (!sub->subtract || s == skip_index) {
            continue;
        }
        Vec3 pn[BRUSH_MAX_FACES];
        f32 pd[BRUSH_MAX_FACES];
        u32 plane_count = brush_planes(sub, pn, pd);
        u32 next_count = 0;
        for (u32 f = 0; f < frag_count; f++) {
            next_count += bake_clip_out_volume(&frags[f], pn, pd, plane_count,
                                               next + next_count, 256 - next_count);
        }
        BPoly* swap = frags;
        frags = next;
        next = swap;
        frag_count = next_count;
    }
    for (u32 f = 0; f < frag_count; f++) {
        bake_emit(out, &frags[f]);
    }
    arena_temp_end(temp);
}

static u32 bake_expand_hollow(const Structure* st, Brush* out)
{
    u32 n = 0;
    for (u32 i = 0; i < st->count; i++) {
        out[n++] = st->brushes[i];
    }
    for (u32 i = 0; i < st->count; i++) {
        const Brush* b = &st->brushes[i];
        if (b->kind != BRUSH_BOX || b->subtract || b->hollow <= 0.0f) {
            continue;
        }
        Vec3 inner = vec3_sub(b->size, vec3_scale(v3(1.0f, 1.0f, 1.0f), b->hollow * 2.0f));
        if (inner.x < 0.05f || inner.y < 0.05f || inner.z < 0.05f) {
            continue;
        }
        Brush* cavity = &out[n++];
        *cavity = *b;
        cavity->subtract = 1;
        cavity->size = inner;
        cavity->hollow = 0.0f;
    }
    return n;
}

static void bake_structure(BakeOut* out, const Brush* brushes, u32 count)
{
    for (u32 a = 0; a < count; a++) {
        const Brush* add = &brushes[a];
        if (add->subtract) {
            continue;
        }
        BPoly faces[BRUSH_MAX_FACES];
        u32 face_count = brush_faces(add, faces);
        for (u32 f = 0; f < face_count; f++) {
            bake_face_vs_subtracts(out, &faces[f], 0xFFFFFFFFu, brushes, count);
        }
    }
    for (u32 s = 0; s < count; s++) {
        const Brush* sub = &brushes[s];
        if (!sub->subtract) {
            continue;
        }
        BPoly faces[BRUSH_MAX_FACES];
        u32 face_count = brush_faces(sub, faces);
        for (u32 f = 0; f < face_count; f++) {
            for (u32 a = 0; a < count; a++) {
                const Brush* add = &brushes[a];
                if (add->subtract) {
                    continue;
                }
                Vec3 pn[BRUSH_MAX_FACES];
                f32 pd[BRUSH_MAX_FACES];
                u32 plane_count = brush_planes(add, pn, pd);
                BPoly piece = faces[f];
                b32 alive = 1;
                for (u32 p = 0; p < plane_count && alive; p++) {
                    BPoly clipped;
                    alive = bpoly_clip(&piece, pn[p], pd[p], -1.0f, &clipped);
                    piece = clipped;
                }
                if (!alive) {
                    continue;
                }
                BPoly flipped;
                flipped.n = piece.n;
                flipped.normal = vec3_negate(piece.normal);
                flipped.material = piece.material;
                for (u32 i = 0; i < piece.n; i++) {
                    flipped.v[i] = piece.v[piece.n - 1 - i];
                }
                bake_face_vs_subtracts(out, &flipped, s, brushes, count);
            }
        }
    }
}

static void bake_face_uv(Vec3 normal, Vec3 p, f32* u, f32* v)
{
    f32 ax = f_abs(normal.x), ay = f_abs(normal.y), az = f_abs(normal.z);
    if (ax >= ay && ax >= az) {
        *u = p.z * 0.5f;
        *v = p.y * 0.5f;
    } else if (ay >= ax && ay >= az) {
        *u = p.x * 0.5f;
        *v = p.z * 0.5f;
    } else {
        *u = p.x * 0.5f;
        *v = p.y * 0.5f;
    }
}

static b32 bake_write_amsh(const char* path, const BakeOut* out)
{
    const char* materials[AMSH_MAX_SUBMESHES];
    u32 material_count = 0;
    for (u32 i = 0; i < out->count; i++) {
        b32 found = 0;
        for (u32 m = 0; m < material_count; m++) {
            if (strcmp(materials[m], out->polys[i].material) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            if (material_count >= AMSH_MAX_SUBMESHES) {
                log_warn("bake: more than %u materials", AMSH_MAX_SUBMESHES);
                return 0;
            }
            materials[material_count++] = out->polys[i].material;
        }
    }
    if (!material_count) {
        return 0;
    }

    u32 vertex_count = 0;
    u32 index_count = 0;
    for (u32 i = 0; i < out->count; i++) {
        vertex_count += out->polys[i].n;
        index_count += (out->polys[i].n - 2) * 3;
    }

    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    AmshVertex* verts = arena_push_array(&g_frame_arena, AmshVertex, vertex_count);
    u32* indices = arena_push_array(&g_frame_arena, u32, index_count);
    AmshSubmesh* subs = arena_push_array(&g_frame_arena, AmshSubmesh, material_count);

    u32 vi = 0;
    u32 ii = 0;
    for (u32 m = 0; m < material_count; m++) {
        AmshSubmesh* sub = &subs[m];
        memset(sub, 0, sizeof(*sub));
        sub->first_index = ii;
        snprintf(sub->material, sizeof(sub->material), "%s", materials[m]);
        for (u32 i = 0; i < out->count; i++) {
            const BPoly* p = &out->polys[i];
            if (strcmp(p->material, materials[m]) != 0) {
                continue;
            }
            u32 base = vi;
            for (u32 k = 0; k < p->n; k++) {
                AmshVertex* v = &verts[vi++];
                v->pos[0] = p->v[k].x;
                v->pos[1] = p->v[k].y;
                v->pos[2] = p->v[k].z;
                v->normal[0] = p->normal.x;
                v->normal[1] = p->normal.y;
                v->normal[2] = p->normal.z;
                bake_face_uv(p->normal, p->v[k], &v->uv[0], &v->uv[1]);
            }
            for (u32 k = 2; k < p->n; k++) {
                indices[ii++] = base;
                indices[ii++] = base + k - 1;
                indices[ii++] = base + k;
            }
        }
        sub->index_count = ii - sub->first_index;
    }

    FILE* file = (FILE*)platform_fopen(path, "wb");
    if (!file) {
        log_warn("bake: could not write %s", path);
        arena_temp_end(temp);
        return 0;
    }
    AmshHeader header;
    header.magic = AMSH_MAGIC;
    header.version = AMSH_VERSION;
    header.vertex_count = vertex_count;
    header.index_count = index_count;
    header.submesh_count = material_count;
    fwrite(&header, sizeof(header), 1, file);
    fwrite(verts, sizeof(AmshVertex), vertex_count, file);
    fwrite(indices, sizeof(u32), index_count, file);
    fwrite(subs, sizeof(AmshSubmesh), material_count, file);
    fclose(file);
    arena_temp_end(temp);
    log_info("bake: wrote %s | %u verts %u tris %u submeshes", path, vertex_count,
             index_count / 3, material_count);
    return 1;
}

static b32 brush_bake(EditorState* ed)
{
    b32 any_add = 0;
    for (u32 i = 0; i < s_struct.count; i++) {
        if (!s_struct.brushes[i].subtract) {
            any_add = 1;
        }
    }
    if (!any_add) {
        editor_status_msg(ed, "no solid brushes to bake");
        return 0;
    }
    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    BakeOut out;
    out.polys = arena_push_array(&g_frame_arena, BPoly, BAKE_MAX_POLYS);
    out.count = 0;
    out.cap = BAKE_MAX_POLYS;
    Brush* expanded = arena_push_array(&g_frame_arena, Brush, STRUCT_MAX_BRUSHES * 2);
    u32 expanded_count = bake_expand_hollow(&s_struct, expanded);
    bake_structure(&out, expanded, expanded_count);
    char path[256];
    snprintf(path, sizeof(path), "assets/meshes/%s.amsh", s_struct.name);
    b32 ok = bake_write_amsh(path, &out);
    arena_temp_end(temp);
    if (ok) {
        editor_scene_reset();
        editor_status_msg(ed, "baked mesh");
    } else {
        editor_status_msg(ed, "bake failed");
    }
    return ok;
}

static b32 brush_save(EditorState* ed)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.struct", STRUCT_DIR, s_struct.name);
    FILE* file = (FILE*)platform_fopen(path, "wb");
    if (!file) {
        editor_status_msg(ed, "struct save failed");
        return 0;
    }
    fprintf(file, "[structure]\n");
    fprintf(file, "name = %s\n", s_struct.name);
    for (u32 i = 0; i < s_struct.count; i++) {
        const Brush* b = &s_struct.brushes[i];
        if (b->kind == BRUSH_POLY) {
            fprintf(file, "brush = poly %s %.3f %.3f %.3f %.3f %.2f %s %u",
                    b->subtract ? "sub" : "add", (f64)b->pos.x, (f64)b->pos.y, (f64)b->pos.z,
                    (f64)b->size.y, (f64)(b->yaw * RAD_TO_DEG), b->material, b->point_count);
            for (u32 k = 0; k < b->point_count; k++) {
                fprintf(file, " %.3f %.3f", (f64)b->points[k].x, (f64)b->points[k].y);
            }
            fprintf(file, "\n");
            continue;
        }
        fprintf(file, "brush = %s %s %.3f %.3f %.3f %.3f %.3f %.3f %.2f %s %.3f\n",
                b->kind == BRUSH_WEDGE ? "wedge" : "box", b->subtract ? "sub" : "add",
                (f64)b->pos.x, (f64)b->pos.y, (f64)b->pos.z, (f64)b->size.x, (f64)b->size.y,
                (f64)b->size.z, (f64)(b->yaw * RAD_TO_DEG), b->material, (f64)b->hollow);
    }
    fclose(file);
    brush_scan_files();
    editor_status_msg(ed, "saved .struct");
    return 1;
}

static b32 brush_load(EditorState* ed, const char* name)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.struct", STRUCT_DIR, name);
    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    FileData file = platform_read_entire_file(&g_frame_arena, path);
    if (!file.data) {
        arena_temp_end(temp);
        editor_status_msg(ed, "struct load failed");
        return 0;
    }
    Config cfg;
    if (!config_parse(&cfg, &g_frame_arena, (const char*)file.data)) {
        arena_temp_end(temp);
        editor_status_msg(ed, "struct parse failed");
        return 0;
    }
    memset(&s_struct, 0, sizeof(s_struct));
    snprintf(s_struct.name, sizeof(s_struct.name), "%s", name);
    for (u32 i = 0; i < cfg.count; i++) {
        if (strcmp(cfg.entries[i].key, "structure.brush") != 0
            || s_struct.count >= STRUCT_MAX_BRUSHES) {
            continue;
        }
        const char* value = cfg.entries[i].value;
        char kind_str[16];
        char mode_str[8];
        Brush* b = &s_struct.brushes[s_struct.count];
        memset(b, 0, sizeof(*b));
        f32 yaw_deg;
        if (sscanf(value, "%15s", kind_str) != 1) {
            continue;
        }
        if (strcmp(kind_str, "poly") == 0) {
            u32 pc = 0;
            i32 off = 0;
            if (sscanf(value, "%15s %7s %f %f %f %f %f %31s %u%n", kind_str, mode_str,
                       &b->pos.x, &b->pos.y, &b->pos.z, &b->size.y, &yaw_deg, b->material,
                       &pc, &off) < 9 || pc < 3) {
                log_warn("struct: malformed poly line: %s", value);
                continue;
            }
            if (pc > BRUSH_POLY_MAX) {
                pc = BRUSH_POLY_MAX;
            }
            b->kind = BRUSH_POLY;
            b->subtract = strcmp(mode_str, "sub") == 0;
            b->yaw = yaw_deg * DEG_TO_RAD;
            b->point_count = 0;
            for (u32 k = 0; k < pc; k++) {
                f32 px, pz;
                i32 adv = 0;
                if (sscanf(value + off, "%f %f%n", &px, &pz, &adv) < 2) {
                    break;
                }
                off += adv;
                b->points[b->point_count++] = v2(px, pz);
            }
            if (b->point_count < 3) {
                log_warn("struct: poly with too few points: %s", value);
                continue;
            }
            poly_normalize(b);
            s_struct.count++;
            continue;
        }
        f32 hollow = 0.0f;
        i32 fields = sscanf(value, "%15s %7s %f %f %f %f %f %f %f %31s %f",
                            kind_str, mode_str, &b->pos.x, &b->pos.y, &b->pos.z, &b->size.x,
                            &b->size.y, &b->size.z, &yaw_deg, b->material, &hollow);
        if (fields < 10) {
            log_warn("struct: malformed brush line: %s", value);
            continue;
        }
        b->kind = strcmp(kind_str, "wedge") == 0 ? BRUSH_WEDGE : BRUSH_BOX;
        b->subtract = strcmp(mode_str, "sub") == 0;
        b->yaw = yaw_deg * DEG_TO_RAD;
        b->hollow = fields >= 11 ? hollow : 0.0f;
        s_struct.count++;
    }
    arena_temp_end(temp);
    snprintf(s_name_buf, sizeof(s_name_buf), "%s", name);
    ed->brush_sel = -1;
    s_mat_sync = -1;
    editor_status_msg(ed, "loaded .struct");
    return 1;
}

static void brush_add(EditorState* ed, BrushKind kind)
{
    if (s_struct.count >= STRUCT_MAX_BRUSHES) {
        editor_status_msg(ed, "brush limit reached");
        return;
    }
    Brush* b = &s_struct.brushes[s_struct.count];
    memset(b, 0, sizeof(*b));
    b->kind = kind;
    b->size = v3(4.0f, 3.0f, 4.0f);
    i32 ha, va;
    f32 vdir;
    view_axes(&ha, &va, &vdir);
    vec3_set_axis(&b->pos, ha, brush_snap(s_pan_h, 1.0f));
    vec3_set_axis(&b->pos, va, brush_snap(s_pan_v, 1.0f));
    if (s_view == VIEW_TOP) {
        b->pos.y = b->size.y * 0.5f;
    }
    snprintf(b->material, sizeof(b->material), "concrete");
    ed->brush_sel = (i32)s_struct.count;
    s_mat_sync = -1;
    s_struct.count++;
}

static void brush_top_local(const Brush* b, f32 h, f32 v, f32* out_lx, f32* out_lz)
{
    f32 c = cosf(b->yaw), s = sinf(b->yaw);
    f32 dx = h - b->pos.x;
    f32 dz = v - b->pos.z;
    *out_lx = dx * c - dz * s;
    *out_lz = dx * s + dz * c;
}

static void brush_top_world(const Brush* b, f32 lx, f32 lz, f32* out_h, f32* out_v)
{
    f32 c = cosf(b->yaw), s = sinf(b->yaw);
    *out_h = b->pos.x + lx * c + lz * s;
    *out_v = b->pos.z - lx * s + lz * c;
}

static b32 brush_view_contains(const Brush* b, f32 h, f32 v)
{
    i32 ha, va;
    f32 vdir;
    view_axes(&ha, &va, &vdir);
    if (s_view == VIEW_TOP && b->kind == BRUSH_POLY && b->point_count >= 3) {
        f32 lx, lz;
        brush_top_local(b, h, v, &lx, &lz);
        for (u32 i = 0; i < b->point_count; i++) {
            Vec2 a = b->points[i];
            Vec2 e = b->points[(i + 1) % b->point_count];
            f32 cross = (e.x - a.x) * (lz - a.y) - (e.y - a.y) * (lx - a.x);
            if (cross < 0.0f) {
                return 0;
            }
        }
        return 1;
    }
    if (s_view == VIEW_TOP && b->yaw != 0.0f) {
        f32 lx, lz;
        brush_top_local(b, h, v, &lx, &lz);
        return f_abs(lx) <= b->size.x * 0.5f && f_abs(lz) <= b->size.z * 0.5f;
    }
    return f_abs(h - vec3_axis(b->pos, ha)) <= vec3_axis(b->size, ha) * 0.5f
        && f_abs(v - vec3_axis(b->pos, va)) <= vec3_axis(b->size, va) * 0.5f;
}

static u32 brush_handle_points(const Brush* b, Vec2* out_pts, i32* out_h, i32* out_v)
{
    i32 ha, va;
    f32 vdir;
    view_axes(&ha, &va, &vdir);
    if (s_view == VIEW_TOP && b->yaw != 0.0f) {
        return 0;
    }
    f32 ch = vec3_axis(b->pos, ha);
    f32 cv = vec3_axis(b->pos, va);
    f32 hh = vec3_axis(b->size, ha) * 0.5f;
    f32 hv = vec3_axis(b->size, va) * 0.5f;
    f32 hs[3] = { ch - hh, ch + hh, ch };
    f32 vs[3] = { cv - hv, cv + hv, cv };
    static const i32 handle_h[8] = { 0, 1, 0, 1, 0, 1, -1, -1 };
    static const i32 handle_v[8] = { 0, 0, 1, 1, -1, -1, 0, 1 };
    for (u32 i = 0; i < 8; i++) {
        f32 h = handle_h[i] < 0 ? hs[2] : hs[handle_h[i]];
        f32 v = handle_v[i] < 0 ? vs[2] : vs[handle_v[i]];
        out_pts[i] = brush_world_to_screen(h, v);
        out_h[i] = handle_h[i];
        out_v[i] = handle_v[i];
    }
    return 8;
}

static b32 brush_handle_hit(const Brush* b, f32 mx, f32 my, i32* out_h, i32* out_v)
{
    Vec2 pts[8];
    i32 hs[8], vs[8];
    u32 n = brush_handle_points(b, pts, hs, vs);
    for (u32 i = 0; i < n; i++) {
        if (f_abs(mx - pts[i].x) < BRUSH_EDGE_PICK_PX
            && f_abs(my - pts[i].y) < BRUSH_EDGE_PICK_PX) {
            *out_h = hs[i];
            *out_v = vs[i];
            return 1;
        }
    }
    return 0;
}

static void brush_delete_selected(EditorState* ed)
{
    if (ed->brush_sel < 0 || ed->brush_sel >= (i32)s_struct.count) {
        return;
    }
    memmove(&s_struct.brushes[ed->brush_sel], &s_struct.brushes[ed->brush_sel + 1],
            (s_struct.count - (u32)ed->brush_sel - 1) * sizeof(Brush));
    s_struct.count--;
    ed->brush_sel = -1;
    s_mat_sync = -1;
}

static void brush_frame_view(void)
{
    Vec2 vp = r_viewport_size();
    i32 ha, va;
    f32 vdir;
    view_axes(&ha, &va, &vdir);
    if (!s_struct.count) {
        s_pan_h = 0.0f;
        s_pan_v = 0.0f;
        s_zoom = 24.0f;
        return;
    }
    f32 h_min = 1e30f, h_max = -1e30f, v_min = 1e30f, v_max = -1e30f;
    for (u32 i = 0; i < s_struct.count; i++) {
        const Brush* b = &s_struct.brushes[i];
        f32 r = vec3_length(vec3_scale(b->size, 0.5f));
        h_min = f_min(h_min, vec3_axis(b->pos, ha) - r);
        h_max = f_max(h_max, vec3_axis(b->pos, ha) + r);
        v_min = f_min(v_min, vec3_axis(b->pos, va) - r);
        v_max = f_max(v_max, vec3_axis(b->pos, va) + r);
    }
    s_pan_h = (h_min + h_max) * 0.5f;
    s_pan_v = (v_min + v_max) * 0.5f;
    f32 span_h = f_max(h_max - h_min, 1.0f);
    f32 span_v = f_max(v_max - v_min, 1.0f);
    s_zoom = f_clamp(f_min(s_view_w / span_h, vp.y / span_v) * 0.8f, 2.0f, 200.0f);
}

static void brush_nudge(EditorState* ed, f32 dh, f32 dv)
{
    if (ed->brush_sel < 0 || ed->brush_sel >= (i32)s_struct.count) {
        return;
    }
    i32 ha, va;
    f32 vdir;
    view_axes(&ha, &va, &vdir);
    brush_push_undo(&s_struct);
    Brush* b = &s_struct.brushes[ed->brush_sel];
    vec3_set_axis(&b->pos, ha, vec3_axis(b->pos, ha) + dh);
    vec3_set_axis(&b->pos, va, vec3_axis(b->pos, va) + dv);
}

void editor_brush_update(EditorState* ed, const struct GameInput* input,
                         const struct Camera* cam, const struct Terrain* terrain)
{
    (void)cam;
    (void)terrain;
    Vec2 vp = r_viewport_size();
    s_view_w = vp.x - BRUSH_SIDEBAR_W - 16.0f;
    b32 typing = ui_text_active();
    b32 over_panel = ui_mouse_over_panel(input);
    f32 step = brush_snap_step(ed);
    b32 ctrl = input->key_down[KEY_LEFT_CONTROL];
    b32 mouse_busy = s_drag != 0 || s_vert_drag || input->mouse_down[MOUSE_LEFT];

    if (ed->brush_sel < 0 || ed->brush_sel >= (i32)s_struct.count) {
        s_vert_sel = -1;
        s_vert_drag = 0;
    } else if (s_vert_sel >= 0) {
        const Brush* vb = &s_struct.brushes[ed->brush_sel];
        u32 pc = vb->kind == BRUSH_POLY ? vb->point_count : (vb->kind == BRUSH_BOX ? 4 : 0);
        if (s_vert_sel >= (i32)pc) {
            s_vert_sel = -1;
        }
    }

    if (!typing) {
        if (input->key_pressed[KEY_1]) {
            s_view = VIEW_TOP;
        }
        if (input->key_pressed[KEY_2]) {
            s_view = VIEW_FRONT;
        }
        if (input->key_pressed[KEY_3]) {
            s_view = VIEW_SIDE;
        }
        if (input->key_pressed[KEY_G]) {
            static const f32 grid_presets[5] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
            i32 next = 0;
            for (i32 i = 0; i < 5; i++) {
                if (f_abs(ed->snap_pos - grid_presets[i]) < 0.01f) {
                    next = (i + 1) % 5;
                    break;
                }
            }
            ed->snap_pos = grid_presets[next];
        }
        if (input->key_pressed[KEY_F]) {
            brush_frame_view();
        }
        if (input->key_pressed[KEY_V]) {
            s_vert_mode = !s_vert_mode;
            s_vert_sel = -1;
            s_vert_drag = 0;
        }
        if (input->key_pressed[KEY_DELETE]
            && ed->brush_sel >= 0 && ed->brush_sel < (i32)s_struct.count) {
            Brush* db = &s_struct.brushes[ed->brush_sel];
            if (s_vert_mode && db->kind == BRUSH_POLY && s_vert_sel >= 0
                && s_vert_sel < (i32)db->point_count && db->point_count > 3) {
                brush_push_undo(&s_struct);
                memmove(&db->points[s_vert_sel], &db->points[s_vert_sel + 1],
                        (db->point_count - (u32)s_vert_sel - 1) * sizeof(Vec2));
                db->point_count--;
                poly_normalize(db);
                s_vert_sel = -1;
                editor_status_msg(ed, "deleted point");
            } else {
                brush_push_undo(&s_struct);
                brush_delete_selected(ed);
                editor_status_msg(ed, "deleted brush");
            }
        }
        if (ctrl && input->key_pressed[KEY_D]
            && ed->brush_sel >= 0 && ed->brush_sel < (i32)s_struct.count
            && s_struct.count < STRUCT_MAX_BRUSHES) {
            brush_push_undo(&s_struct);
            Brush copy = s_struct.brushes[ed->brush_sel];
            i32 ha2, va2;
            f32 vdir2;
            view_axes(&ha2, &va2, &vdir2);
            vec3_set_axis(&copy.pos, ha2, vec3_axis(copy.pos, ha2) + step);
            vec3_set_axis(&copy.pos, va2, vec3_axis(copy.pos, va2) + step);
            s_struct.brushes[s_struct.count] = copy;
            ed->brush_sel = (i32)s_struct.count;
            s_struct.count++;
            s_mat_sync = -1;
        }
        if (ctrl && input->key_pressed[KEY_Z] && !mouse_busy) {
            brush_undo(ed);
        }
        if (ctrl && input->key_pressed[KEY_Y] && !mouse_busy) {
            brush_redo(ed);
        }
        if (input->key_pressed[KEY_LEFT]) {
            brush_nudge(ed, -step, 0.0f);
        }
        if (input->key_pressed[KEY_RIGHT]) {
            brush_nudge(ed, step, 0.0f);
        }
        if (input->key_pressed[KEY_UP]) {
            brush_nudge(ed, 0.0f, step);
        }
        if (input->key_pressed[KEY_DOWN]) {
            brush_nudge(ed, 0.0f, -step);
        }
        if (input->key_pressed[KEY_ESCAPE]) {
            if (s_drag || s_vert_drag) {
                s_drag = 0;
                s_vert_drag = 0;
                s_gesture_open = 0;
            } else if (s_vert_sel >= 0) {
                s_vert_sel = -1;
            } else {
                ed->brush_sel = -1;
            }
        }
    }

    i32 ha, va;
    f32 vdir;
    view_axes(&ha, &va, &vdir);

    if (input->mouse_down[MOUSE_RIGHT]) {
        s_pan_h -= input->mouse_dx / s_zoom;
        s_pan_v -= vdir * input->mouse_dy / s_zoom;
    }
    if (input->scroll_dy != 0.0f && !over_panel) {
        f32 before_h, before_v;
        brush_screen_to_world(input->mouse_x, input->mouse_y, &before_h, &before_v);
        s_zoom = f_clamp(s_zoom * powf(1.2f, input->scroll_dy), 2.0f, 200.0f);
        f32 after_h, after_v;
        brush_screen_to_world(input->mouse_x, input->mouse_y, &after_h, &after_v);
        s_pan_h += before_h - after_h;
        s_pan_v += before_v - after_v;
    }

    f32 wh, wv;
    brush_screen_to_world(input->mouse_x, input->mouse_y, &wh, &wv);
    b32 in_view = input->mouse_x < s_view_w;

    if (input->mouse_pressed[MOUSE_LEFT]) {
        s_gesture_pre = s_struct;
        s_gesture_open = 1;
    }

    b32 vert_consumed = 0;
    if (s_vert_mode && s_view == VIEW_TOP && input->mouse_pressed[MOUSE_LEFT] && !over_panel
        && in_view && !input->mouse_down[MOUSE_RIGHT]
        && ed->brush_sel >= 0 && ed->brush_sel < (i32)s_struct.count) {
        Brush* b = &s_struct.brushes[ed->brush_sel];
        Vec2 local[BRUSH_POLY_MAX];
        u32 pc = brush_edit_points(b, local);
        i32 hit_vert = -1;
        for (u32 i = 0; i < pc; i++) {
            f32 h, v;
            brush_top_world(b, local[i].x, local[i].y, &h, &v);
            Vec2 sp = brush_world_to_screen(h, v);
            if (f_abs(input->mouse_x - sp.x) < BRUSH_EDGE_PICK_PX + 2.0f
                && f_abs(input->mouse_y - sp.y) < BRUSH_EDGE_PICK_PX + 2.0f) {
                hit_vert = (i32)i;
                break;
            }
        }
        if (hit_vert >= 0) {
            if (b->kind == BRUSH_BOX) {
                poly_from_box(b);
            }
            s_vert_sel = hit_vert;
            s_vert_drag = 1;
            vert_consumed = 1;
        } else if (ctrl && pc >= 3 && pc < BRUSH_POLY_MAX) {
            f32 best = BRUSH_EDGE_PICK_PX + 2.0f;
            i32 best_edge = -1;
            f32 best_t = 0.0f;
            Vec2 mouse = v2(input->mouse_x, input->mouse_y);
            for (u32 i = 0; i < pc; i++) {
                f32 ah, av, bh, bv;
                brush_top_world(b, local[i].x, local[i].y, &ah, &av);
                brush_top_world(b, local[(i + 1) % pc].x, local[(i + 1) % pc].y, &bh, &bv);
                Vec2 sa = brush_world_to_screen(ah, av);
                Vec2 sb = brush_world_to_screen(bh, bv);
                Vec2 ab = vec2_sub(sb, sa);
                Vec2 ap = vec2_sub(mouse, sa);
                f32 denom = vec2_dot(ab, ab);
                f32 t = denom > 1e-6f ? f_clamp01(vec2_dot(ap, ab) / denom) : 0.0f;
                Vec2 proj = vec2_add(sa, vec2_scale(ab, t));
                f32 d = vec2_length(vec2_sub(mouse, proj));
                if (d < best) {
                    best = d;
                    best_edge = (i32)i;
                    best_t = t;
                }
            }
            if (best_edge >= 0) {
                if (b->kind == BRUSH_BOX) {
                    poly_from_box(b);
                }
                Vec2 np = vec2_lerp(b->points[best_edge],
                                    b->points[(best_edge + 1) % b->point_count], best_t);
                np.x = brush_snap(np.x, step);
                np.y = brush_snap(np.y, step);
                u32 at = (u32)best_edge + 1;
                memmove(&b->points[at + 1], &b->points[at],
                        (b->point_count - at) * sizeof(Vec2));
                b->points[at] = np;
                b->point_count++;
                if (!poly_convex(b->points, b->point_count)) {
                    memmove(&b->points[at], &b->points[at + 1],
                            (b->point_count - at - 1) * sizeof(Vec2));
                    b->point_count--;
                } else {
                    s_vert_sel = (i32)at;
                    s_vert_drag = 1;
                }
                vert_consumed = 1;
            }
        }
    }

    if (s_vert_drag) {
        vert_consumed = 1;
        if (ed->brush_sel >= 0 && ed->brush_sel < (i32)s_struct.count) {
            Brush* b = &s_struct.brushes[ed->brush_sel];
            if (input->mouse_down[MOUSE_LEFT] && s_vert_sel >= 0
                && s_vert_sel < (i32)b->point_count) {
                f32 lx, lz;
                brush_top_local(b, wh, wv, &lx, &lz);
                Vec2 old = b->points[s_vert_sel];
                b->points[s_vert_sel] = v2(brush_snap(lx, step), brush_snap(lz, step));
                if (!poly_convex(b->points, b->point_count)) {
                    b->points[s_vert_sel] = old;
                }
            }
            if (!input->mouse_down[MOUSE_LEFT]) {
                s_vert_drag = 0;
                poly_normalize(b);
            }
        } else {
            s_vert_drag = 0;
        }
    }

    if (!vert_consumed && input->mouse_pressed[MOUSE_LEFT] && !over_panel && in_view
        && !input->mouse_down[MOUSE_RIGHT]) {
        s_drag = 0;
        if (!ctrl && !(s_vert_mode && s_view == VIEW_TOP)
            && ed->brush_sel >= 0 && ed->brush_sel < (i32)s_struct.count) {
            i32 hh, hv;
            if (brush_handle_hit(&s_struct.brushes[ed->brush_sel], input->mouse_x,
                                 input->mouse_y, &hh, &hv)) {
                s_drag = 3;
                s_rs_h = hh;
                s_rs_v = hv;
            }
        }
        if (!s_drag && !ctrl) {
            i32 hit = -1;
            for (i32 i = (i32)s_struct.count - 1; i >= 0; i--) {
                if (brush_view_contains(&s_struct.brushes[i], wh, wv)) {
                    hit = i;
                    break;
                }
            }
            if (hit >= 0) {
                if (hit != ed->brush_sel) {
                    ed->brush_sel = hit;
                    s_mat_sync = -1;
                    s_vert_sel = -1;
                    s_drag = -1;
                } else {
                    Brush* b = &s_struct.brushes[hit];
                    s_grab_dh = wh - vec3_axis(b->pos, ha);
                    s_grab_dv = wv - vec3_axis(b->pos, va);
                    s_drag = 2;
                }
            }
        }
        if (!s_drag) {
            ed->brush_sel = -1;
            s_drag = 1;
            s_draw_h0 = brush_snap(wh, step);
            s_draw_v0 = brush_snap(wv, step);
        }
    }

    if (s_drag && input->mouse_down[MOUSE_LEFT]) {
        if (s_drag == 2 && ed->brush_sel >= 0 && ed->brush_sel < (i32)s_struct.count) {
            Brush* b = &s_struct.brushes[ed->brush_sel];
            vec3_set_axis(&b->pos, ha, brush_snap(wh - s_grab_dh, step));
            vec3_set_axis(&b->pos, va, brush_snap(wv - s_grab_dv, step));
        } else if (s_drag == 3 && ed->brush_sel >= 0 && ed->brush_sel < (i32)s_struct.count) {
            Brush* b = &s_struct.brushes[ed->brush_sel];
            for (i32 pass = 0; pass < 2; pass++) {
                i32 side = pass == 0 ? s_rs_h : s_rs_v;
                if (side < 0) {
                    continue;
                }
                i32 axis = pass == 0 ? ha : va;
                f32 value = brush_snap(pass == 0 ? wh : wv, step);
                f32 center = vec3_axis(b->pos, axis);
                f32 half = vec3_axis(b->size, axis) * 0.5f;
                f32 lo = center - half;
                f32 hi = center + half;
                if (side == 0) {
                    lo = f_min(value, hi - BRUSH_MIN_SIZE);
                } else {
                    hi = f_max(value, lo + BRUSH_MIN_SIZE);
                }
                vec3_set_axis(&b->pos, axis, (lo + hi) * 0.5f);
                vec3_set_axis(&b->size, axis, hi - lo);
                if (b->kind == BRUSH_POLY && axis != 1 && half > 1e-4f) {
                    f32 ratio = (hi - lo) / (half * 2.0f);
                    for (u32 pi = 0; pi < b->point_count; pi++) {
                        if (axis == 0) {
                            b->points[pi].x *= ratio;
                        } else {
                            b->points[pi].y *= ratio;
                        }
                    }
                }
            }
        }
    }

    if (s_drag && !input->mouse_down[MOUSE_LEFT]) {
        if (s_drag == 1) {
            f32 h1 = brush_snap(wh, step);
            f32 v1 = brush_snap(wv, step);
            f32 dh = f_abs(h1 - s_draw_h0);
            f32 dv = f_abs(v1 - s_draw_v0);
            if (dh >= BRUSH_MIN_SIZE && dv >= BRUSH_MIN_SIZE
                && s_struct.count < STRUCT_MAX_BRUSHES) {
                Brush* b = &s_struct.brushes[s_struct.count];
                memset(b, 0, sizeof(*b));
                b->kind = BRUSH_BOX;
                snprintf(b->material, sizeof(b->material), "concrete");
                vec3_set_axis(&b->pos, ha, (s_draw_h0 + h1) * 0.5f);
                vec3_set_axis(&b->pos, va, (s_draw_v0 + v1) * 0.5f);
                vec3_set_axis(&b->size, ha, dh);
                vec3_set_axis(&b->size, va, dv);
                if (s_view == VIEW_TOP) {
                    b->size.y = 3.0f;
                    b->pos.y = 1.5f;
                } else if (s_view == VIEW_FRONT) {
                    b->size.z = 4.0f;
                    b->pos.z = 0.0f;
                } else {
                    b->size.x = 4.0f;
                    b->pos.x = 0.0f;
                }
                ed->brush_sel = (i32)s_struct.count;
                s_mat_sync = -1;
                s_struct.count++;
            }
        }
        s_drag = 0;
    }

    if (s_gesture_open && !input->mouse_down[MOUSE_LEFT]) {
        s_gesture_open = 0;
        if (memcmp(&s_gesture_pre, &s_struct, sizeof(Structure)) != 0) {
            brush_push_undo(&s_gesture_pre);
        }
    }
}

static void brush_line_clipped(f32 x0, f32 y0, f32 x1, f32 y1, u32 color)
{
    Vec2 vp = r_viewport_size();
    f32 t0 = 0.0f, t1 = 1.0f;
    f32 dx = x1 - x0, dy = y1 - y0;
    f32 p[4] = { -dx, dx, -dy, dy };
    f32 q[4] = { x0, s_view_w - x0, y0, vp.y - y0 };
    for (i32 i = 0; i < 4; i++) {
        if (p[i] == 0.0f) {
            if (q[i] < 0.0f) {
                return;
            }
            continue;
        }
        f32 r = q[i] / p[i];
        if (p[i] < 0.0f) {
            t0 = f_max(t0, r);
        } else {
            t1 = f_min(t1, r);
        }
    }
    if (t0 > t1) {
        return;
    }
    dd_line_2d(x0 + dx * t0, y0 + dy * t0, x0 + dx * t1, y0 + dy * t1, color);
}

static void brush_view_corners(const Brush* b, Vec2 out[4])
{
    i32 ha, va;
    f32 vdir;
    view_axes(&ha, &va, &vdir);
    if (s_view == VIEW_TOP) {
        f32 c = cosf(b->yaw), s = sinf(b->yaw);
        f32 hx = b->size.x * 0.5f;
        f32 hz = b->size.z * 0.5f;
        f32 lx[4] = { -hx, hx, hx, -hx };
        f32 lz[4] = { -hz, -hz, hz, hz };
        for (i32 i = 0; i < 4; i++) {
            f32 x = b->pos.x + lx[i] * c + lz[i] * s;
            f32 z = b->pos.z - lx[i] * s + lz[i] * c;
            out[i] = brush_world_to_screen(x, z);
        }
        return;
    }
    f32 ch = vec3_axis(b->pos, ha);
    f32 cv = vec3_axis(b->pos, va);
    f32 hh = vec3_axis(b->size, ha) * 0.5f;
    f32 hv = vec3_axis(b->size, va) * 0.5f;
    out[0] = brush_world_to_screen(ch - hh, cv - hv);
    out[1] = brush_world_to_screen(ch + hh, cv - hv);
    out[2] = brush_world_to_screen(ch + hh, cv + hv);
    out[3] = brush_world_to_screen(ch - hh, cv + hv);
}

static void brush_sidebar(EditorState* ed, f32 px)
{
    static const char* view_names[3] = { "view: top (1)", "view: front (2)", "view: side (3)" };
    ui_panel_begin("structure", px, 16.0f, BRUSH_SIDEBAR_W - 16.0f);
    if (ui_list_item(view_names[s_view], 0)) {
        s_view = (BrushView)(((i32)s_view + 1) % 3);
    }
    ui_slider_f32("grid snap", &ed->snap_pos, 0.0f, 5.0f);
    if (ui_text_field("name", s_name_buf, sizeof(s_name_buf)) || s_name_buf[0]) {
        snprintf(s_struct.name, sizeof(s_struct.name), "%s", s_name_buf);
    }
    if (ui_button("save .struct")) {
        brush_save(ed);
    }
    if (ui_button("bake .amsh")) {
        brush_bake(ed);
    }
    if (ui_button("new structure")) {
        memset(&s_struct, 0, sizeof(s_struct));
        snprintf(s_struct.name, sizeof(s_struct.name), "structure");
        snprintf(s_name_buf, sizeof(s_name_buf), "%s", s_struct.name);
        ed->brush_sel = -1;
        s_mat_sync = -1;
    }
    if (s_file_count > BRUSH_LIST_PAGE && ui_button("scroll files")) {
        s_file_scroll += BRUSH_LIST_PAGE;
        if (s_file_scroll >= (i32)s_file_count) {
            s_file_scroll = 0;
        }
    }
    for (i32 i = s_file_scroll;
         i < (i32)s_file_count && i < s_file_scroll + BRUSH_LIST_PAGE; i++) {
        if (ui_list_item(s_files[i], strcmp(s_files[i], s_struct.name) == 0)) {
            brush_load(ed, s_files[i]);
        }
    }
    ui_label("undo %u | redo %u", s_undo_count, s_redo_count);
    if (ui_button("undo (ctrl+z)")) {
        brush_undo(ed);
    }
    if (ui_button("redo (ctrl+y)")) {
        brush_redo(ed);
    }
    if (ui_button("exit structure mode")) {
        ed->brush_mode = 0;
        ed->brush_sel = -1;
        s_drag = 0;
    }
    if (ed->status_time > 0.0f) {
        ui_label("%s", ed->status);
    }
    ui_panel_end();

    ui_panel_begin("brushes", px, 470.0f, BRUSH_SIDEBAR_W - 16.0f);
    ui_label("brushes: %u  (drag to draw)", s_struct.count);
    if (ui_button("add wedge")) {
        brush_add(ed, BRUSH_WEDGE);
    }
    if (ed->brush_sel >= 0 && ed->brush_sel < (i32)s_struct.count) {
        Brush* b = &s_struct.brushes[ed->brush_sel];
        const char* kind_name = b->kind == BRUSH_WEDGE ? "wedge"
                                : (b->kind == BRUSH_POLY ? "poly" : "box");
        if (b->kind == BRUSH_POLY) {
            ui_label("sel: poly %u pts %s", b->point_count,
                     b->subtract ? "(carve)" : "(solid)");
        } else {
            ui_label("sel: %s %s", kind_name, b->subtract ? "(carve)" : "(solid)");
        }
        if (b->kind != BRUSH_WEDGE) {
            b32 vm = s_vert_mode;
            if (ui_checkbox("edit points (v)", &vm)) {
                s_vert_mode = vm;
                s_vert_sel = -1;
                s_vert_drag = 0;
            }
            if (s_vert_mode) {
                ui_label("top view: drag points");
                ui_label("ctrl+click edge adds point");
            }
        }
        b32 sub = b->subtract;
        if (ui_checkbox("carve (subtract)", &sub)) {
            b->subtract = sub;
        }
        if (s_mat_sync != ed->brush_sel) {
            s_mat_sync = ed->brush_sel;
            snprintf(s_mat_buf, sizeof(s_mat_buf), "%s", b->material);
        }
        if (ui_text_field("material (enter)", s_mat_buf, sizeof(s_mat_buf)) && s_mat_buf[0]
            && strcmp(s_mat_buf, b->material) != 0) {
            brush_push_undo(&s_struct);
            snprintf(b->material, sizeof(b->material), "%s", s_mat_buf);
        }
        f32 yaw_deg = b->yaw * RAD_TO_DEG;
        if (ui_slider_f32("yaw", &yaw_deg, -180.0f, 180.0f)) {
            b->yaw = yaw_deg * DEG_TO_RAD;
        }
        if (b->kind == BRUSH_BOX && !b->subtract) {
            ui_slider_f32("hollow walls", &b->hollow, 0.0f, 1.0f);
        }
        ui_label("pos %.2f %.2f %.2f", (f64)b->pos.x, (f64)b->pos.y, (f64)b->pos.z);
        ui_label("size %.2f %.2f %.2f", (f64)b->size.x, (f64)b->size.y, (f64)b->size.z);
        if (ui_button("duplicate brush")) {
            if (s_struct.count < STRUCT_MAX_BRUSHES) {
                Brush copy = *b;
                copy.pos = vec3_add(copy.pos, v3(1.0f, 0.0f, 1.0f));
                s_struct.brushes[s_struct.count] = copy;
                ed->brush_sel = (i32)s_struct.count;
                s_struct.count++;
                s_mat_sync = -1;
            }
        }
        if (ui_button("delete brush (del)")) {
            brush_delete_selected(ed);
        }
    } else {
        ui_label("drag empty grid: draw box");
        ui_label("ctrl+drag: draw over brushes");
        ui_label("drag handles: resize");
        ui_label("arrows: nudge | del: delete");
        ui_label("f: frame | g: cycle grid");
        ui_label("rmb pan | scroll zoom | esc");
    }
    ui_panel_end();
}

void editor_brush_render(EditorState* ed, const struct GameInput* input,
                         const struct Camera* cam, const struct Terrain* terrain)
{
    (void)cam;
    (void)terrain;
    Vec2 vp = r_viewport_size();
    s_view_w = vp.x - BRUSH_SIDEBAR_W - 16.0f;
    f32 px = s_view_w + 8.0f;
    f32 step = brush_snap_step(ed);

    dd_rect_2d_filled(0.0f, 0.0f, vp.x, vp.y, dd_rgba(11, 13, 17, 255));

    f32 draw_step = step;
    while (draw_step * s_zoom < 8.0f) {
        draw_step *= 4.0f;
    }
    f32 h_min, v_a, h_max, v_b;
    brush_screen_to_world(0.0f, 0.0f, &h_min, &v_a);
    brush_screen_to_world(s_view_w, vp.y, &h_max, &v_b);
    f32 v_min = f_min(v_a, v_b);
    f32 v_max = f_max(v_a, v_b);
    for (f32 h = floorf(h_min / draw_step) * draw_step; h <= h_max; h += draw_step) {
        Vec2 s = brush_world_to_screen(h, 0.0f);
        b32 major = f_abs(h - floorf(h / (draw_step * 8.0f) + 0.5f) * draw_step * 8.0f) < draw_step * 0.5f;
        if (s.x >= 0.0f && s.x <= s_view_w) {
            dd_line_2d(s.x, 0.0f, s.x, vp.y, major ? dd_rgba(52, 60, 72, 255)
                                                   : dd_rgba(28, 33, 41, 255));
        }
    }
    for (f32 v = floorf(v_min / draw_step) * draw_step; v <= v_max; v += draw_step) {
        Vec2 s = brush_world_to_screen(0.0f, v);
        b32 major = f_abs(v - floorf(v / (draw_step * 8.0f) + 0.5f) * draw_step * 8.0f) < draw_step * 0.5f;
        if (s.y >= 0.0f && s.y <= vp.y) {
            dd_line_2d(0.0f, s.y, s_view_w, s.y, major ? dd_rgba(52, 60, 72, 255)
                                                       : dd_rgba(28, 33, 41, 255));
        }
    }
    Vec2 origin_s = brush_world_to_screen(0.0f, 0.0f);
    if (origin_s.x >= 0.0f && origin_s.x <= s_view_w) {
        dd_line_2d(origin_s.x, 0.0f, origin_s.x, vp.y, dd_rgba(70, 110, 80, 255));
    }
    if (origin_s.y >= 0.0f && origin_s.y <= vp.y) {
        dd_line_2d(0.0f, origin_s.y, s_view_w, origin_s.y, dd_rgba(110, 70, 70, 255));
    }

    for (u32 i = 0; i < s_struct.count; i++) {
        const Brush* b = &s_struct.brushes[i];
        b32 is_sel = (i32)i == ed->brush_sel;
        u32 color = b->subtract ? DD_RED : DD_GREEN;
        if (is_sel) {
            color = DD_YELLOW;
        }
        b32 poly_top = s_view == VIEW_TOP && b->kind == BRUSH_POLY && b->point_count >= 3;
        Vec2 corners[4];
        brush_view_corners(b, corners);
        if (is_sel && !poly_top && !(s_view == VIEW_TOP && b->yaw != 0.0f)) {
            f32 x0 = f_min(f_min(corners[0].x, corners[1].x), f_min(corners[2].x, corners[3].x));
            f32 x1 = f_max(f_max(corners[0].x, corners[1].x), f_max(corners[2].x, corners[3].x));
            f32 y0 = f_min(f_min(corners[0].y, corners[1].y), f_min(corners[2].y, corners[3].y));
            f32 y1 = f_max(f_max(corners[0].y, corners[1].y), f_max(corners[2].y, corners[3].y));
            dd_rect_2d_filled(f_max(x0, 0.0f), f_max(y0, 0.0f), f_min(x1, s_view_w),
                              f_min(y1, vp.y),
                              b->subtract ? dd_rgba(120, 40, 40, 50) : dd_rgba(120, 120, 40, 50));
        }
        if (poly_top) {
            for (u32 k = 0; k < b->point_count; k++) {
                Vec2 pa = b->points[k];
                Vec2 pb = b->points[(k + 1) % b->point_count];
                f32 ah, av, bh, bv;
                brush_top_world(b, pa.x, pa.y, &ah, &av);
                brush_top_world(b, pb.x, pb.y, &bh, &bv);
                Vec2 sa = brush_world_to_screen(ah, av);
                Vec2 sb = brush_world_to_screen(bh, bv);
                brush_line_clipped(sa.x, sa.y, sb.x, sb.y, color);
            }
        } else {
            for (i32 k = 0; k < 4; k++) {
                Vec2 a = corners[k];
                Vec2 c = corners[(k + 1) % 4];
                brush_line_clipped(a.x, a.y, c.x, c.y, color);
            }
        }
        if (b->kind == BRUSH_WEDGE) {
            brush_line_clipped(corners[0].x, corners[0].y, corners[2].x, corners[2].y, color);
        }
        if (b->kind == BRUSH_BOX && !b->subtract && b->hollow > 0.0f) {
            Brush inner = *b;
            inner.size = vec3_sub(b->size, vec3_scale(v3(1.0f, 1.0f, 1.0f), b->hollow * 2.0f));
            if (inner.size.x > 0.05f && inner.size.y > 0.05f && inner.size.z > 0.05f) {
                Vec2 ic[4];
                brush_view_corners(&inner, ic);
                u32 inner_color = is_sel ? dd_rgba(200, 200, 90, 255) : dd_rgba(60, 120, 60, 255);
                for (i32 k = 0; k < 4; k++) {
                    Vec2 a = ic[k];
                    Vec2 c = ic[(k + 1) % 4];
                    brush_line_clipped(a.x, a.y, c.x, c.y, inner_color);
                }
            }
        }
        if (is_sel) {
            b32 vert_view = s_vert_mode && s_view == VIEW_TOP && b->kind != BRUSH_WEDGE;
            if (vert_view) {
                Vec2 local[BRUSH_POLY_MAX];
                u32 pc = brush_edit_points(b, local);
                for (u32 k = 0; k < pc; k++) {
                    f32 h, v;
                    brush_top_world(b, local[k].x, local[k].y, &h, &v);
                    Vec2 sp = brush_world_to_screen(h, v);
                    if (sp.x < 0.0f || sp.x > s_view_w) {
                        continue;
                    }
                    b32 vsel = b->kind == BRUSH_POLY && (i32)k == s_vert_sel;
                    f32 r = vsel ? 5.0f : 3.5f;
                    dd_rect_2d_filled(sp.x - r, sp.y - r, sp.x + r, sp.y + r,
                                      vsel ? DD_WHITE : DD_CYAN);
                }
            } else {
                Vec2 pts[8];
                i32 hs[8], vs[8];
                u32 n = brush_handle_points(b, pts, hs, vs);
                for (u32 k = 0; k < n; k++) {
                    if (pts[k].x < 0.0f || pts[k].x > s_view_w) {
                        continue;
                    }
                    dd_rect_2d_filled(pts[k].x - 3.0f, pts[k].y - 3.0f, pts[k].x + 3.0f,
                                      pts[k].y + 3.0f, DD_YELLOW);
                }
            }
            i32 ha, va;
            f32 vdir;
            view_axes(&ha, &va, &vdir);
            Vec2 center = brush_world_to_screen(vec3_axis(b->pos, ha), vec3_axis(b->pos, va));
            if (center.x >= 0.0f && center.x <= s_view_w - 90.0f) {
                dd_text_2d(center.x + 6.0f, center.y - 6.0f, 13.0f, DD_YELLOW, "%.4g x %.4g",
                           (f64)vec3_axis(b->size, ha), (f64)vec3_axis(b->size, va));
            }
        }
    }

    if (s_drag == 1) {
        f32 wh, wv;
        brush_screen_to_world(input->mouse_x, input->mouse_y, &wh, &wv);
        f32 h1 = brush_snap(wh, step);
        f32 v1 = brush_snap(wv, step);
        Vec2 a = brush_world_to_screen(s_draw_h0, s_draw_v0);
        Vec2 c = brush_world_to_screen(h1, v1);
        brush_line_clipped(a.x, a.y, c.x, a.y, DD_CYAN);
        brush_line_clipped(c.x, a.y, c.x, c.y, DD_CYAN);
        brush_line_clipped(c.x, c.y, a.x, c.y, DD_CYAN);
        brush_line_clipped(a.x, c.y, a.x, a.y, DD_CYAN);
        dd_text_2d(f_min(c.x + 8.0f, s_view_w - 90.0f), c.y - 8.0f, 13.0f, DD_CYAN,
                   "%.4g x %.4g", (f64)f_abs(h1 - s_draw_h0), (f64)f_abs(v1 - s_draw_v0));
    }

    {
        f32 wh, wv;
        brush_screen_to_world(input->mouse_x, input->mouse_y, &wh, &wv);
        static const char* axis_names[3] = { "x/z", "x/y", "z/y" };
        dd_text_2d(12.0f, vp.y - 14.0f, 14.0f, DD_GRAY, "%s  %.2f / %.2f  grid %.2g  zoom %.0f",
                   axis_names[s_view], (f64)brush_snap(wh, step), (f64)brush_snap(wv, step),
                   (f64)draw_step, (f64)s_zoom);
    }

    brush_sidebar(ed, px);
}
