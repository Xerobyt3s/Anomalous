#include "terminal/programs/status.h"
#include "carsys/carsys.h"
#include "terminal/screen.h"
#include "terminal/virus.h"
#include "vehicle/vehicle.h"

#include <cmath>

namespace anom {
namespace {

constexpr u32 kWireViewportW = 208;
constexpr u32 kWireViewportH = 224;

Vec3 condition_color(f32 cond, f32 blink)
{
    if (cond > 0.6f) {
        return Vec3{0.24f, 0.71f, 0.32f};
    }
    if (cond > 0.3f) {
        return Vec3{0.86f, 0.67f, 0.24f};
    }
    return Vec3{0.98f, 0.33f, 0.25f} * (0.65f + 0.35f * std::sin(blink * 5.0f));
}

} // namespace

void StatusProgram::enter(const ProgramContext& ctx, const TermView& view)
{
    (void)ctx;
    (void)view;
    spin_ = 0.0f;
}

void StatusProgram::build_wires(const ProgramContext& ctx, const TermView& view) const
{
    TermScene& scene = *ctx.scene;
    const CarSys& sys = *view.sys;
    const Vehicle& veh = *view.veh;
    const Vec3 one{1.0f, 1.0f, 1.0f};

    scene.wire_count = 0;
    scene.push_wire(veh.config().body_mesh.view(),
                    mat4_trs(Vec3{}, quat_identity(), one), Vec3{0.09f, 0.28f, 0.12f});

    for (u32 i = 0; i < kWheelCount; i++) {
        const PartSlot& slot = sys.parts[PART_TIRE_FL + i];
        if (!slot.installed) {
            continue;
        }
        const Quat q = veh.config().wheels[i].pos.x > 0.0f
                         ? quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, kPi)
                         : quat_identity();
        scene.push_wire(veh.config().wheel_mesh.view(),
                        mat4_trs(veh.config().wheels[i].pos, q, one),
                        condition_color(slot.condition, ctx.blink));
    }

    static const PartKind kBay[4] = {PART_ENGINE, PART_BATTERY, PART_ALTERNATOR, PART_RADIATOR};
    for (const PartKind kind : kBay) {
        const PartSlot& slot = sys.parts[kind];
        const PartDef& def = part_def(kind);
        if (!slot.installed || def.mesh.empty()) {
            continue;
        }
        scene.push_wire(def.mesh, mat4_trs(def.socket_pos, quat_identity(), one),
                        condition_color(slot.condition, ctx.blink));
    }

    if (sys.parts[PART_COMPUTER].installed) {
        scene.push_wire(part_def(PART_COMPUTER).mesh,
                        mat4_trs(part_def(PART_COMPUTER).socket_pos, part_computer_rest_rot(),
                                 one),
                        condition_color(sys.parts[PART_COMPUTER].condition, ctx.blink));
    }
    if (sys.parts[PART_ANTENNA].installed) {
        static const char* kAntennas[3] = {"antenna_whip", "antenna_std", "antenna_array"};
        i32 variant = sys.parts[PART_ANTENNA].variant;
        variant = variant < 0 || variant > 2 ? 1 : variant;
        scene.push_wire(kAntennas[variant],
                        mat4_trs(part_def(PART_ANTENNA).socket_pos, quat_identity(), one),
                        condition_color(sys.parts[PART_ANTENNA].condition, ctx.blink));
    }

    const f32 pitch = 0.30f;
    const f32 dist = 8.8f;
    const Vec3 eye{std::sin(spin_) * std::cos(pitch) * dist, std::sin(pitch) * dist,
                   std::cos(spin_) * std::cos(pitch) * dist};
    const Mat4 vmat = mat4_look_at(eye, Vec3{}, Vec3{0.0f, 1.0f, 0.0f});
    const Mat4 proj = mat4_perspective(30.0f * kDegToRad,
                                       static_cast<f32>(kWireViewportW)
                                           / static_cast<f32>(kWireViewportH),
                                       0.1f, 60.0f);
    scene.vp3d = proj * vmat;
}

