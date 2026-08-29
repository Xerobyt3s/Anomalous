#pragma once

#include "core/fixed_string.h"
#include "core/types.h"
#include "editor/brush.h"
#include "math/vmath.h"

#include <string_view>

namespace anom {

class Arena;
class DebugDraw;
class Editor;
class Input;
class InputContext;
class TextRenderer;
class Ui;

inline constexpr u32 kStructMaxFiles = 64;
inline constexpr u32 kBrushUndoMax = 32;
inline constexpr i32 kBrushListPage = 8;
inline constexpr f32 kBrushSidebarWidth = 276.0f;
inline constexpr f32 kBrushEdgePickPx = 7.0f;

enum class BrushView : u32 {
    Top,
    Front,
    Side,
};

bool operator==(const Brush& a, const Brush& b);
bool operator==(const Structure& a, const Structure& b);
inline bool operator!=(const Structure& a, const Structure& b) { return !(a == b); }

class BrushEditor {
public:
    void init(Arena& storage);
    void reset(Arena& scratch);

    void update(Editor& editor, const Input& input, const InputContext& ctx, Arena& scratch,
                Vec2 viewport);
    void render(Editor& editor, Ui& ui, DebugDraw& debug, TextRenderer& text, const Input& input,
                Arena& scratch, Vec2 viewport);

    Structure& structure() { return structure_; }
    const Structure& structure() const { return structure_; }
    i32 selection() const { return selection_; }
    void set_selection(i32 index) { selection_ = index; }
    BrushView view() const { return view_; }
    u32 undo_depth() const { return undo_count_; }
    u32 redo_depth() const { return redo_count_; }

    bool save(Editor& editor, Arena& scratch);
    bool load(Editor& editor, Arena& scratch, std::string_view name);
    bool bake(Editor& editor, Arena& scratch);

    void push_undo(const Structure& pre);
    void undo(Editor& editor);
    void redo(Editor& editor);

private:
    void scan_files();
    void view_axes(i32& out_h, i32& out_v, f32& out_vdir) const;
    Vec2 world_to_screen(f32 h, f32 v, Vec2 viewport) const;
    void screen_to_world(f32 sx, f32 sy, Vec2 viewport, f32& out_h, f32& out_v) const;
    f32 snap_step(const Editor& editor) const;

    void add_brush(Editor& editor, BrushKind kind);
    void delete_selected(Editor& editor);
    void nudge(Editor& editor, f32 dh, f32 dv);
    void frame_view(Vec2 viewport);

    bool view_contains(const Brush& b, f32 h, f32 v) const;
    u32 handle_points(const Brush& b, Vec2 viewport, Vec2* out_pts, i32* out_h, i32* out_v) const;
    bool handle_hit(const Brush& b, Vec2 viewport, Vec2 mouse, i32& out_h, i32& out_v) const;
    void view_corners(const Brush& b, Vec2 viewport, Vec2 out[4]) const;

    void sidebar(Editor& editor, Ui& ui, Arena& scratch, f32 px);
    void line_clipped(DebugDraw& debug, Vec2 viewport, f32 x0, f32 y0, f32 x1, f32 y1,
                      u32 color) const;

    Structure structure_;
    Structure* undo_stack_ = nullptr;
    Structure* redo_stack_ = nullptr;
    u32 undo_count_ = 0;
    u32 redo_count_ = 0;
    Structure* gesture_pre_ = nullptr;
    bool gesture_open_ = false;

    FixedString<32> files_[kStructMaxFiles];
    u32 file_count_ = 0;
    i32 file_scroll_ = 0;

    char name_buf_[32] = {};
    char material_buf_[32] = {};
    i32 material_sync_ = -1;

    BrushView view_ = BrushView::Top;
    f32 pan_h_ = 0.0f;
    f32 pan_v_ = 0.0f;
    f32 zoom_ = 24.0f;
    f32 view_w_ = 0.0f;

    i32 selection_ = -1;
    i32 drag_ = 0;
    f32 draw_h0_ = 0.0f;
    f32 draw_v0_ = 0.0f;
    f32 grab_dh_ = 0.0f;
    f32 grab_dv_ = 0.0f;
    i32 resize_h_ = -1;
    i32 resize_v_ = -1;

    bool vert_mode_ = false;
    i32 vert_sel_ = -1;
    bool vert_drag_ = false;
};

} // namespace anom
