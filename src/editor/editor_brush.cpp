#include "editor/editor_brush.h"
#include "core/arena.h"
#include "core/config.h"
#include "core/log.h"
#include "editor/editor.h"
#include "platform/filesystem.h"
#include "platform/input.h"
#include "platform/input_context.h"
#include "render/debug_draw.h"
#include "render/text.h"
#include "ui/ui.h"

#include <charconv>
#include <cstdio>
#include <cstring>

namespace anom {
namespace {

constexpr const char* kStructDir = "assets/structures";

class Tokens {
public:
    explicit Tokens(std::string_view line) : cursor_(line) {}

    std::string_view next()
    {
        const std::size_t begin = cursor_.find_first_not_of(" \t");
        if (begin == std::string_view::npos) {
            cursor_ = {};
            return {};
        }
        const std::size_t end = cursor_.find_first_of(" \t", begin);
        const std::string_view token = cursor_.substr(
            begin, end == std::string_view::npos ? std::string_view::npos : end - begin);
        cursor_ = end == std::string_view::npos ? std::string_view{} : cursor_.substr(end);
        count_++;
        return token;
    }

    f32 next_f32(f32 fallback = 0.0f)
    {
        const std::string_view token = next();
        if (token.empty()) {
            return fallback;
        }
        f32 value = fallback;
        std::from_chars(token.data(), token.data() + token.size(), value);
        return value;
    }

    u32 next_u32(u32 fallback = 0)
    {
        const std::string_view token = next();
        if (token.empty()) {
            return fallback;
        }
        u32 value = fallback;
        std::from_chars(token.data(), token.data() + token.size(), value);
        return value;
    }

