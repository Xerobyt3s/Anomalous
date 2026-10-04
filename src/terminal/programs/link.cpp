#include "terminal/programs/link.h"
#include "carsys/items.h"
#include "terminal/screen.h"

#include <cmath>
#include <cstdio>

namespace anom {
namespace {

const char* phase_name(f32 tod)
{
    if (tod < 0.20f || tod >= 0.80f) {
        return "NIGHT";
    }
    if (tod < 0.30f) {
        return "DAWN";
    }
    return tod < 0.70f ? "DAY" : "DUSK";
}

} // namespace

void LinkProgram::enter(const ProgramContext& ctx, const TermView& view)
{
    (void)ctx;
    (void)view;
    anim_port_ = -1;
    anim_t_ = 0.0f;
}

void LinkProgram::draw_port(Screen& s, i32 row, const char* label, i32 state,
                            const char* linked, f32 blink) const
{
    s.grid_text(row, 2, TC_BRIGHT, "%s", label);
    if (state == PORT_UNPLUGGED) {
        s.grid_text(row + 1, 4, TC_DIM, "UNPLUGGED");
    } else if (state == PORT_PLUGGED) {
        const u8 color = std::fmod(blink, 0.9f) < 0.55f ? TC_AMBER : TC_DIM;
        s.grid_text(row + 1, 4, color, "PLUGGED - NO LINK");
    } else {
        s.grid_text(row + 1, 4, TC_GREEN, "LINKED - %s", linked);
    }
}

void LinkProgram::update(const ProgramContext& ctx, const TermView& view, f32 dt)
{
    Screen& s = *ctx.screen;

    if (deny_ > 0.0f) {
        deny_ -= dt;
    }
    if (anim_port_ >= 0) {
        const f32 prev = anim_t_;
        anim_t_ += dt;
        if (static_cast<i32>(anim_t_ * 7.0f) != static_cast<i32>(prev * 7.0f)) {
            s.print("");
        }
        const i32 state = anim_port_ == 0 ? view.coax_state : view.bus_state;
        const bool secured = anim_port_ == 1 && view.bus_tower && !view.tower_breached;
        if (state != PORT_PLUGGED) {
            anim_port_ = -1;
        } else if (secured && anim_t_ >= 1.1f) {
            deny_ = 3.0f;
            anim_port_ = -1;
        } else if (anim_t_ >= 1.6f) {
            ctx.request->link_port[anim_port_] = true;
            anim_port_ = -1;
        }
    }

    s.grid_clear();
    s.grid_title("PORT LINK MANAGER");

    char coax_desc[40];
    if (view.coax_camera) {
        std::snprintf(coax_desc, sizeof(coax_desc), "CAMERA FILM");
    } else {
        const std::string_view name = antenna_variant_name(view.antenna_tier);
        std::snprintf(coax_desc, sizeof(coax_desc), "%.*s ANTENNA",
                      static_cast<int>(name.size()), name.data());
    }
    draw_port(s, 2, "PORT A   COAX / ANTENNA", view.coax_state,
              view.coax_camera || view.antenna_tier >= 0 ? coax_desc : "ANTENNA", ctx.blink);

    const char* bus_desc = view.bus_tower
                             ? (view.tower_breached ? "RELAY R-4 (OPEN)" : "RELAY R-4 (SECURED)")
                             : "ENGINE BAY TAP";
    draw_port(s, 5, view.bus_tower ? "PORT B   REMOTE NODE" : "PORT B   VEHICLE BUS",
              view.bus_state, bus_desc, ctx.blink);

    if (view.bus_state == PORT_LINKED && view.bus_tower && !view.tower_breached) {
        s.grid_text(6, 4, std::fmod(ctx.blink, 0.7f) < 0.45f ? TC_RED : TC_DIM,
                    "LOCKED - RUN BREACH");
    }

    if (deny_ > 0.0f) {
        s.grid_text(9, 2, std::fmod(ctx.blink, 0.4f) < 0.25f ? TC_RED : TC_DIM,
                    "ACCESS DENIED - PORT SECURED");
        s.grid_text(10, 2, TC_DIM, "RUN BREACH TO CRACK THE LOCK.");
    } else if (anim_port_ >= 0) {
        i32 dots = static_cast<i32>(anim_t_ * 8.0f);
        dots = dots > 12 ? 12 : dots;
        s.grid_text(9, 2, TC_BRIGHT, "NEGOTIATING PORT %c %.*s", anim_port_ == 0 ? 'A' : 'B',
                    dots, "............");
        if (anim_t_ > 1.1f) {
            s.grid_text(10, 2, TC_GREEN, "CARRIER OK");
        }
    } else if (view.coax_state == PORT_PLUGGED || view.bus_state == PORT_PLUGGED) {
        if (std::fmod(ctx.blink, 0.8f) < 0.5f) {
            s.grid_text(9, 2, TC_AMBER, "PORT READY. INITIALIZE TO ESTABLISH LINK.");
        }
    } else if (view.coax_state == PORT_UNPLUGGED && view.bus_state == PORT_UNPLUGGED) {
        s.grid_text(9, 2, TC_DIM, "CONNECT CABLES AT REAR OF UNIT.");
    }

    s.grid_text(static_cast<i32>(kTermRows) - 1, 1, TC_DIM,
                "[1] INIT PORT A   [2] INIT PORT B   [Q] BACK");
}

void LinkProgram::key_char(const ProgramContext& ctx, const TermView& view, char c)
{
    (void)ctx;
    if (anim_port_ >= 0) {
        return;
    }
    if (c == '1' && view.coax_state == PORT_PLUGGED) {
        anim_port_ = 0;
        anim_t_ = 0.0f;
    } else if (c == '2' && view.bus_state == PORT_PLUGGED) {
        anim_port_ = 1;
        anim_t_ = 0.0f;
    }
}

void DevProgram::enter(const ProgramContext& ctx, const TermView& view)
{
    (void)ctx;
    tod_ = view.time_of_day;
    rain_ = view.weather_rain;
    wet_ = view.weather_wetness;
    wmode_ = view.weather_mode;
}

void DevProgram::request_time(const ProgramContext& ctx, f32 tod)
{
    tod -= std::floor(tod);
    ctx.request->time_set = true;
    ctx.request->time_value = tod;
    tod_ = tod;
}

void DevProgram::update(const ProgramContext& ctx, const TermView& view, f32 dt)
{
    (void)dt;
    rain_ = view.weather_rain;
    wet_ = view.weather_wetness;

    Screen& s = *ctx.screen;
    s.grid_clear();
    s.grid_title("DEV CONSOLE -- FIELD DIAGNOSTICS");

    const f32 hours = tod_ * 24.0f;
    const i32 hh = static_cast<i32>(hours);
    const i32 mm = static_cast<i32>((hours - static_cast<f32>(hh)) * 60.0f);
    static const char* kWeatherNames[5] = {"AUTO", "CLEAR", "DRIZZLE", "RAIN", "SNOW"};

    s.grid_text(3, 4, TC_BRIGHT, "TIME      %02d:%02d  (%s)", hh, mm, phase_name(tod_));
    s.grid_text(4, 4, TC_GREEN, "WARP      %s", warp_ ? "60X ENGAGED" : "OFF");
    s.grid_text(6, 4, TC_BRIGHT, "WEATHER   %s   RAIN %3.0f%%   GROUND WET %3.0f%%",
                kWeatherNames[(wmode_ >= 0 && wmode_ < 5) ? wmode_ : 0],
                static_cast<f64>(rain_ * 100.0f),
                static_cast<f64>(wet_ * 100.0f));

    s.grid_text(9, 4, TC_GREEN, "[LEFT]/[RIGHT]  TIME -/+ 30 MIN");
    s.grid_text(10, 4, TC_GREEN, "[1] DAWN   [2] NOON   [3] DUSK   [4] MIDNIGHT");
    s.grid_text(11, 4, TC_GREEN, "[T] TOGGLE TIME WARP");
    s.grid_text(12, 4, TC_GREEN, "[5] WX AUTO  [6] CLEAR  [7] DRIZZLE  [8] RAIN  [9] SNOW");

    if (std::fmod(ctx.blink, 1.4f) < 0.8f) {
        s.grid_text(14, 4, TC_AMBER, "ENGINEERING BUILD -- NOT FOR FIELD UNITS");
    }
    s.grid_text(static_cast<i32>(kTermRows) - 1, 1, TC_DIM, "[Q] EXIT");
}

void DevProgram::key_char(const ProgramContext& ctx, const TermView& view, char c)
{
    (void)view;
    if (c == '1') {
        request_time(ctx, 0.27f);
    } else if (c == '2') {
        request_time(ctx, 0.50f);
    } else if (c == '3') {
        request_time(ctx, 0.72f);
    } else if (c == '4') {
        request_time(ctx, 0.0f);
    } else if (c == 'T') {
        warp_ = !warp_;
    } else if (c >= '5' && c <= '9') {
        wmode_ = c - '5';
        ctx.request->weather_mode = wmode_;
    }
}

bool DevProgram::key(const ProgramContext& ctx, const TermView& view, TermKey key)
{
    (void)view;
    if (key == TermKey::Left) {
        request_time(ctx, tod_ - 1.0f / 48.0f);
        return false;
    }
    if (key == TermKey::Right) {
        request_time(ctx, tod_ + 1.0f / 48.0f);
        return false;
    }
    return key == TermKey::Enter || key == TermKey::Quit || key == TermKey::Escape;
}

} // namespace anom
