#pragma once

#include "terminal/program.h"

namespace anom {

class Arena;

inline constexpr u32 kTravelGridU = 37;
inline constexpr u32 kTravelGridV = 23;
inline constexpr u32 kSubdivisions = 4;

class TravelProgram : public Program {
public:
    void init(Arena& arena);

    void enter(const ProgramContext& ctx, const TermView& view) override;
    void update(const ProgramContext& ctx, const TermView& view, f32 dt) override;
    bool key(const ProgramContext& ctx, const TermView& view, TermKey key) override;
    void key_char(const ProgramContext& ctx, const TermView& view, char c) override;

private:
    enum class Stage : u32 {
        Select,
        Plot,
        Spool,
    };

    void draw_select(const ProgramContext& ctx, const TermView& view);
    void draw_plot(const ProgramContext& ctx);
    void draw_spool(const ProgramContext& ctx, const TermView& view);

    struct Well {
        Vec2 centre;
        Vec3 axis;
        f32 depth = 0.0f;
    };

    struct Sheet {
        Well wells[2];
        f32 bend = 0.0f;
    };

    void build_sheet(f32 bend, f32 well, f32 reveal, f32 throat);
    void push_line(Vec3 a, Vec3 b, f32 shade);
    Sheet make_sheet(f32 bend, f32 well, f32 throat) const;
    Vec3 surface(f32 u, f32 v, const Sheet& sheet) const;
    void build_coil(f32 charge, f32 spooling);
    void set_camera(const ProgramContext& ctx, f32 bend) const;

    TermPoint* lines_ = nullptr;
    u32 line_count_ = 0;

    Stage stage_ = Stage::Select;
    f32 stage_time_ = 0.0f;
    f32 orbit_ = 0.0f;
    f32 coil_spin_ = 0.0f;
    f32 coil_view_ = 0.0f;
    u32 arc_seed_ = 1u;
    i32 selected_ = 0;
    i32 target_ = -1;
};

} // namespace anom