    u32 consumed() const { return count_; }

private:
    std::string_view cursor_;
    u32 count_ = 0;
};

f32 axis_of(Vec3 v, i32 axis)
{
    return elem(v, axis);
}

void set_axis(Vec3& v, i32 axis, f32 value)
{
    if (axis == 0) {
        v.x = value;
    } else if (axis == 1) {
        v.y = value;
    } else {
        v.z = value;
    }
}

f32 snap_to(f32 value, f32 step)
{
    return std::floor(value / step + 0.5f) * step;
}

void top_local(const Brush& b, f32 h, f32 v, f32& out_lx, f32& out_lz)
{
    const f32 c = std::cos(b.yaw);
    const f32 s = std::sin(b.yaw);
    const f32 dx = h - b.pos.x;
    const f32 dz = v - b.pos.z;
    out_lx = dx * c - dz * s;
    out_lz = dx * s + dz * c;
}

void top_world(const Brush& b, f32 lx, f32 lz, f32& out_h, f32& out_v)
{
    const f32 c = std::cos(b.yaw);
    const f32 s = std::sin(b.yaw);
    out_h = b.pos.x + lx * c + lz * s;
    out_v = b.pos.z - lx * s + lz * c;
}

bool ends_with(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

} // namespace

bool operator==(const Brush& a, const Brush& b)
{
    if (a.kind != b.kind || a.subtract != b.subtract || a.pos != b.pos || a.size != b.size
        || a.yaw != b.yaw || a.hollow != b.hollow || a.point_count != b.point_count
        || a.material != b.material) {
        return false;
    }
    for (u32 i = 0; i < a.point_count; i++) {
        if (a.points[i] != b.points[i]) {
            return false;
        }
    }
    return true;
}

bool operator==(const Structure& a, const Structure& b)
{
    if (a.count != b.count || a.name != b.name) {
        return false;
    }
    for (u32 i = 0; i < a.count; i++) {
        if (!(a.brushes[i] == b.brushes[i])) {
            return false;
        }
    }
    return true;
}

void BrushEditor::init(Arena& storage)
{
    undo_stack_ = storage.push_array<Structure>(kBrushUndoMax);
    redo_stack_ = storage.push_array<Structure>(kBrushUndoMax);
    gesture_pre_ = storage.push<Structure>();
    structure_.name.assign("structure");
    std::snprintf(name_buf_, sizeof(name_buf_), "%s", structure_.name.c_str());
}

void BrushEditor::scan_files()
{
    fs::DirEntry entries[kStructMaxFiles];
    const u32 found = fs::list_dir(kStructDir, entries);

    file_count_ = 0;
    for (u32 i = 0; i < found && file_count_ < kStructMaxFiles; i++) {
        const std::string_view name = entries[i].name.view();
        if (entries[i].is_dir || !ends_with(name, ".struct")) {
            continue;
        }
        files_[file_count_++].assign(name.substr(0, name.size() - 7));
    }
}

void BrushEditor::reset(Arena& scratch)
{
    (void)scratch;
    scan_files();
    file_scroll_ = 0;
    material_sync_ = -1;
    drag_ = 0;
    if (structure_.name.empty()) {
        structure_.name.assign("structure");
    }
    std::snprintf(name_buf_, sizeof(name_buf_), "%s", structure_.name.c_str());
}

void BrushEditor::push_undo(const Structure& pre)
{
    if (!undo_stack_) {
        return;
    }
    if (undo_count_ >= kBrushUndoMax) {
        for (u32 i = 1; i < kBrushUndoMax; i++) {
            undo_stack_[i - 1] = undo_stack_[i];
        }
        undo_count_--;
    }
    undo_stack_[undo_count_++] = pre;
    redo_count_ = 0;
}

void BrushEditor::undo(Editor& editor)
{
    gesture_open_ = false;
    if (!undo_count_) {
        editor.status("nothing to undo");
        return;
    }
    if (redo_count_ < kBrushUndoMax) {
        redo_stack_[redo_count_++] = structure_;
    }
    structure_ = undo_stack_[--undo_count_];
    if (selection_ >= static_cast<i32>(structure_.count)) {
        selection_ = -1;
    }
    material_sync_ = -1;
    std::snprintf(name_buf_, sizeof(name_buf_), "%s", structure_.name.c_str());
    editor.status("undo");
}

void BrushEditor::redo(Editor& editor)
{
    gesture_open_ = false;
    if (!redo_count_) {
        editor.status("nothing to redo");
        return;
    }
    if (undo_count_ < kBrushUndoMax) {
        undo_stack_[undo_count_++] = structure_;
    }
    structure_ = redo_stack_[--redo_count_];
    if (selection_ >= static_cast<i32>(structure_.count)) {
        selection_ = -1;
    }
    material_sync_ = -1;
    std::snprintf(name_buf_, sizeof(name_buf_), "%s", structure_.name.c_str());
    editor.status("redo");
}

void BrushEditor::view_axes(i32& out_h, i32& out_v, f32& out_vdir) const
{
    if (view_ == BrushView::Top) {
        out_h = 0;
        out_v = 2;
        out_vdir = 1.0f;
    } else if (view_ == BrushView::Front) {
        out_h = 0;
        out_v = 1;
        out_vdir = -1.0f;
    } else {
        out_h = 2;
        out_v = 1;
        out_vdir = -1.0f;
    }
}

Vec2 BrushEditor::world_to_screen(f32 h, f32 v, Vec2 viewport) const
{
    i32 ha = 0;
    i32 va = 0;
    f32 vdir = 1.0f;
    view_axes(ha, va, vdir);
    return Vec2{view_w_ * 0.5f + (h - pan_h_) * zoom_,
                viewport.y * 0.5f + vdir * (v - pan_v_) * zoom_};
}

void BrushEditor::screen_to_world(f32 sx, f32 sy, Vec2 viewport, f32& out_h, f32& out_v) const
{
    i32 ha = 0;
    i32 va = 0;
    f32 vdir = 1.0f;
    view_axes(ha, va, vdir);
    out_h = pan_h_ + (sx - view_w_ * 0.5f) / zoom_;
    out_v = pan_v_ + vdir * (sy - viewport.y * 0.5f) / zoom_;
}

f32 BrushEditor::snap_step(const Editor& editor) const
{
    return editor.snap().pos > 0.0f ? editor.snap().pos : 1.0f;
}

void BrushEditor::add_brush(Editor& editor, BrushKind kind)
{
    if (structure_.count >= kStructMaxBrushes) {
        editor.status("brush limit reached");
        return;
    }
    Brush& b = structure_.brushes[structure_.count];
    b = Brush{};
    b.kind = kind;
    b.size = Vec3{4.0f, 3.0f, 4.0f};

    i32 ha = 0;
    i32 va = 0;
    f32 vdir = 1.0f;
    view_axes(ha, va, vdir);
    set_axis(b.pos, ha, snap_to(pan_h_, 1.0f));
    set_axis(b.pos, va, snap_to(pan_v_, 1.0f));
    if (view_ == BrushView::Top) {
        b.pos.y = b.size.y * 0.5f;
    }
    b.material.assign("concrete");

    selection_ = static_cast<i32>(structure_.count);
    material_sync_ = -1;
    structure_.count++;
}

void BrushEditor::delete_selected(Editor& editor)
{
    (void)editor;
    if (selection_ < 0 || selection_ >= static_cast<i32>(structure_.count)) {
        return;
    }
    for (u32 i = static_cast<u32>(selection_) + 1; i < structure_.count; i++) {
        structure_.brushes[i - 1] = structure_.brushes[i];
    }
    structure_.count--;
    selection_ = -1;
    material_sync_ = -1;
}

void BrushEditor::nudge(Editor& editor, f32 dh, f32 dv)
{
    if (selection_ < 0 || selection_ >= static_cast<i32>(structure_.count)) {
        return;
    }
    i32 ha = 0;
    i32 va = 0;
    f32 vdir = 1.0f;
    view_axes(ha, va, vdir);

    push_undo(structure_);
    (void)editor;
    Brush& b = structure_.brushes[selection_];
    set_axis(b.pos, ha, axis_of(b.pos, ha) + dh);
    set_axis(b.pos, va, axis_of(b.pos, va) + dv);
}

void BrushEditor::frame_view(Vec2 viewport)
{
    i32 ha = 0;
    i32 va = 0;
    f32 vdir = 1.0f;
    view_axes(ha, va, vdir);

    if (!structure_.count) {
        pan_h_ = 0.0f;
        pan_v_ = 0.0f;
        zoom_ = 24.0f;
        return;
    }

    f32 h_min = 1e30f;
    f32 h_max = -1e30f;
    f32 v_min = 1e30f;
    f32 v_max = -1e30f;
    for (u32 i = 0; i < structure_.count; i++) {
        const Brush& b = structure_.brushes[i];
        const f32 r = length(b.size * 0.5f);
        h_min = f_min(h_min, axis_of(b.pos, ha) - r);
        h_max = f_max(h_max, axis_of(b.pos, ha) + r);
        v_min = f_min(v_min, axis_of(b.pos, va) - r);
        v_max = f_max(v_max, axis_of(b.pos, va) + r);
    }
    pan_h_ = (h_min + h_max) * 0.5f;
    pan_v_ = (v_min + v_max) * 0.5f;

    const f32 span_h = f_max(h_max - h_min, 1.0f);
    const f32 span_v = f_max(v_max - v_min, 1.0f);
    zoom_ = f_clamp(f_min(view_w_ / span_h, viewport.y / span_v) * 0.8f, 2.0f, 200.0f);
}

bool BrushEditor::view_contains(const Brush& b, f32 h, f32 v) const
{
    i32 ha = 0;
    i32 va = 0;
    f32 vdir = 1.0f;
    view_axes(ha, va, vdir);

    if (view_ == BrushView::Top && b.kind == BrushKind::Poly && b.point_count >= 3) {
        f32 lx = 0.0f;
        f32 lz = 0.0f;
        top_local(b, h, v, lx, lz);
        for (u32 i = 0; i < b.point_count; i++) {
            const Vec2 a = b.points[i];
            const Vec2 e = b.points[(i + 1) % b.point_count];
            if ((e.x - a.x) * (lz - a.y) - (e.y - a.y) * (lx - a.x) < 0.0f) {
                return false;
            }
        }
        return true;
    }
    if (view_ == BrushView::Top && b.yaw != 0.0f) {
        f32 lx = 0.0f;
        f32 lz = 0.0f;
        top_local(b, h, v, lx, lz);
        return f_abs(lx) <= b.size.x * 0.5f && f_abs(lz) <= b.size.z * 0.5f;
    }
    return f_abs(h - axis_of(b.pos, ha)) <= axis_of(b.size, ha) * 0.5f
        && f_abs(v - axis_of(b.pos, va)) <= axis_of(b.size, va) * 0.5f;
}

u32 BrushEditor::handle_points(const Brush& b, Vec2 viewport, Vec2* out_pts, i32* out_h,
                               i32* out_v) const
{
    i32 ha = 0;
    i32 va = 0;
    f32 vdir = 1.0f;
    view_axes(ha, va, vdir);
    if (view_ == BrushView::Top && b.yaw != 0.0f) {
        return 0;
    }

    const f32 ch = axis_of(b.pos, ha);
    const f32 cv = axis_of(b.pos, va);
    const f32 hh = axis_of(b.size, ha) * 0.5f;
    const f32 hv = axis_of(b.size, va) * 0.5f;
    const f32 hs[3] = {ch - hh, ch + hh, ch};
    const f32 vs[3] = {cv - hv, cv + hv, cv};
    static const i32 kHandleH[8] = {0, 1, 0, 1, 0, 1, -1, -1};
    static const i32 kHandleV[8] = {0, 0, 1, 1, -1, -1, 0, 1};

    for (u32 i = 0; i < 8; i++) {
        const f32 h = kHandleH[i] < 0 ? hs[2] : hs[kHandleH[i]];
        const f32 v = kHandleV[i] < 0 ? vs[2] : vs[kHandleV[i]];
        out_pts[i] = world_to_screen(h, v, viewport);
        out_h[i] = kHandleH[i];
        out_v[i] = kHandleV[i];
    }
    return 8;
}

bool BrushEditor::handle_hit(const Brush& b, Vec2 viewport, Vec2 mouse, i32& out_h,
                             i32& out_v) const
{
    Vec2 pts[8];
    i32 hs[8];
    i32 vs[8];
    const u32 n = handle_points(b, viewport, pts, hs, vs);
    for (u32 i = 0; i < n; i++) {
        if (f_abs(mouse.x - pts[i].x) < kBrushEdgePickPx
            && f_abs(mouse.y - pts[i].y) < kBrushEdgePickPx) {
            out_h = hs[i];
            out_v = vs[i];
            return true;
        }
    }
    return false;
}

void BrushEditor::view_corners(const Brush& b, Vec2 viewport, Vec2 out[4]) const
{
    i32 ha = 0;
    i32 va = 0;
    f32 vdir = 1.0f;
    view_axes(ha, va, vdir);

    if (view_ == BrushView::Top) {
        const f32 c = std::cos(b.yaw);
        const f32 s = std::sin(b.yaw);
        const f32 hx = b.size.x * 0.5f;
        const f32 hz = b.size.z * 0.5f;
        const f32 lx[4] = {-hx, hx, hx, -hx};
        const f32 lz[4] = {-hz, -hz, hz, hz};
        for (i32 i = 0; i < 4; i++) {
            out[i] = world_to_screen(b.pos.x + lx[i] * c + lz[i] * s,
                                     b.pos.z - lx[i] * s + lz[i] * c, viewport);
        }
        return;
    }

    const f32 ch = axis_of(b.pos, ha);
    const f32 cv = axis_of(b.pos, va);
    const f32 hh = axis_of(b.size, ha) * 0.5f;
    const f32 hv = axis_of(b.size, va) * 0.5f;
    out[0] = world_to_screen(ch - hh, cv - hv, viewport);
    out[1] = world_to_screen(ch + hh, cv - hv, viewport);
    out[2] = world_to_screen(ch + hh, cv + hv, viewport);
    out[3] = world_to_screen(ch - hh, cv + hv, viewport);
}

bool BrushEditor::save(Editor& editor, Arena& scratch)
{
    (void)scratch;
    FixedString<256> path;
    path.format("%s/%s.struct", kStructDir, structure_.name.c_str());

    std::FILE* file = fs::open(path.view(), "wb");
    if (!file) {
        editor.status("struct save failed");
        return false;
    }

    std::fprintf(file, "[structure]\n");
    std::fprintf(file, "name = %s\n", structure_.name.c_str());
    for (u32 i = 0; i < structure_.count; i++) {
        const Brush& b = structure_.brushes[i];
        if (b.kind == BrushKind::Poly) {
            std::fprintf(file, "brush = poly %s %.3f %.3f %.3f %.3f %.2f %s %u",
                         b.subtract ? "sub" : "add", static_cast<f64>(b.pos.x),
                         static_cast<f64>(b.pos.y), static_cast<f64>(b.pos.z),
                         static_cast<f64>(b.size.y), static_cast<f64>(b.yaw * kRadToDeg),
                         b.material.c_str(), b.point_count);
            for (u32 k = 0; k < b.point_count; k++) {
                std::fprintf(file, " %.3f %.3f", static_cast<f64>(b.points[k].x),
                             static_cast<f64>(b.points[k].y));
            }
            std::fprintf(file, "\n");
            continue;
        }
        std::fprintf(file, "brush = %s %s %.3f %.3f %.3f %.3f %.3f %.3f %.2f %s %.3f\n",
                     b.kind == BrushKind::Wedge ? "wedge" : "box", b.subtract ? "sub" : "add",
                     static_cast<f64>(b.pos.x), static_cast<f64>(b.pos.y),
                     static_cast<f64>(b.pos.z), static_cast<f64>(b.size.x),
                     static_cast<f64>(b.size.y), static_cast<f64>(b.size.z),
                     static_cast<f64>(b.yaw * kRadToDeg), b.material.c_str(),
                     static_cast<f64>(b.hollow));
    }
    std::fclose(file);

    scan_files();
    editor.status("saved .struct");
    return true;
}

bool BrushEditor::load(Editor& editor, Arena& scratch, std::string_view name)
{
    ArenaScope scope(scratch);

    FixedString<256> path;
    path.format("%s/%.*s.struct", kStructDir, static_cast<int>(name.size()), name.data());
    const fs::FileData file = fs::read_entire_file(scratch, path.view());
    if (!file.valid()) {
        editor.status("struct load failed");
        return false;
    }

    Config cfg;
    if (!cfg.parse(scratch, file.text())) {
        editor.status("struct parse failed");
        return false;
    }

    structure_ = Structure{};
    structure_.name.assign(name);

    for (const Config::Entry& entry : cfg.entries()) {
        if (entry.key != "structure.brush" || structure_.count >= kStructMaxBrushes) {
            continue;
        }
        Tokens t(entry.value);
        Brush& b = structure_.brushes[structure_.count];
        b = Brush{};

        const std::string_view kind_str = t.next();
        const std::string_view mode_str = t.next();
        b.subtract = mode_str == "sub";

        if (kind_str == "poly") {
            b.pos.x = t.next_f32();
            b.pos.y = t.next_f32();
            b.pos.z = t.next_f32();
            b.size.y = t.next_f32();
            const f32 yaw_deg = t.next_f32();
            const std::string_view material = t.next();
            u32 pc = t.next_u32();
            if (t.consumed() < 9 || pc < 3) {
                log_warn("struct: malformed poly line: %.*s",
                         static_cast<int>(entry.value.size()), entry.value.data());
                continue;
            }
            pc = pc > kBrushPolyMax ? kBrushPolyMax : pc;

            b.kind = BrushKind::Poly;
            b.yaw = yaw_deg * kDegToRad;
            b.material.assign(material);
            b.point_count = 0;
            for (u32 k = 0; k < pc; k++) {
                const f32 px = t.next_f32();
                const f32 pz = t.next_f32();
                b.points[b.point_count++] = Vec2{px, pz};
            }
            if (b.point_count < 3) {
                log_warn("struct: poly with too few points");
                continue;
            }
            poly_normalize(b);
            structure_.count++;
            continue;
        }

        b.pos.x = t.next_f32();
        b.pos.y = t.next_f32();
        b.pos.z = t.next_f32();
        b.size.x = t.next_f32();
        b.size.y = t.next_f32();
        b.size.z = t.next_f32();
        const f32 yaw_deg = t.next_f32();
        const std::string_view material = t.next();
        if (t.consumed() < 10) {
            log_warn("struct: malformed brush line: %.*s", static_cast<int>(entry.value.size()),
                     entry.value.data());
            continue;
        }
        b.kind = kind_str == "wedge" ? BrushKind::Wedge : BrushKind::Box;
        b.yaw = yaw_deg * kDegToRad;
        b.material.assign(material);
        b.hollow = t.next_f32(0.0f);
        structure_.count++;
    }

    std::snprintf(name_buf_, sizeof(name_buf_), "%.*s", static_cast<int>(name.size()),
                  name.data());
    selection_ = -1;
    material_sync_ = -1;
    editor.status("loaded .struct");
    return true;
}

bool BrushEditor::bake(Editor& editor, Arena& scratch)
{
    bool any_add = false;
    for (u32 i = 0; i < structure_.count; i++) {
        any_add = any_add || !structure_.brushes[i].subtract;
    }
    if (!any_add) {
        editor.status("no solid brushes to bake");
        return false;
    }

    ArenaScope scope(scratch);
    BakeOutput out;
    out.polys = scratch.push_array<BrushPoly>(kBakeMaxPolys);
    out.cap = kBakeMaxPolys;
    out.count = 0;

    Brush* expanded = scratch.push_array<Brush>(kStructMaxBrushes * 2);
    if (!out.polys || !expanded) {
        editor.status("bake out of memory");
        return false;
    }
    const u32 count = bake_expand_hollow(structure_, expanded);
    bake_structure(out, scratch, expanded, count);

    FixedString<256> path;
    path.format("assets/meshes/%s.amsh", structure_.name.c_str());
    const bool ok = bake_write_amsh(path.view(), scratch, out);
    editor.status(ok ? "baked mesh" : "bake failed");
    return ok;
}

void BrushEditor::update(Editor& editor, const Input& input, const InputContext& ctx,
                         Arena& scratch, Vec2 viewport)
{
    view_w_ = viewport.x - kBrushSidebarWidth - 16.0f;

    const bool keys = ctx.keyboard(InputLayer::Editor);
    const bool over_panel = !ctx.pointer(InputLayer::Editor);
    const f32 step = snap_step(editor);
    const bool ctrl = input.down(Key::LeftControl);
    const bool mouse_busy = drag_ != 0 || vert_drag_ || input.down(MouseButton::Left);

    if (selection_ < 0 || selection_ >= static_cast<i32>(structure_.count)) {
        vert_sel_ = -1;
        vert_drag_ = false;
    } else if (vert_sel_ >= 0) {
        const Brush& vb = structure_.brushes[selection_];
        const u32 pc = vb.kind == BrushKind::Poly ? vb.point_count
                                                  : (vb.kind == BrushKind::Box ? 4u : 0u);
        if (vert_sel_ >= static_cast<i32>(pc)) {
            vert_sel_ = -1;
        }
    }

    if (keys) {
        if (input.pressed(Key::Num1)) {
            view_ = BrushView::Top;
        }
        if (input.pressed(Key::Num2)) {
            view_ = BrushView::Front;
        }
        if (input.pressed(Key::Num3)) {
            view_ = BrushView::Side;
        }
        if (input.pressed(Key::G)) {
            static const f32 kGridPresets[5] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
            i32 next = 0;
            for (i32 i = 0; i < 5; i++) {
                if (f_abs(editor.snap().pos - kGridPresets[i]) < 0.01f) {
                    next = (i + 1) % 5;
                    break;
                }
            }
            editor.snap().pos = kGridPresets[next];
        }
        if (input.pressed(Key::F)) {
            frame_view(viewport);
        }
        if (input.pressed(Key::V)) {
            vert_mode_ = !vert_mode_;
            vert_sel_ = -1;
            vert_drag_ = false;
        }
        if (input.pressed(Key::Delete) && selection_ >= 0
            && selection_ < static_cast<i32>(structure_.count)) {
            Brush& db = structure_.brushes[selection_];
            if (vert_mode_ && db.kind == BrushKind::Poly && vert_sel_ >= 0
                && vert_sel_ < static_cast<i32>(db.point_count) && db.point_count > 3) {
                push_undo(structure_);
                for (u32 i = static_cast<u32>(vert_sel_) + 1; i < db.point_count; i++) {
                    db.points[i - 1] = db.points[i];
                }
                db.point_count--;
                poly_normalize(db);
                vert_sel_ = -1;
                editor.status("deleted point");
            } else {
                push_undo(structure_);
                delete_selected(editor);
                editor.status("deleted brush");
            }
        }
        if (ctrl && input.pressed(Key::D) && selection_ >= 0
            && selection_ < static_cast<i32>(structure_.count)
            && structure_.count < kStructMaxBrushes) {
            push_undo(structure_);
            Brush copy = structure_.brushes[selection_];
            i32 ha = 0;
            i32 va = 0;
            f32 vdir = 1.0f;
            view_axes(ha, va, vdir);
            set_axis(copy.pos, ha, axis_of(copy.pos, ha) + step);
            set_axis(copy.pos, va, axis_of(copy.pos, va) + step);
            structure_.brushes[structure_.count] = copy;
            selection_ = static_cast<i32>(structure_.count);
            structure_.count++;
            material_sync_ = -1;
        }
        if (ctrl && input.pressed(Key::Z) && !mouse_busy) {
            undo(editor);
        }
        if (ctrl && input.pressed(Key::Y) && !mouse_busy) {
            redo(editor);
        }
        if (input.pressed(Key::Left)) {
            nudge(editor, -step, 0.0f);
        }
        if (input.pressed(Key::Right)) {
            nudge(editor, step, 0.0f);
        }
        if (input.pressed(Key::Up)) {
            nudge(editor, 0.0f, step);
        }
        if (input.pressed(Key::Down)) {
            nudge(editor, 0.0f, -step);
        }
        if (input.pressed(Key::Escape)) {
            if (drag_ || vert_drag_) {
                drag_ = 0;
                vert_drag_ = false;
                gesture_open_ = false;
            } else if (vert_sel_ >= 0) {
                vert_sel_ = -1;
            } else {
                selection_ = -1;
            }
        }
    }

    i32 ha = 0;
    i32 va = 0;
    f32 vdir = 1.0f;
    view_axes(ha, va, vdir);

    if (input.down(MouseButton::Right)) {
        pan_h_ -= input.mouse_delta().x / zoom_;
        pan_v_ -= vdir * input.mouse_delta().y / zoom_;
    }
    if (input.scroll() != 0.0f && !over_panel) {
        f32 before_h = 0.0f;
        f32 before_v = 0.0f;
        screen_to_world(input.mouse_pos().x, input.mouse_pos().y, viewport, before_h, before_v);
        zoom_ = f_clamp(zoom_ * std::pow(1.2f, input.scroll()), 2.0f, 200.0f);
        f32 after_h = 0.0f;
        f32 after_v = 0.0f;
        screen_to_world(input.mouse_pos().x, input.mouse_pos().y, viewport, after_h, after_v);
        pan_h_ += before_h - after_h;
        pan_v_ += before_v - after_v;
    }

    f32 wh = 0.0f;
    f32 wv = 0.0f;
    screen_to_world(input.mouse_pos().x, input.mouse_pos().y, viewport, wh, wv);
    const bool in_view = input.mouse_pos().x < view_w_;

    if (input.pressed(MouseButton::Left) && gesture_pre_) {
        *gesture_pre_ = structure_;
        gesture_open_ = true;
    }

    bool vert_consumed = false;
    if (vert_mode_ && view_ == BrushView::Top && input.pressed(MouseButton::Left) && !over_panel
        && in_view && !input.down(MouseButton::Right) && selection_ >= 0
        && selection_ < static_cast<i32>(structure_.count)) {
        Brush& b = structure_.brushes[selection_];
        Vec2 local[kBrushPolyMax];
        const u32 pc = brush_edit_points(b, local);

        i32 hit_vert = -1;
        for (u32 i = 0; i < pc; i++) {
            f32 h = 0.0f;
            f32 v = 0.0f;
            top_world(b, local[i].x, local[i].y, h, v);
            const Vec2 sp = world_to_screen(h, v, viewport);
            if (f_abs(input.mouse_pos().x - sp.x) < kBrushEdgePickPx + 2.0f
                && f_abs(input.mouse_pos().y - sp.y) < kBrushEdgePickPx + 2.0f) {
                hit_vert = static_cast<i32>(i);
                break;
            }
        }

        if (hit_vert >= 0) {
            if (b.kind == BrushKind::Box) {
                poly_from_box(b);
            }
            vert_sel_ = hit_vert;
            vert_drag_ = true;
            vert_consumed = true;
        } else if (ctrl && pc >= 3 && pc < kBrushPolyMax) {
            f32 best = kBrushEdgePickPx + 2.0f;
            i32 best_edge = -1;
            f32 best_t = 0.0f;
            const Vec2 mouse = input.mouse_pos();
            for (u32 i = 0; i < pc; i++) {
                f32 ah = 0.0f;
                f32 av = 0.0f;
                f32 bh = 0.0f;
                f32 bv = 0.0f;
                top_world(b, local[i].x, local[i].y, ah, av);
                top_world(b, local[(i + 1) % pc].x, local[(i + 1) % pc].y, bh, bv);
                const Vec2 sa = world_to_screen(ah, av, viewport);
                const Vec2 sb = world_to_screen(bh, bv, viewport);
                const Vec2 ab = sb - sa;
                const f32 denom = dot(ab, ab);
                const f32 t = denom > 1e-6f ? f_clamp01(dot(mouse - sa, ab) / denom) : 0.0f;
                const f32 d = length(mouse - (sa + ab * t));
                if (d < best) {
                    best = d;
                    best_edge = static_cast<i32>(i);
                    best_t = t;
                }
            }
            if (best_edge >= 0) {
                if (b.kind == BrushKind::Box) {
                    poly_from_box(b);
                }
                Vec2 np = lerp(b.points[best_edge],
                               b.points[(best_edge + 1) % b.point_count], best_t);
                np.x = snap_to(np.x, step);
                np.y = snap_to(np.y, step);

                const u32 at = static_cast<u32>(best_edge) + 1;
                for (u32 i = b.point_count; i > at; i--) {
                    b.points[i] = b.points[i - 1];
                }
                b.points[at] = np;
                b.point_count++;
                if (!poly_convex({b.points, b.point_count})) {
                    for (u32 i = at; i + 1 < b.point_count; i++) {
                        b.points[i] = b.points[i + 1];
                    }
                    b.point_count--;
                } else {
                    vert_sel_ = static_cast<i32>(at);
                    vert_drag_ = true;
                }
                vert_consumed = true;
            }
        }
    }

    if (vert_drag_) {
        vert_consumed = true;
        if (selection_ >= 0 && selection_ < static_cast<i32>(structure_.count)) {
            Brush& b = structure_.brushes[selection_];
            if (input.down(MouseButton::Left) && vert_sel_ >= 0
                && vert_sel_ < static_cast<i32>(b.point_count)) {
                f32 lx = 0.0f;
                f32 lz = 0.0f;
                top_local(b, wh, wv, lx, lz);
                const Vec2 old = b.points[vert_sel_];
                b.points[vert_sel_] = Vec2{snap_to(lx, step), snap_to(lz, step)};
                if (!poly_convex({b.points, b.point_count})) {
                    b.points[vert_sel_] = old;
                }
            }
            if (!input.down(MouseButton::Left)) {
                vert_drag_ = false;
                poly_normalize(b);
            }
        } else {
            vert_drag_ = false;
        }
    }

    if (!vert_consumed && input.pressed(MouseButton::Left) && !over_panel && in_view
        && !input.down(MouseButton::Right)) {
        drag_ = 0;
        if (!ctrl && !(vert_mode_ && view_ == BrushView::Top) && selection_ >= 0
            && selection_ < static_cast<i32>(structure_.count)) {
            i32 hh = 0;
            i32 hv = 0;
            if (handle_hit(structure_.brushes[selection_], viewport, input.mouse_pos(), hh, hv)) {
                drag_ = 3;
                resize_h_ = hh;
                resize_v_ = hv;
            }
        }
        if (!drag_ && !ctrl) {
            i32 hit = -1;
            for (i32 i = static_cast<i32>(structure_.count) - 1; i >= 0; i--) {
                if (view_contains(structure_.brushes[i], wh, wv)) {
                    hit = i;
                    break;
                }
            }
            if (hit >= 0) {
                if (hit != selection_) {
                    selection_ = hit;
                    material_sync_ = -1;
                    vert_sel_ = -1;
                    drag_ = -1;
                } else {
                    const Brush& b = structure_.brushes[hit];
                    grab_dh_ = wh - axis_of(b.pos, ha);
                    grab_dv_ = wv - axis_of(b.pos, va);
                    drag_ = 2;
                }
            }
        }
        if (!drag_) {
            selection_ = -1;
            drag_ = 1;
            draw_h0_ = snap_to(wh, step);
            draw_v0_ = snap_to(wv, step);
        }
    }

    if (drag_ && input.down(MouseButton::Left)) {
        if (drag_ == 2 && selection_ >= 0 && selection_ < static_cast<i32>(structure_.count)) {
            Brush& b = structure_.brushes[selection_];
            set_axis(b.pos, ha, snap_to(wh - grab_dh_, step));
            set_axis(b.pos, va, snap_to(wv - grab_dv_, step));
        } else if (drag_ == 3 && selection_ >= 0
                   && selection_ < static_cast<i32>(structure_.count)) {
            Brush& b = structure_.brushes[selection_];
            for (i32 pass = 0; pass < 2; pass++) {
                const i32 side = pass == 0 ? resize_h_ : resize_v_;
                if (side < 0) {
                    continue;
                }
                const i32 axis = pass == 0 ? ha : va;
                const f32 value = snap_to(pass == 0 ? wh : wv, step);
                const f32 center = axis_of(b.pos, axis);
                const f32 half = axis_of(b.size, axis) * 0.5f;
                f32 lo = center - half;
                f32 hi = center + half;
                if (side == 0) {
                    lo = f_min(value, hi - kBrushMinSize);
                } else {
                    hi = f_max(value, lo + kBrushMinSize);
                }
                set_axis(b.pos, axis, (lo + hi) * 0.5f);
                set_axis(b.size, axis, hi - lo);
                if (b.kind == BrushKind::Poly && axis != 1 && half > 1e-4f) {
                    const f32 ratio = (hi - lo) / (half * 2.0f);
                    for (u32 pi = 0; pi < b.point_count; pi++) {
                        if (axis == 0) {
                            b.points[pi].x *= ratio;
                        } else {
                            b.points[pi].y *= ratio;
                        }
                    }
                }
            }
        }
    }

    if (drag_ && !input.down(MouseButton::Left)) {
        if (drag_ == 1) {
            const f32 h1 = snap_to(wh, step);
            const f32 v1 = snap_to(wv, step);
            const f32 dh = f_abs(h1 - draw_h0_);
            const f32 dv = f_abs(v1 - draw_v0_);
            if (dh >= kBrushMinSize && dv >= kBrushMinSize
                && structure_.count < kStructMaxBrushes) {
                Brush& b = structure_.brushes[structure_.count];
                b = Brush{};
                b.kind = BrushKind::Box;
                b.material.assign("concrete");
                set_axis(b.pos, ha, (draw_h0_ + h1) * 0.5f);
                set_axis(b.pos, va, (draw_v0_ + v1) * 0.5f);
                set_axis(b.size, ha, dh);
                set_axis(b.size, va, dv);
                if (view_ == BrushView::Top) {
                    b.size.y = 3.0f;
                    b.pos.y = 1.5f;
                } else if (view_ == BrushView::Front) {
                    b.size.z = 4.0f;
                    b.pos.z = 0.0f;
                } else {
                    b.size.x = 4.0f;
                    b.pos.x = 0.0f;
                }
                selection_ = static_cast<i32>(structure_.count);
                material_sync_ = -1;
                structure_.count++;
            }
        }
        drag_ = 0;
    }

    if (gesture_open_ && !input.down(MouseButton::Left)) {
        gesture_open_ = false;
        if (gesture_pre_ && *gesture_pre_ != structure_) {
            push_undo(*gesture_pre_);
        }
    }

    (void)scratch;
}

void BrushEditor::line_clipped(DebugDraw& debug, Vec2 viewport, f32 x0, f32 y0, f32 x1, f32 y1,
                               u32 color) const
{
    f32 t0 = 0.0f;
    f32 t1 = 1.0f;
    const f32 dx = x1 - x0;
    const f32 dy = y1 - y0;
    const f32 p[4] = {-dx, dx, -dy, dy};
    const f32 q[4] = {x0, view_w_ - x0, y0, viewport.y - y0};

    for (i32 i = 0; i < 4; i++) {
        if (p[i] == 0.0f) {
            if (q[i] < 0.0f) {
                return;
            }
            continue;
        }
        const f32 r = q[i] / p[i];
        if (p[i] < 0.0f) {
            t0 = f_max(t0, r);
        } else {
            t1 = f_min(t1, r);
        }
    }
    if (t0 > t1) {
        return;
    }
    debug.line_2d(x0 + dx * t0, y0 + dy * t0, x0 + dx * t1, y0 + dy * t1, color);
}

void BrushEditor::sidebar(Editor& editor, Ui& ui, Arena& scratch, f32 px)
{
    static const char* kViewNames[3] = {"view: top (1)", "view: front (2)", "view: side (3)"};

    ui.panel_begin("structure", px, 16.0f, kBrushSidebarWidth - 16.0f);
    if (ui.list_item(kViewNames[static_cast<u32>(view_)], false)) {
        view_ = static_cast<BrushView>((static_cast<u32>(view_) + 1) % 3);
    }
    ui.slider("grid snap", editor.snap().pos, 0.0f, 5.0f);
    if (ui.text_field("name", name_buf_, sizeof(name_buf_)) || name_buf_[0]) {
        structure_.name.assign(name_buf_);
    }
    if (ui.button("save .struct")) {
        save(editor, scratch);
    }
    if (ui.button("bake .amsh")) {
        bake(editor, scratch);
    }
    if (ui.button("new structure")) {
        structure_ = Structure{};
        structure_.name.assign("structure");
        std::snprintf(name_buf_, sizeof(name_buf_), "%s", structure_.name.c_str());
        selection_ = -1;
        material_sync_ = -1;
    }
    if (file_count_ > kBrushListPage && ui.button("scroll files")) {
        file_scroll_ += kBrushListPage;
        if (file_scroll_ >= static_cast<i32>(file_count_)) {
            file_scroll_ = 0;
        }
    }
    for (i32 i = file_scroll_;
         i < static_cast<i32>(file_count_) && i < file_scroll_ + kBrushListPage; i++) {
        if (ui.list_item(files_[i].view(), files_[i] == structure_.name)) {
            load(editor, scratch, files_[i].view());
        }
    }
    ui.label("undo %u | redo %u", undo_count_, redo_count_);
    if (ui.button("undo (ctrl+z)")) {
        undo(editor);
    }
    if (ui.button("redo (ctrl+y)")) {
        redo(editor);
    }
    if (ui.button("exit structure mode")) {
        editor.set_brush_mode(false);
        selection_ = -1;
        drag_ = 0;
    }
    if (editor.status_time() > 0.0f) {
        ui.label("%.*s", static_cast<int>(editor.status_text().size()),
                 editor.status_text().data());
    }
    ui.panel_end();

    ui.panel_begin("brushes", px, 470.0f, kBrushSidebarWidth - 16.0f);
    ui.label("brushes: %u  (drag to draw)", structure_.count);
    if (ui.button("add wedge")) {
        add_brush(editor, BrushKind::Wedge);
    }

    if (selection_ >= 0 && selection_ < static_cast<i32>(structure_.count)) {
        Brush& b = structure_.brushes[selection_];
        const char* kind_name = b.kind == BrushKind::Wedge ? "wedge"
                                : (b.kind == BrushKind::Poly ? "poly" : "box");
        if (b.kind == BrushKind::Poly) {
            ui.label("sel: poly %u pts %s", b.point_count, b.subtract ? "(carve)" : "(solid)");
        } else {
            ui.label("sel: %s %s", kind_name, b.subtract ? "(carve)" : "(solid)");
        }

        if (b.kind != BrushKind::Wedge) {
            bool vm = vert_mode_;
            if (ui.checkbox("edit points (v)", vm)) {
                vert_mode_ = vm;
                vert_sel_ = -1;
                vert_drag_ = false;
            }
            if (vert_mode_) {
                ui.label("top view: drag points");
                ui.label("ctrl+click edge adds point");
            }
        }

        bool sub = b.subtract;
        if (ui.checkbox("carve (subtract)", sub)) {
            b.subtract = sub;
        }
        if (material_sync_ != selection_) {
            material_sync_ = selection_;
            std::snprintf(material_buf_, sizeof(material_buf_), "%s", b.material.c_str());
        }
        if (ui.text_field("material (enter)", material_buf_, sizeof(material_buf_))
            && material_buf_[0] && b.material != material_buf_) {
            push_undo(structure_);
            b.material.assign(material_buf_);
        }

        f32 yaw_deg = b.yaw * kRadToDeg;
        if (ui.slider("yaw", yaw_deg, -180.0f, 180.0f)) {
            b.yaw = yaw_deg * kDegToRad;
        }
        if (b.kind == BrushKind::Box && !b.subtract) {
            ui.slider("hollow walls", b.hollow, 0.0f, 1.0f);
        }
        ui.label("pos %.2f %.2f %.2f", static_cast<f64>(b.pos.x), static_cast<f64>(b.pos.y),
                 static_cast<f64>(b.pos.z));
        ui.label("size %.2f %.2f %.2f", static_cast<f64>(b.size.x), static_cast<f64>(b.size.y),
                 static_cast<f64>(b.size.z));

        if (ui.button("duplicate brush") && structure_.count < kStructMaxBrushes) {
            Brush copy = b;
            copy.pos += Vec3{1.0f, 0.0f, 1.0f};
            structure_.brushes[structure_.count] = copy;
            selection_ = static_cast<i32>(structure_.count);
            structure_.count++;
            material_sync_ = -1;
        }
        if (ui.button("delete brush (del)")) {
            delete_selected(editor);
        }
    } else {
        ui.label("drag empty grid: draw box");
        ui.label("ctrl+drag: draw over brushes");
        ui.label("drag handles: resize");
        ui.label("arrows: nudge | del: delete");
        ui.label("f: frame | g: cycle grid");
        ui.label("rmb pan | scroll zoom | esc");
    }
    ui.panel_end();
}

void BrushEditor::render(Editor& editor, Ui& ui, DebugDraw& debug, TextRenderer& text,
                         const Input& input, Arena& scratch, Vec2 viewport)
{
    (void)text;
    view_w_ = viewport.x - kBrushSidebarWidth - 16.0f;
    const f32 px = view_w_ + 8.0f;
    const f32 step = snap_step(editor);

    debug.rect_2d_filled(0.0f, 0.0f, viewport.x, viewport.y, dd_rgba(11, 13, 17, 255));

    f32 draw_step = step;
    while (draw_step * zoom_ < 8.0f) {
        draw_step *= 4.0f;
    }

    f32 h_min = 0.0f;
    f32 v_a = 0.0f;
    f32 h_max = 0.0f;
    f32 v_b = 0.0f;
    screen_to_world(0.0f, 0.0f, viewport, h_min, v_a);
    screen_to_world(view_w_, viewport.y, viewport, h_max, v_b);
    const f32 v_min = f_min(v_a, v_b);
    const f32 v_max = f_max(v_a, v_b);

    for (f32 h = std::floor(h_min / draw_step) * draw_step; h <= h_max; h += draw_step) {
        const Vec2 s = world_to_screen(h, 0.0f, viewport);
        const bool major = f_abs(h - std::floor(h / (draw_step * 8.0f) + 0.5f) * draw_step * 8.0f)
                         < draw_step * 0.5f;
        if (s.x >= 0.0f && s.x <= view_w_) {
            debug.line_2d(s.x, 0.0f, s.x, viewport.y,
                          major ? dd_rgba(52, 60, 72, 255) : dd_rgba(28, 33, 41, 255));
        }
    }
    for (f32 v = std::floor(v_min / draw_step) * draw_step; v <= v_max; v += draw_step) {
        const Vec2 s = world_to_screen(0.0f, v, viewport);
        const bool major = f_abs(v - std::floor(v / (draw_step * 8.0f) + 0.5f) * draw_step * 8.0f)
                         < draw_step * 0.5f;
        if (s.y >= 0.0f && s.y <= viewport.y) {
            debug.line_2d(0.0f, s.y, view_w_, s.y,
                          major ? dd_rgba(52, 60, 72, 255) : dd_rgba(28, 33, 41, 255));
        }
    }

    const Vec2 origin = world_to_screen(0.0f, 0.0f, viewport);
    if (origin.x >= 0.0f && origin.x <= view_w_) {
        debug.line_2d(origin.x, 0.0f, origin.x, viewport.y, dd_rgba(70, 110, 80, 255));
    }
    if (origin.y >= 0.0f && origin.y <= viewport.y) {
        debug.line_2d(0.0f, origin.y, view_w_, origin.y, dd_rgba(110, 70, 70, 255));
    }

    for (u32 i = 0; i < structure_.count; i++) {
        const Brush& b = structure_.brushes[i];
        const bool is_sel = static_cast<i32>(i) == selection_;
        const u32 color = is_sel ? kDdYellow : (b.subtract ? kDdRed : kDdGreen);
        const bool poly_top = view_ == BrushView::Top && b.kind == BrushKind::Poly
                           && b.point_count >= 3;

        Vec2 corners[4];
        view_corners(b, viewport, corners);

        if (is_sel && !poly_top && !(view_ == BrushView::Top && b.yaw != 0.0f)) {
            const f32 x0 = f_min(f_min(corners[0].x, corners[1].x),
                                 f_min(corners[2].x, corners[3].x));
            const f32 x1 = f_max(f_max(corners[0].x, corners[1].x),
                                 f_max(corners[2].x, corners[3].x));
            const f32 y0 = f_min(f_min(corners[0].y, corners[1].y),
                                 f_min(corners[2].y, corners[3].y));
            const f32 y1 = f_max(f_max(corners[0].y, corners[1].y),
                                 f_max(corners[2].y, corners[3].y));
            debug.rect_2d_filled(f_max(x0, 0.0f), f_max(y0, 0.0f), f_min(x1, view_w_),
                                 f_min(y1, viewport.y),
                                 b.subtract ? dd_rgba(120, 40, 40, 50)
                                            : dd_rgba(120, 120, 40, 50));
        }

        if (poly_top) {
            for (u32 k = 0; k < b.point_count; k++) {
                const Vec2 pa = b.points[k];
                const Vec2 pb = b.points[(k + 1) % b.point_count];
                f32 ah = 0.0f;
                f32 av = 0.0f;
                f32 bh = 0.0f;
                f32 bv = 0.0f;
                top_world(b, pa.x, pa.y, ah, av);
                top_world(b, pb.x, pb.y, bh, bv);
                const Vec2 sa = world_to_screen(ah, av, viewport);
                const Vec2 sb = world_to_screen(bh, bv, viewport);
                line_clipped(debug, viewport, sa.x, sa.y, sb.x, sb.y, color);
            }
        } else {
            for (i32 k = 0; k < 4; k++) {
                const Vec2 a = corners[k];
                const Vec2 c = corners[(k + 1) % 4];
                line_clipped(debug, viewport, a.x, a.y, c.x, c.y, color);
            }
        }

        if (b.kind == BrushKind::Wedge) {
            line_clipped(debug, viewport, corners[0].x, corners[0].y, corners[2].x, corners[2].y,
                         color);
        }
        if (b.kind == BrushKind::Box && !b.subtract && b.hollow > 0.0f) {
            Brush inner = b;
            inner.size = b.size - Vec3{1.0f, 1.0f, 1.0f} * (b.hollow * 2.0f);
            if (inner.size.x > 0.05f && inner.size.y > 0.05f && inner.size.z > 0.05f) {
                Vec2 ic[4];
                view_corners(inner, viewport, ic);
                const u32 inner_color = is_sel ? dd_rgba(200, 200, 90, 255)
                                               : dd_rgba(60, 120, 60, 255);
                for (i32 k = 0; k < 4; k++) {
                    line_clipped(debug, viewport, ic[k].x, ic[k].y, ic[(k + 1) % 4].x,
                                 ic[(k + 1) % 4].y, inner_color);
                }
            }
        }

        if (!is_sel) {
            continue;
        }

        if (vert_mode_ && view_ == BrushView::Top && b.kind != BrushKind::Wedge) {
            Vec2 local[kBrushPolyMax];
            const u32 pc = brush_edit_points(b, local);
            for (u32 k = 0; k < pc; k++) {
                f32 h = 0.0f;
                f32 v = 0.0f;
                top_world(b, local[k].x, local[k].y, h, v);
                const Vec2 sp = world_to_screen(h, v, viewport);
                if (sp.x < 0.0f || sp.x > view_w_) {
                    continue;
                }
                const bool vsel = b.kind == BrushKind::Poly && static_cast<i32>(k) == vert_sel_;
                const f32 r = vsel ? 5.0f : 3.5f;
                debug.rect_2d_filled(sp.x - r, sp.y - r, sp.x + r, sp.y + r,
                                     vsel ? kDdWhite : kDdCyan);
            }
        } else {
            Vec2 pts[8];
            i32 hs[8];
            i32 vs[8];
            const u32 n = handle_points(b, viewport, pts, hs, vs);
            for (u32 k = 0; k < n; k++) {
                if (pts[k].x < 0.0f || pts[k].x > view_w_) {
                    continue;
                }
                debug.rect_2d_filled(pts[k].x - 3.0f, pts[k].y - 3.0f, pts[k].x + 3.0f,
                                     pts[k].y + 3.0f, kDdYellow);
            }
        }

        i32 ha = 0;
        i32 va = 0;
        f32 vdir = 1.0f;
        view_axes(ha, va, vdir);
        const Vec2 center = world_to_screen(axis_of(b.pos, ha), axis_of(b.pos, va), viewport);
        if (center.x >= 0.0f && center.x <= view_w_ - 90.0f) {
            debug.text_2d(text, center.x + 6.0f, center.y - 6.0f, 13.0f, kDdYellow,
                          "%.4g x %.4g", static_cast<f64>(axis_of(b.size, ha)),
                          static_cast<f64>(axis_of(b.size, va)));
        }
    }

    f32 wh = 0.0f;
    f32 wv = 0.0f;
    screen_to_world(input.mouse_pos().x, input.mouse_pos().y, viewport, wh, wv);

    if (drag_ == 1) {
        const f32 h1 = snap_to(wh, step);
        const f32 v1 = snap_to(wv, step);
        const Vec2 a = world_to_screen(draw_h0_, draw_v0_, viewport);
        const Vec2 c = world_to_screen(h1, v1, viewport);
        line_clipped(debug, viewport, a.x, a.y, c.x, a.y, kDdCyan);
        line_clipped(debug, viewport, c.x, a.y, c.x, c.y, kDdCyan);
        line_clipped(debug, viewport, c.x, c.y, a.x, c.y, kDdCyan);
        line_clipped(debug, viewport, a.x, c.y, a.x, a.y, kDdCyan);
        debug.text_2d(text, f_min(c.x + 8.0f, view_w_ - 90.0f), c.y - 8.0f, 13.0f, kDdCyan,
                      "%.4g x %.4g", static_cast<f64>(f_abs(h1 - draw_h0_)),
                      static_cast<f64>(f_abs(v1 - draw_v0_)));
    }

    static const char* kAxisNames[3] = {"x/z", "x/y", "z/y"};
    debug.text_2d(text, 12.0f, viewport.y - 14.0f, 14.0f, kDdGray,
                  "%s  %.2f / %.2f  grid %.2g  zoom %.0f", kAxisNames[static_cast<u32>(view_)],
                  static_cast<f64>(snap_to(wh, step)), static_cast<f64>(snap_to(wv, step)),
                  static_cast<f64>(draw_step), static_cast<f64>(zoom_));

    sidebar(editor, ui, scratch, px);
}

} // namespace anom