void StatusProgram::update(const ProgramContext& ctx, const TermView& view, f32 dt)
{
    spin_ += dt * 0.55f;

    Screen& s = *ctx.screen;
    const CarSys& sys = *view.sys;
    const f32 blink = ctx.blink;

    s.grid_clear();
    s.grid_title("VEHICLE DIAGNOSTICS");

    s.grid_text(2, 2, TC_GREEN, "ENGINE %-8s", sys.engine_on ? "RUNNING" : "OFF");
    s.grid_text(2, 18, TC_GREEN, "RPM %4.0f",
                static_cast<f64>(virus_->lie(view.rpm, blink, 1.0f)));
    s.grid_text(2, 29, TC_GREEN, "SPEED %3.0f KM/H",
                static_cast<f64>(virus_->lie(view.speed_kmh, blink, 2.0f)));

    static const PartKind kRows[6] = {PART_ENGINE,     PART_BATTERY,   PART_ALTERNATOR,
                                      PART_RADIATOR,   PART_FUEL_TANK, PART_HEADLIGHTS};
    for (i32 i = 0; i < 6; i++) {
        const PartSlot& slot = sys.parts[kRows[i]];
        const f32 raw = slot.installed ? slot.condition : 0.0f;
        const f32 cond = f_clamp01(virus_->lie(raw, blink, 3.0f + static_cast<f32>(i)));
        const u8 color = cond > 0.6f ? TC_GREEN : (cond > 0.3f ? TC_AMBER : TC_RED);
        const std::string_view name = part_def(kRows[i]).name;
        s.grid_text(4 + i, 2, TC_GREEN, "%-11.*s", static_cast<int>(name.size()), name.data());
        s.grid_bar(4 + i, 14, 20, cond, color);
        if (slot.installed) {
            s.grid_text(4 + i, 37, color, "%3.0f%%", static_cast<f64>(cond * 100.0f));
        } else {
            s.grid_text(4 + i, 37, color, "OUT");
        }
    }

    static const char* kTireNames[4] = {"FL", "FR", "RL", "RR"};
    for (i32 i = 0; i < 4; i++) {
        const PartSlot& slot = sys.parts[PART_TIRE_FL + i];
        const f32 raw = slot.installed ? slot.condition : 0.0f;
        const f32 cond = f_clamp01(virus_->lie(raw, blink, 9.0f + static_cast<f32>(i)));
        const u8 color = cond > 0.5f ? TC_GREEN : (cond > 0.2f ? TC_AMBER : TC_RED);
        s.grid_text(11 + i / 2, 2 + (i % 2) * 20, color, "TIRE %s %3.0f%%", kTireNames[i],
                    static_cast<f64>(cond * 100.0f));
    }

    build_wires(ctx, view);

    const f32 fuel = f_clamp01(virus_->lie(sys.fluids.fuel, blink, 13.0f));
    const f32 oil = f_clamp01(virus_->lie(sys.fluids.oil, blink, 14.0f));
    s.grid_text(13, 2, TC_GREEN, "FUEL");
    s.grid_bar(13, 14, 20, fuel, fuel > 0.2f ? TC_GREEN : TC_RED);
    s.grid_text(14, 2, TC_GREEN, "OIL");
    s.grid_bar(14, 14, 20, oil, oil > 0.3f ? TC_GREEN : TC_RED);

    const f32 coolant = virus_->lie(sys.fluids.coolant_temp, blink, 15.0f);
    const bool hot = coolant > kCoolantOverheatC;
    s.grid_text(15, 2, TC_GREEN, "COOLANT");
    s.grid_text(15, 15, hot ? TC_RED : TC_GREEN, "%3.0f C %s", static_cast<f64>(coolant),
                hot ? "OVERHEAT" : "");

    const f32 charge = f_clamp01(virus_->lie(sys.elec.battery_charge, blink, 16.0f));
    s.grid_text(17, 2, TC_GREEN, "BATTERY");
    s.grid_bar(17, 14, 20, charge, charge > 0.15f ? TC_GREEN : TC_RED);
    s.grid_text(18, 2, TC_DIM, "ALT %4.1fA   DRAW %4.1fA   %s",
                static_cast<f64>(virus_->lie(sys.elec.alternator_amps, blink, 17.0f)),
                static_cast<f64>(virus_->lie(sys.elec.draw_amps, blink, 18.0f)),
                sys.handbrake_latched ? "PARK BRAKE SET" : "");
}

void ViewProgram::set(i32 photo, std::string_view name)
{
    photo_ = photo;
    name_.assign(name);
}

void ViewProgram::enter(const ProgramContext& ctx, const TermView& view)
{
    (void)ctx;
    (void)view;
    materialize_ = 0.0f;
}

void ViewProgram::update(const ProgramContext& ctx, const TermView& view, f32 dt)
{
    (void)view;
    materialize_ += dt;

    ctx.scene->photo = photo_;
    ctx.scene->image_reveal = f_clamp01(materialize_ * 0.9f);

    Screen& s = *ctx.screen;
    s.grid_clear();
    s.grid_title("IMAGE VIEWER");
    s.grid_text(static_cast<i32>(kTermRows) - 1, 1, TC_DIM, "%s   320X200 VC-6   [Q] BACK",
                name_.c_str());
}

void VideoProgram::enter(const ProgramContext& ctx, const TermView& view)
{
    (void)ctx;
    (void)view;
    materialize_ = 0.0f;
}

void VideoProgram::update(const ProgramContext& ctx, const TermView& view, f32 dt)
{
    (void)view;
    materialize_ += dt;

    ctx.scene->video_texture = texture_;
    ctx.scene->image_reveal = f_clamp01(materialize_ * 1.6f);

    Screen& s = *ctx.screen;
    s.grid_clear();
    s.grid_title("VIDEO FEED");
    if (std::fmod(ctx.blink, 1.2f) < 0.7f) {
        s.grid_text(1, static_cast<i32>(kTermCols) - 7, TC_RED, "\x7f LIVE");
    }
    s.grid_text(static_cast<i32>(kTermRows) - 1, 1, TC_DIM,
                "SRC: COAX CAM-1   320X200 10FPS   [Q] BACK");
}

} // namespace anom
