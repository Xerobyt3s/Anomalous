#include "terminal/programs/travel.h"
#include "core/arena.h"
#include "terminal/screen.h"
#include "terminal/term_font.h"

namespace anom {
namespace {
inline constexpr u16 kTermBlockGlyph = 127;
}
}
#include "world/destination.h"

#include <cmath>

namespace anom {
namespace {

// The sheet is folded into a hairpin: a quarter-turn arc at the crown and two straight
// legs hanging parallel below it, so the two mass wells end up facing each other across
// the gap. Each well is a funnel of revolution rather than a dent, which is what lets the
// two of them grow toward each other and meet as one continuous throat.
constexpr f32 kSheetHalfLength = 9.0f;
constexpr f32 kSheetHalfDepth = 3.0f;
constexpr f32 kArcFraction = 0.52f;
constexpr f32 kWellU = 0.72f;
constexpr f32 kWellRadius = 1.55f;
constexpr f32 kNeckRadius = 0.55f;
// Fraction of the well disc that maps to straight bore rather than flare. Without it the
// two funnels meet at a circle and the throat has no length.
constexpr f32 kBoreFraction = 0.60f;
constexpr f32 kWellDepth = 0.90f;

constexpr f32 kRevealEnd = 1.2f;
constexpr f32 kFoldStart = 1.3f;
constexpr f32 kFoldEnd = 4.2f;
constexpr f32 kThroatStart = 3.6f;
constexpr f32 kThroatEnd = 5.4f;
constexpr f32 kPlotEnd = 7.0f;

constexpr f32 kSpoolSpeedKmh = 90.0f;

struct Frame {
    Vec3 pos;
    Vec3 normal;
};

f32 ramp(f32 t, f32 from, f32 to)
{
    if (to <= from) {
        return t >= to ? 1.0f : 0.0f;
    }
    return f_clamp01((t - from) / (to - from));
}

// Grid lines crowd toward the middle of the strip, which is where both wells sit, so the
// funnels get their circumferential resolution without doubling the line count.
f32 grid_v(f32 s)
{
    const f32 a = f_abs(s);
    return (s < 0.0f ? -1.0f : 1.0f) * std::pow(a, 1.35f);
}

f32 smooth_step(f32 t)
{
    t = f_clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

f32 ease(f32 t)
{
    return t * t * (3.0f - 2.0f * t);
}

// Walks the sheet from the crown outward: an arc of turn `half_turn` over the first
// `kArcFraction` of the length, then a straight leg along the arc's exit tangent.
Frame fold_frame(f32 u, f32 v, f32 bend)
{
    const f32 half_turn = f_max(bend * kPi * 0.5f, 1e-4f);
    const f32 arc_len = kArcFraction * kSheetHalfLength;
    const f32 radius = arc_len / half_turn;
    const f32 side = u < 0.0f ? -1.0f : 1.0f;
    const f32 s = f_abs(u) * kSheetHalfLength;

    const f32 a = f_min(s, arc_len) / radius;
    const f32 sin_a = std::sin(a);
    const f32 cos_a = std::cos(a);

    f32 x = radius * sin_a;
    f32 y = radius * cos_a - radius;
    if (s > arc_len) {
        const f32 leg = s - arc_len;
        x += cos_a * leg;
        y += -sin_a * leg;
    }

    return Frame{Vec3{side * x, y, v * kSheetHalfDepth}, Vec3{side * sin_a, cos_a, 0.0f}};
}

} // namespace

void TravelProgram::init(Arena& arena)
{
    lines_ = arena.push_array<TermPoint>(kTermMaxLineVerts);
}

void TravelProgram::push_line(Vec3 a, Vec3 b, f32 shade)
{
    if (!lines_ || line_count_ + 2 > kTermMaxLineVerts) {
        return;
    }
    lines_[line_count_++] = TermPoint{a.x, a.y, a.z, shade};
    lines_[line_count_++] = TermPoint{b.x, b.y, b.z, shade};
}

// Pulls the sheet itself into a funnel wherever it falls inside a mass well: the point is
// pushed outward in sheet space so the centre never collapses to nothing, and sunk along
// the well's axis. Rim displacement and its slope are both zero, so the funnel and the
// flat sheet are one surface with no seam to hide. When two funnels sink far enough that
// their necks land on the same circle, the sheet has become a throat -- there is no
// separate tube spliced in.
Vec3 TravelProgram::surface(f32 u, f32 v, const Sheet& sheet) const
{
    Vec2 p{u * kSheetHalfLength, v * kSheetHalfDepth};
    Vec3 sink{0.0f, 0.0f, 0.0f};

    for (const Well& well : sheet.wells) {
        const Vec2 d{p.x - well.centre.x, p.y - well.centre.y};
        const f32 rho = std::sqrt(d.x * d.x + d.y * d.y);
        if (rho >= kWellRadius) {
            continue;
        }
        const f32 t = smooth_step(rho / kWellRadius);
        const f32 flare = smooth_step((t - kBoreFraction) / (1.0f - kBoreFraction));
        const f32 pulled = f_lerp(kNeckRadius, kWellRadius, flare);
        const f32 inv = rho > 1e-4f ? pulled / rho : 0.0f;
        p = rho > 1e-4f ? Vec2{well.centre.x + d.x * inv, well.centre.y + d.y * inv}
                        : Vec2{well.centre.x + pulled, well.centre.y};
        sink = sink + well.axis * (well.depth * (1.0f - t));
    }

    return fold_frame(p.x / kSheetHalfLength, p.y / kSheetHalfDepth, sheet.bend).pos + sink;
}

TravelProgram::Sheet TravelProgram::make_sheet(f32 bend, f32 well, f32 throat) const
{
    Sheet sheet;
    sheet.bend = bend;

    const Frame a = fold_frame(-kWellU, 0.0f, bend);
    const Frame b = fold_frame(kWellU, 0.0f, bend);
    const f32 gap = distance(a.pos, b.pos);
    const f32 depth = f_min(f_lerp(kWellDepth * well, gap * 0.5f, throat), gap * 0.5f);

    sheet.wells[0] = Well{Vec2{-kWellU * kSheetHalfLength, 0.0f}, a.normal * -1.0f, depth};
    sheet.wells[1] = Well{Vec2{kWellU * kSheetHalfLength, 0.0f}, b.normal * -1.0f, depth};
    return sheet;
}

void TravelProgram::build_sheet(f32 bend, f32 well, f32 reveal, f32 throat)
{
    line_count_ = 0;
    const Sheet sheet = make_sheet(bend, well, throat);

    const f32 span_u = static_cast<f32>(kTravelGridU - 1);
    const f32 span_v = static_cast<f32>(kTravelGridV - 1);
    const f32 step = 1.0f / static_cast<f32>(kSubdivisions);

    for (u32 iv = 0; iv < kTravelGridV; iv++) {
        const f32 v = grid_v(static_cast<f32>(iv) / span_v * 2.0f - 1.0f);
        Vec3 prev = surface(-reveal, v, sheet);
        for (u32 seg = 1; seg <= kTravelGridU * kSubdivisions; seg++) {
            const f32 u = f_lerp(-reveal, reveal,
                                 static_cast<f32>(seg)
                                     / static_cast<f32>(kTravelGridU * kSubdivisions));
            const Vec3 next = surface(u, v, sheet);
            push_line(prev, next, 0.12f);
            prev = next;
        }
    }

    for (u32 iu = 0; iu < kTravelGridU; iu++) {
        const f32 u = (static_cast<f32>(iu) / span_u * 2.0f - 1.0f) * reveal;
        Vec3 prev = surface(u, -1.0f, sheet);
        for (u32 seg = 1; seg <= kTravelGridV * kSubdivisions; seg++) {
            const f32 v = grid_v(f_lerp(-1.0f, 1.0f,
                                        static_cast<f32>(seg)
                                            / static_cast<f32>(kTravelGridV * kSubdivisions)));
            const Vec3 next = surface(u, v, sheet);
            push_line(prev, next, 0.12f);
            prev = next;
        }
    }

    (void)step;
}

void TravelProgram::set_camera(const ProgramContext& ctx, f32 bend) const
{
    // Swing about the profile view rather than orbiting past it -- edge on, the hairpin
    // reads as a tube.
    const f32 sway = std::sin(orbit_) * 0.55f;
    const f32 pitch = f_lerp(0.42f, 0.30f, bend);
    const f32 dist = f_lerp(17.5f, 20.5f, bend);
    const Vec3 focus{0.0f, f_lerp(0.0f, -3.6f, bend), 0.0f};
    const Vec3 eye{std::sin(sway) * std::cos(pitch) * dist, std::sin(pitch) * dist,
                   std::cos(sway) * std::cos(pitch) * dist};
    const Mat4 proj = mat4_perspective(34.0f * kDegToRad,
                                       static_cast<f32>(kTermTexW)
                                           / static_cast<f32>(kTermTexH),
                                       0.5f, 90.0f);
    ctx.scene->vp3d = proj * mat4_look_at(eye + focus, focus, Vec3{0.0f, 1.0f, 0.0f});
}

void TravelProgram::enter(const ProgramContext& ctx, const TermView& view)
{
    (void)ctx;
    (void)view;
    stage_ = Stage::Select;
    stage_time_ = 0.0f;
    orbit_ = 0.0f;
    target_ = -1;
    line_count_ = 0;
}

void TravelProgram::draw_select(const ProgramContext& ctx, const TermView& view)
{
    Screen& s = *ctx.screen;
    s.grid_clear();
    s.grid_title("TRANSIT PLOTTER");

    s.grid_text(2, 2, TC_DIM, "ORIGIN   REDLINE FLATS");
    s.grid_text(2, 40, view.travel_ready ? TC_GREEN : TC_AMBER, "COIL %s",
                view.travel_ready ? "ARMED" : "NOT INSTALLED");

    const std::span<const Destination> all = destinations();
    for (u32 i = 0; i < all.size(); i++) {
        const Destination& d = all[i];
        const i32 row = 5 + static_cast<i32>(i) * 2;
        const bool sel = static_cast<i32>(i) == selected_;
        if (sel) {
            s.grid_text(row, 1, TC_BRIGHT, ">");
        }
        const u8 color = d.surveyed() ? (sel ? TC_BRIGHT : TC_GREEN) : TC_DIM;
        s.grid_text(row, 3, color, "%-18.*s", static_cast<int>(d.name.size()), d.name.data());
        if (d.range_km <= 0.0f) {
            s.grid_text(row, 23, TC_DIM, "CURRENT POSITION");
        } else {
            s.grid_text(row, 23, color, "%6.0f KM", static_cast<f64>(d.range_km));
            s.grid_text(row, 33, d.surveyed() ? TC_GREEN : TC_AMBER,
                        d.surveyed() ? "SURVEYED" : "UNSURVEYED");
        }
        if (sel) {
            s.grid_text(row + 1, 5, TC_DIM, "%.*s", static_cast<int>(d.note.size()),
                        d.note.data());
        }
    }

    s.grid_text(static_cast<i32>(kTermRows) - 1, 1, TC_DIM,
                "[UP/DN] SELECT   [ENTER] PLOT COURSE   [Q] EXIT");
}

void TravelProgram::draw_plot(const ProgramContext& ctx)
{
    const f32 t = stage_time_;
    const f32 reveal = ramp(t, 0.0f, kRevealEnd) * 1.02f;
    const f32 well = ease(ramp(t, 0.35f, 1.4f));
    const f32 bend = ease(ramp(t, kFoldStart, kFoldEnd));
    const f32 throat = ease(ramp(t, kThroatStart, kThroatEnd));

    build_sheet(bend, well, reveal, throat);
    ctx.scene->lines = lines_;
    ctx.scene->line_vertex_count = line_count_;
    set_camera(ctx, bend);

    const std::span<const Destination> all = destinations();
    const Destination& d = all[static_cast<u32>(target_)];

    Screen& s = *ctx.screen;
    s.grid_clear();
    s.grid_title("WORMHOLE PLOT");

    s.grid_text(2, 2, TC_GREEN, "REDLINE FLATS  -->  %.*s", static_cast<int>(d.name.size()),
                d.name.data());
    s.grid_text(3, 2, TC_DIM, "PROPER DISTANCE %6.0f KM   THROAT LENGTH %4.1f M",
                static_cast<f64>(d.range_km), static_cast<f64>(0.4f + throat * 3.1f));

    const char* status = "MAPPING LOCAL METRIC";
    if (t > kThroatEnd) {
        status = "THROAT STABLE";
    } else if (t > kThroatStart) {
        status = "OPENING THROAT";
    } else if (t > kFoldStart) {
        status = "FOLDING MANIFOLD";
    } else if (t > 0.35f) {
        status = "SEATING MASS WELLS";
    }
    const u8 color = t > kThroatEnd ? TC_BRIGHT : TC_AMBER;
    if (t > kThroatEnd || std::fmod(ctx.blink, 0.7f) < 0.45f) {
        s.grid_text(static_cast<i32>(kTermRows) - 3, 2, color, "%s", status);
    }
    s.grid_text(static_cast<i32>(kTermRows) - 1, 1, TC_DIM,
                "[ENTER] SKIP   [Q] ABORT PLOT");
}

void TravelProgram::build_coil(f32 charge, f32 spooling)
{
    line_count_ = 0;

    // Primary stack: a tapering solenoid the charge climbs.
    constexpr u32 kTurns = 26;
    constexpr u32 kSegs = 14;
    const f32 lit = charge * static_cast<f32>(kTurns);

    Vec3 prev{0.0f, 0.0f, 0.0f};
    for (u32 turn = 0; turn <= kTurns; turn++) {
        const f32 ft = static_cast<f32>(turn) / static_cast<f32>(kTurns);
        const f32 radius = f_lerp(1.55f, 0.85f, ft);
        const f32 y = f_lerp(-2.7f, 1.5f, ft);
        const f32 shade = turn <= static_cast<u32>(lit) ? 0.55f + 0.45f * spooling : 0.06f;
        for (u32 s = 0; s <= kSegs; s++) {
            const f32 a = static_cast<f32>(s) / static_cast<f32>(kSegs) * kTau
                        + ft * kTau * 0.5f + coil_spin_;
            const Vec3 p{std::cos(a) * radius, y + static_cast<f32>(s) * (4.2f / kTurns / kSegs),
                         std::sin(a) * radius};
            if (turn != 0 || s != 0) {
                push_line(prev, p, shade);
            }
            prev = p;
        }
    }

    // Toroid at the crown; it fattens and brightens as the coil fills.
    constexpr u32 kMajor = 18;
    constexpr u32 kMinor = 8;
    const f32 major = 1.85f;
    const f32 minor = f_lerp(0.28f, 0.60f, charge);
    Vec3 torus[kMajor][kMinor];
    for (u32 i = 0; i < kMajor; i++) {
        const f32 a = static_cast<f32>(i) / static_cast<f32>(kMajor) * kTau;
        for (u32 j = 0; j < kMinor; j++) {
            const f32 b = static_cast<f32>(j) / static_cast<f32>(kMinor) * kTau;
            const f32 r = major + std::cos(b) * minor;
            torus[i][j] = Vec3{std::cos(a) * r, 2.1f + std::sin(b) * minor, std::sin(a) * r};
        }
    }
    const f32 torus_shade = 0.35f + 0.65f * charge;
    for (u32 i = 0; i < kMajor; i++) {
        for (u32 j = 0; j < kMinor; j++) {
            push_line(torus[i][j], torus[i][(j + 1) % kMinor], torus_shade);
            push_line(torus[i][j], torus[(i + 1) % kMajor][j], torus_shade * 0.7f);
        }
    }

    // Arcs off the toroid. Count tracks charge so the screen gets busier as it fills.
    const u32 arcs = static_cast<u32>(charge * 7.0f) + (spooling > 0.5f ? 2u : 0u);
    for (u32 arc = 0; arc < arcs; arc++) {
        u32 seed = arc_seed_ + arc * 2654435761u;
        auto rnd = [&seed]() {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            return static_cast<f32>(seed & 0xFFFFFFu) / 16777216.0f;
        };
        const f32 a = rnd() * kTau;
        Vec3 tip{std::cos(a) * (major + minor), 2.1f, std::sin(a) * (major + minor)};
        const Vec3 aim{std::cos(a) * 3.6f, f_lerp(-1.0f, 3.4f, rnd()), std::sin(a) * 3.6f};
        for (u32 step = 0; step < 5; step++) {
            const f32 ft = static_cast<f32>(step + 1) / 5.0f;
            Vec3 next = lerp(tip, aim, ft * ft);
            next.x += (rnd() - 0.5f) * 0.6f;
            next.y += (rnd() - 0.5f) * 0.6f;
            next.z += (rnd() - 0.5f) * 0.6f;
            push_line(tip, next, 1.0f);
            tip = next;
        }
    }
}

void TravelProgram::draw_spool(const ProgramContext& ctx, const TermView& view)
{
    const std::span<const Destination> all = destinations();
    const Destination& d = all[static_cast<u32>(target_)];
    const f32 charge = f_clamp01(view.travel_charge);
    const bool full = charge >= 0.999f;
    const bool spooling = view.travel_ready && view.speed_kmh >= kSpoolSpeedKmh;

    build_coil(charge, spooling ? 1.0f : 0.0f);
    ctx.scene->lines = lines_;
    ctx.scene->line_vertex_count = line_count_;

    const Mat4 proj = mat4_perspective(36.0f * kDegToRad,
                                       static_cast<f32>(kTermTexW)
                                           / static_cast<f32>(kTermTexH),
                                       0.5f, 90.0f);
    const Vec3 focus{-2.5f, -0.2f, 0.0f};
    const Vec3 eye{std::sin(coil_view_) * 14.5f, 2.6f, std::cos(coil_view_) * 14.5f};
    ctx.scene->vp3d = proj * mat4_look_at(eye + focus, focus, Vec3{0.0f, 1.0f, 0.0f});

    Screen& s = *ctx.screen;
    s.grid_clear();
    s.grid_title("COIL SPOOL");

    s.grid_text(2, 2, TC_GREEN, "-->  %.*s", static_cast<int>(d.name.size()), d.name.data());

    // Solid block column, tall and wide enough to read from the driver's seat once the
    // screen has been through the depth pixelation.
    constexpr i32 kColTop = 5;
    constexpr i32 kColRows = 14;
    constexpr i32 kColLeft = 2;
    constexpr i32 kColWide = 11;
    const i32 filled = static_cast<i32>(charge * static_cast<f32>(kColRows) + 0.5f);
    for (i32 r = 0; r < kColRows; r++) {
        const i32 row = kColTop + kColRows - 1 - r;
        const bool on = r < filled;
        const bool crest = on && r == filled - 1 && !full;
        const u8 color = full ? TC_BRIGHT : (crest ? TC_AMBER : (on ? TC_GREEN : TC_DIM));
        for (i32 c = 0; c < kColWide; c++) {
            if (on) {
                s.grid_put(row, kColLeft + c, kTermBlockGlyph, color);
            } else if ((r % 3) == 0 && (c % 2) == 0) {
                s.grid_put(row, kColLeft + c, '.', TC_DIM);
            }
        }
    }
    s.grid_text(kColTop - 1, kColLeft, TC_DIM, "COIL CHARGE");
    s.grid_text(kColTop + kColRows, kColLeft, full ? TC_BRIGHT : TC_GREEN, "%3.0f%%",
                static_cast<f64>(charge * 100.0f));
    s.grid_text(kColTop + kColRows, kColLeft + 6, TC_DIM, "%4.1f MJ",
                static_cast<f64>(charge * 48.0f));

    // Speed gauge along the bottom, threshold marked.
    constexpr i32 kBarRow = 21;
    constexpr i32 kBarLeft = 2;
    constexpr i32 kBarWide = 34;
    const f32 speed_frac = f_clamp01(view.speed_kmh / 160.0f);
    const i32 marker = static_cast<i32>(kSpoolSpeedKmh / 160.0f * kBarWide);
    for (i32 c = 0; c < kBarWide; c++) {
        const bool on = static_cast<f32>(c) / static_cast<f32>(kBarWide) < speed_frac;
        const u8 color = c < marker ? (on ? TC_AMBER : TC_DIM) : (on ? TC_BRIGHT : TC_DIM);
        s.grid_put(kBarRow, kBarLeft + c, on ? kTermBlockGlyph : '-', color);
    }
    s.grid_put(kBarRow - 1, kBarLeft + marker, 'v', TC_DIM);
    s.grid_text(kBarRow + 1, kBarLeft, spooling ? TC_BRIGHT : TC_AMBER,
                "%3.0f KM/H   SPOOL AT %3.0f", static_cast<f64>(view.speed_kmh),
                static_cast<f64>(kSpoolSpeedKmh));

    const i32 last_row = static_cast<i32>(kTermRows) - 1;
    if (!view.travel_ready) {
        s.grid_text(3, 20, TC_RED, "NO COIL ON THE ROOF");
        s.grid_text(last_row, 1, TC_DIM, "[Q] STAND DOWN");
        return;
    }

    if (view.travel_primed && std::fmod(ctx.blink, 0.7f) < 0.45f) {
        s.grid_text(2, 20, TC_BRIGHT, "*** PRIMED ***");
    }

    if (full) {
        if (std::fmod(ctx.blink, 0.6f) < 0.4f) {
            s.grid_text(3, 20, TC_BRIGHT, "*** COIL SATURATED ***");
        }
    } else if (spooling) {
        const i32 dots = static_cast<i32>(std::fmod(ctx.blink * 4.0f, 4.0f));
        s.grid_text(3, 20, TC_BRIGHT, "SPOOLING%.*s", dots, "...");
    } else if (std::fmod(ctx.blink, 0.9f) < 0.6f) {
        s.grid_text(3, 20, TC_AMBER, "BLEEDING OFF");
    }

    if (!d.surveyed()) {
        s.grid_text(5, 20, TC_RED, "NO SURVEY DATA.");
        s.grid_text(6, 20, TC_RED, "EXIT POINT UNRESOLVED.");
        s.grid_text(last_row, 1, TC_DIM, "[Q] STAND DOWN");
    } else if (view.travel_primed) {
        s.grid_text(5, 20, TC_BRIGHT, "FIRES ON ITS OWN AT");
        s.grid_text(6, 20, TC_BRIGHT, "FULL CHARGE. DRIVE.");
        s.grid_text(last_row, 1, TC_DIM, "[ENTER] SAFE   [Q] STAND DOWN");
    } else {
        s.grid_text(last_row, 1, TC_DIM, "[ENTER] PRIME   [Q] STAND DOWN");
    }
}

void TravelProgram::update(const ProgramContext& ctx, const TermView& view, f32 dt)
{
    stage_time_ += dt;
    orbit_ += dt * 0.40f;
    coil_spin_ += dt * 1.30f;
    coil_view_ = std::sin(stage_time_ * 0.24f) * 0.42f;
    arc_seed_ = arc_seed_ * 1664525u + 1013904223u;

    switch (stage_) {
    case Stage::Select:
        draw_select(ctx, view);
        break;
    case Stage::Plot:
        if (stage_time_ >= kPlotEnd) {
            stage_ = Stage::Spool;
            stage_time_ = 0.0f;
            draw_spool(ctx, view);
            break;
        }
        draw_plot(ctx);
        break;
    case Stage::Spool:
        draw_spool(ctx, view);
        break;
    }
}

void TravelProgram::key_char(const ProgramContext& ctx, const TermView& view, char c)
{
    (void)ctx;
    (void)view;
    (void)c;
}

bool TravelProgram::key(const ProgramContext& ctx, const TermView& view, TermKey key)
{
    (void)view;
    const i32 count = static_cast<i32>(destinations().size());

    switch (stage_) {
    case Stage::Select:
        if (key == TermKey::Up && selected_ > 0) {
            selected_--;
            ctx.screen->click();
        } else if (key == TermKey::Down && selected_ + 1 < count) {
            selected_++;
            ctx.screen->click();
        } else if (key == TermKey::Enter) {
            const Destination& d = destinations()[static_cast<u32>(selected_)];
            if (d.range_km <= 0.0f) {
                break;
            }
            target_ = selected_;
            stage_ = Stage::Plot;
            stage_time_ = 0.0f;
            ctx.screen->click();
        } else if (key == TermKey::Quit || key == TermKey::Escape) {
            return true;
        }
        break;

    case Stage::Plot:
        if (key == TermKey::Enter) {
            stage_time_ = kPlotEnd;
        } else if (key == TermKey::Quit || key == TermKey::Escape) {
            stage_ = Stage::Select;
            stage_time_ = 0.0f;
            line_count_ = 0;
        }
        break;

    case Stage::Spool:
        // Arming rather than firing: nobody can reach for the keyboard at the speed the
        // coil needs, so the shot is set up first and goes off on its own at saturation.
        if (key == TermKey::Enter && destinations()[static_cast<u32>(target_)].surveyed()
            && view.travel_ready) {
            if (view.travel_primed) {
                ctx.request->travel_disarm = true;
            } else {
                ctx.request->travel_arm = true;
                ctx.request->travel_destination = target_;
            }
            ctx.screen->click();
        } else if (key == TermKey::Quit || key == TermKey::Escape) {
            if (view.travel_primed) {
                ctx.request->travel_disarm = true;
            }
            stage_ = Stage::Select;
            stage_time_ = 0.0f;
            line_count_ = 0;
        }
        break;
    }
    return false;
}

} // namespace anom
