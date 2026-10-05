#include "sim/sim.h"

#include <cmath>

namespace anom {
namespace {

constexpr f32 kRunLap = 5.5f;
constexpr f32 kCrawlLap = 5.0f;

bool crossed(f32 t, f32 dt, f32 when)
{
    return t >= when && t - dt < when;
}

}

std::string_view dummy_script_name(DummyScript script)
{
    switch (script) {
    case DummyScript::Stand:
        return "stands";
    case DummyScript::Strafe:
        return "strafes";
    case DummyScript::Revive:
        return "revives the downed";
    case DummyScript::Run:
        return "runs laps";
    case DummyScript::Shroud:
        return "shrouds itself";
    case DummyScript::Crawl:
        return "crawls and dives";
    case DummyScript::Count:
        break;
    }
    return "idles";
}

PlayerCommand Sim::dummy_command(PlayerSlot& s, f32 dt) const
{
    s.script_time += dt;
    Player& body = s.player;
    const MoveState& state = body.movement().state();
    const Quat frame = state.frame;
    const Vec3 feet = body.pos();
    const Vec3 eye = feet + body.up() * state.eye_height;

    f32 yaw = state.yaw;
    f32 pitch = state.pitch;
    const auto face = [&](Vec3 point) {
        const Vec3 d = point - eye;
        if (length_sq(d) > 1e-6f) {
            frame_view_angles(frame, normalize(d), yaw, pitch);
        }
    };

    PlayerCommand cmd;
    cmd.gameplay = true;
    const PlayerSlot* local = nearest_human(feet);
    const bool down = roster_.downed(s.id);
    const auto chest = [](const PlayerSlot& other) {
        const MoveState& o = other.player.movement().state();
        return other.player.pos() + other.player.up() * (o.height * 0.65f);
    };
    const auto lap_around = [&](f32 ahead) {
        if (!local) {
            return;
        }
        const MoveState& o = local->player.movement().state();
        const Vec3 center = local->player.pos() + frame_forward(o.frame, o.yaw) * ahead;
        face(center + body.up() * state.eye_height);
        yaw += kPi * 0.5f - f_clamp((length(center - feet) - 3.0f) * 0.5f, -0.6f, 0.9f);
        pitch = 0.0f;
    };

    if (local && !down) {
        face(chest(*local));
    }
    if (!down) {
        const f32 t_all = s.script_time;
        switch (s.script) {
        case DummyScript::Strafe:
            cmd.move_x = std::sin(t_all * 1.2f) > 0.0f ? 1.0f : -1.0f;
            break;
        case DummyScript::Revive:
            for (const PlayerSlot& other : slots_) {
                if (!other.active || other.id == s.id || !roster_.downed(other.id)) {
                    continue;
                }
                face(other.player.pos() + other.player.up() * 0.3f);
                const Vec3 apart = other.player.pos() - feet;
                const f32 distance = length(apart - body.up() * dot(apart, body.up()));
                cmd.move_z = distance > 1.2f ? 1.0f : 0.0f;
                cmd.run = distance > 6.0f;
                cmd.use_down = distance < rules_.reviveRange * 0.9f;
                break;
            }
            break;
        case DummyScript::Run: {
            lap_around(kRunLap);
            const f32 t = std::fmod(t_all, 6.0f);
            cmd.move_z = 1.0f;
            cmd.run = t < 4.0f;
            cmd.holster = (t >= 4.0f) != state.holstered;
            break;
        }
        case DummyScript::Shroud:
            if (std::fmod(t_all, 5.0f) < dt) {
                body.movement().apply_shroud(2.0f);
            }
            break;
        case DummyScript::Crawl: {
            lap_around(kCrawlLap);
            cmd.move_z = 1.0f;
            const f32 t = std::fmod(t_all, 6.0f);
            const Stance stance = state.stance;
            if (t > 0.2f && t < 4.0f && stance != Stance::Crawl && stance != Stance::Dive && state.grounded) {
                cmd.crawl = true;
            }
            cmd.jump = (crossed(t, dt, 4.0f) && stance == Stance::Crawl) || crossed(t, dt, 4.6f)
                    || crossed(t, dt, 4.85f);
            cmd.run = t >= 4.0f;
            cmd.holster = (crossed(t, dt, 0.3f) && !state.holstered) || (crossed(t, dt, 2.2f) && state.holstered);
            if (t > 2.4f && t < 2.9f) {
                cmd.move_z = 0.0f;
                cmd.aim = true;
                if (local) {
                    face(chest(*local));
                }
            } else if (t >= 2.9f && t < 3.95f) {
                cmd.move_z = 0.0f;
                yaw = state.lie_yaw + 2.0f * kPi * (t - 2.9f) / 1.05f;
            }
            if (crossed(t, dt, 4.85f)) {
                cmd.move_x = 1.0f;
                cmd.move_z = 0.0f;
            }
            break;
        }
        case DummyScript::Stand:
            cmd.holster = std::fmod(t_all, 2.0f) < dt;
            break;
        case DummyScript::Count:
            break;
        }
    }

    cmd.face_view = true;
    cmd.view_dir = frame_view(frame, f_wrap_angle(yaw), pitch);
    cmd.view_origin = eye;
    return cmd;
}

}
