#include "carsys/cables.h"
#include "physics/collide.h"
#include "physics/heightfield.h"
#include "physics/world.h"
#include "world/terrain.h"

namespace anom {
namespace {
constexpr u32 kCableIters = 8;

constexpr u32 kCableBoxCount = 14;
constexpr i32 kCableAnchorIdx =
    static_cast<i32>(static_cast<f32>(kCablePoints - 1)
                         * (kCableLength / (kCableLength + kReelLength))
                     + 0.5f);

struct CarBox {
    Vec3 center;
    Vec3 half;
};

constexpr CarBox kCarBoxes[kCableBoxCount] = {
    {{0.0f, -0.17f, 0.00f}, {0.80f, 0.31f, 2.18f}},
    {{0.0f, 0.075f, 0.10f}, {0.79f, 0.095f, 0.68f}},
    {{0.0f, 0.06f, -1.45f}, {0.72f, 0.06f, 0.73f}},
    {{0.0f, 0.12f, -0.66f}, {0.70f, 0.04f, 0.12f}},
    {{0.0f, 0.22f, -0.48f}, {0.60f, 0.07f, 0.14f}},
    {{0.0f, 0.38f, -0.28f}, {0.57f, 0.09f, 0.14f}},
    {{0.0f, 0.49f, 0.25f}, {0.55f, 0.045f, 0.32f}},
    {{0.0f, 0.42f, 0.72f}, {0.54f, 0.06f, 0.18f}},
    {{0.0f, 0.30f, 0.95f}, {0.58f, 0.06f, 0.20f}},
    {{0.0f, 0.13f, 1.70f}, {0.68f, 0.075f, 0.49f}},
    {{-0.72f, -0.255f, -1.24f}, {0.10f, 0.31f, 0.31f}},
    {{0.72f, -0.255f, -1.24f}, {0.10f, 0.31f, 0.31f}},
    {{-0.72f, -0.255f, 1.24f}, {0.10f, 0.31f, 0.31f}},
    {{0.72f, -0.255f, 1.24f}, {0.10f, 0.31f, 0.31f}},
};

void push_out_of_box(Vec3& local, Vec3 center, Vec3 box_half)
{
    const Vec3 d = local - center;
    const Vec3 half = box_half + Vec3{kCableRadius, kCableRadius, kCableRadius};
    if (f_abs(d.x) >= half.x || f_abs(d.y) >= half.y || f_abs(d.z) >= half.z) {
        return;
    }
    const f32 px = half.x - f_abs(d.x);
    const f32 py = half.y - f_abs(d.y);
    const f32 pz = half.z - f_abs(d.z);
    if (px < py && px < pz) {
        local.x = center.x + (d.x >= 0.0f ? half.x : -half.x);
    } else if (py < pz) {
        local.y = center.y + (d.y >= 0.0f ? half.y : -half.y);
    } else {
        local.z = center.z + (d.z >= 0.0f ? half.z : -half.z);
    }
}

void cable_collide(Vec3& p, const CableSimInput& in)
{
    if (in.terrain) {
        const f32 floor_y = in.terrain->heightfield().sample(p.x, p.z) + kCableRadius + 0.005f;
        if (p.y < floor_y) {
            p.y = floor_y;
        }
    }

    const Quat inv = conjugate(in.car_rot);
    Vec3 local = rotate(inv, p - in.car_pos);
    for (u32 b = 0; b < kCableBoxCount; b++) {
        push_out_of_box(local, kCarBoxes[b].center, kCarBoxes[b].half);
    }
    p = in.car_pos + rotate(in.car_rot, local);

    for (u32 o = 0; o < in.obstacle_count; o++) {
        const CableObstacle& obs = in.obstacles[o];
        Vec3 obs_local = rotate(conjugate(obs.rot), p - obs.pos);
        push_out_of_box(obs_local, obs.center, obs.half);
        p = obs.pos + rotate(obs.rot, obs_local);
    }

    if (in.phys) {
        Sphere sphere;
        sphere.center = p;
        sphere.radius = kCableRadius + 0.01f;
        SphereContact contacts[3];
        const u32 n = collide_sphere_statics(in.phys->statics(), sphere, contacts, 3);
        for (u32 c = 0; c < n; c++) {
            p += contacts[c].normal * contacts[c].depth;
        }
    }
}

}

void Cable::reset()
{
    state = CableState::Stowed;
    linked = false;
    sim_init = false;
    via_reel = false;
    let_out = 0.0f;
}

f32 Cable::max_len() const
{
    return via_reel ? kCableLength + kReelLength : kCableLength;
}

f32 Cable::current_length() const
{
    if (!sim_init) {
        return 0.0f;
    }
    f32 total = 0.0f;
    for (u32 i = 0; i + 1 < kCablePoints; i++) {
        total += distance(p[i], p[i + 1]);
    }
    return total;
}

f32 Cable::span(Vec3 root, Vec3 end, const Vec3* anchor) const
{
    if (via_reel && anchor) {
        return distance(root, *anchor) + distance(*anchor, end);
    }
    return distance(root, end);
}

void Cable::sim(const CableSimInput& in, f32 dt)
{
    dt = f_min(dt, 1.0f / 30.0f);
    const i32 aidx = (via_reel && in.anchor) ? kCableAnchorIdx : -1;

    f32 need = 1.0f;
    if (in.end) {
        need = span(in.root, *in.end, in.anchor) * 1.10f + 0.35f;
    }
    need = f_clamp(need, 0.7f, max_len());

    if (!sim_init) {
        for (u32 i = 0; i < kCablePoints; i++) {
            const f32 f = static_cast<f32>(i) / static_cast<f32>(kCablePoints - 1);
            const Vec3 hang = in.phys ? in.phys->up_at(in.root) * -1.0f : Vec3{0.0f, -1.0f, 0.0f};
            const Vec3 target = in.end ? *in.end : in.root + hang * 0.3f;
            p[i] = lerp(in.root, target, f);
            prev[i] = p[i];
        }
        let_out = need;
        sim_init = true;
    }

    if (let_out < need) {
        const f32 pay_out = f_max((need - let_out) * 10.0f, 1.5f) * dt;
        let_out = f_min(let_out + pay_out, need);
    } else {
        let_out = f_max(let_out - 1.8f * dt, need);
    }

    const f32 seg = let_out / static_cast<f32>(kCablePoints - 1);

    for (u32 i = 1; i < kCablePoints; i++) {
        const bool pinned = (in.end && i == kCablePoints - 1)
                         || (aidx >= 0 && static_cast<i32>(i) == aidx);
        if (pinned) {
            continue;
        }
        const Vec3 vel = (p[i] - prev[i]) * 0.976f;
        prev[i] = p[i];
        p[i] += vel;
        p[i] += (in.phys ? in.phys->gravity_at(p[i]) : Vec3{0.0f, -9.8f, 0.0f}) * (dt * dt);
    }

    p[0] = in.root;
    if (in.end) {
        p[kCablePoints - 1] = *in.end;
    }
    if (aidx >= 0) {
        p[aidx] = *in.anchor;
    }

    for (u32 iter = 0; iter < kCableIters; iter++) {
        for (u32 i = 0; i + 1 < kCablePoints; i++) {
            const Vec3 delta = p[i + 1] - p[i];
            const f32 len = length(delta);
            if (len < 1e-6f) {
                continue;
            }
            const f32 diff = (len - seg) / len;
            const bool pin_a = i == 0 || (aidx >= 0 && static_cast<i32>(i) == aidx);
            const bool pin_b = (in.end && i + 1 == kCablePoints - 1)
                            || (aidx >= 0 && static_cast<i32>(i + 1) == aidx);
            if (pin_a && pin_b) {
                continue;
            }
            const f32 wa = pin_a ? 0.0f : (pin_b ? 1.0f : 0.5f);
            const f32 wb = pin_b ? 0.0f : (pin_a ? 1.0f : 0.5f);
            p[i] += delta * (diff * wa);
            p[i + 1] -= delta * (diff * wb);
        }

        if (iter + 2 < kCableIters) {
            continue;
        }
        for (u32 i = 1; i < kCablePoints; i++) {
            if ((in.end && i == kCablePoints - 1) || (aidx >= 0 && static_cast<i32>(i) == aidx)) {
                continue;
            }
            cable_collide(p[i], in);
        }
    }
}

}
