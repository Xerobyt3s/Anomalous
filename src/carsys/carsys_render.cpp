#include "carsys/carsys_render.h"
#include "carsys/carsys.h"
#include "physics/world.h"
#include "render/device.h"
#include "vehicle/vehicle.h"

namespace anom {
namespace {

constexpr f32 kHoodHingeY = 0.1474f;
constexpr f32 kHoodHingeZ = -0.62f;
constexpr f32 kHoodOpenAngle = 1.15f;
constexpr f32 kDoorHingeX = 0.80f;
constexpr f32 kDoorHingeZ = -0.55f;
constexpr f32 kDoorOpenAngle = 1.05f;
constexpr f32 kTrunkHingeY = 0.2038f;
constexpr f32 kTrunkHingeZ = 1.42f;
constexpr f32 kTrunkOpenAngle = 1.35f;
constexpr Vec3 kLeverPos{-0.13f, -0.17f, 0.44f};
constexpr f32 kLeverAngleRest = 0.12f;
constexpr f32 kLeverAngleSet = 0.55f;

const Vec3 kOne{1.0f, 1.0f, 1.0f};

Mat4 offset_from(const Mat4& base, Vec3 local, Quat rot = quat_identity())
{
    return base * mat4_trs(local, rot, kOne);
}

} // namespace

f32 CarSysRenderer::rand01()
{
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<f32>(rng_ >> 8) / 16777216.0f;
}

void CarSysRenderer::spawn_puff(Vec3 pos, Vec3 vel, f32 life, f32 size, bool spark)
{
    Puff& p = puffs_[puff_next_];
    puff_next_ = (puff_next_ + 1) % kMaxPuffs;
    p.pos = pos;
    p.vel = vel;
    p.life = life;
    p.max_life = life;
    p.size = size;
    p.spark = spark;
    p.used = true;
}

void CarSysRenderer::spawn_sparks(Vec3 pos, u32 count)
{
    for (u32 i = 0; i < count; i++) {
        spawn_puff(pos,
                   Vec3{(rand01() - 0.5f) * 3.0f, 1.0f + rand01() * 2.0f,
                        (rand01() - 0.5f) * 3.0f},
                   0.2f + rand01() * 0.15f, 0.025f, true);
    }
}

void CarSysRenderer::draw_effects(RenderDevice& device, const CarSys& sys, const Vehicle& veh,
                                  const Mat4& base, f32 dt)
{
    if (sys.fluids.coolant_temp > 105.0f && sys.engine_on) {
        smoke_accum_ += (sys.fluids.coolant_temp - 105.0f) * 0.6f * dt;
        while (smoke_accum_ >= 1.0f) {
            smoke_accum_ -= 1.0f;
            const Vec3 local = Vec3{(rand01() - 0.5f) * 0.5f, 0.08f, -1.35f + rand01() * 0.4f}
                             - veh.config().com_offset;
            spawn_puff(transform_point(base, local),
                       Vec3{(rand01() - 0.5f) * 0.5f, 0.9f + rand01() * 0.6f,
                            (rand01() - 0.5f) * 0.5f},
                       1.1f + rand01() * 0.6f, 0.05f + rand01() * 0.05f, false);
        }
    }

    for (Puff& p : puffs_) {
        if (!p.used) {
            continue;
        }
        p.life -= dt;
        if (p.life <= 0.0f) {
            p.used = false;
            continue;
        }
        p.vel.y += (p.spark ? -9.8f : 0.6f) * dt;
        p.pos += p.vel * dt;

        const f32 t = 1.0f - p.life / p.max_life;
        const f32 scale = p.spark ? p.size * (1.0f - t)
                                  : p.size * (0.6f + 2.2f * t) * f_clamp01(p.life * 5.0f);
        device.draw_mesh(device.assets().mesh(p.spark ? "warn_amber" : "puff"),
                         mat4_trs(p.pos, quat_identity(), Vec3{scale, scale, scale}));
    }
}

void CarSysRenderer::draw_glass(RenderDevice& device, const CarSys& sys, const Vehicle& veh,
                                PhysWorld& phys, f32 alpha, f32 time)
{
    const RigidBody* body = phys.body(veh.body());
    if (!body || veh.config().body_mesh.empty()) {
        return;
    }
    const Vec3 com = veh.config().com_offset;
    const Mat4 base = mat4_trs(lerp(body->prev_pos, body->pos, alpha),
                               slerp(body->prev_rot, body->rot, alpha), kOne);

    device.draw_glass(device.assets().mesh("excel_glass"), offset_from(base, -com), time);
    for (u32 side = 0; side < 2; side++) {
        const f32 sign = side == 0 ? -1.0f : 1.0f;
        const Vec3 hinge = Vec3{sign * kDoorHingeX, 0.0f, kDoorHingeZ} - com;
        const Quat swing = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f},
                                                sign * sys.door_open[side] * kDoorOpenAngle);
        device.draw_glass(device.assets().mesh(side == 0 ? "excel_door_glass_l"
                                                         : "excel_door_glass_r"),
                          offset_from(base, hinge, swing), time);
    }
}

void CarSysRenderer::draw(RenderDevice& device, const CarSys& sys, const Vehicle& veh,
                          PhysWorld& phys, f32 alpha, f32 dt, u32 screen_texture)
{
    const RigidBody* body = phys.body(veh.body());
    if (!body || veh.config().body_mesh.empty()) {
        return;
    }
    const Vec3 com = veh.config().com_offset;
    const Mat4 base = mat4_trs(lerp(body->prev_pos, body->pos, alpha),
                               slerp(body->prev_rot, body->rot, alpha), kOne);
    AssetCache& assets = device.assets();

    const Mat4 hood = offset_from(base, Vec3{0.0f, kHoodHingeY, kHoodHingeZ} - com,
                                  quat_from_axis_angle(Vec3{1.0f, 0.0f, 0.0f},
                                                       sys.hood_open * kHoodOpenAngle));
    device.draw_mesh(assets.mesh("excel_hood"), hood);

    for (u32 p = 0; p < 2; p++) {
        const f32 sign = p == 0 ? -1.0f : 1.0f;
        f32 pod = sys.popup_anim;
        if (p == 1 && sys.parts[PART_HEADLIGHTS].condition < 0.4f) {
            pod = f_min(pod, 0.38f);
        }
        device.draw_mesh(assets.mesh("excel_popup"),
                         offset_from(hood, Vec3{sign * 0.40f, -0.0934f, -0.88f},
                                     quat_from_axis_angle(Vec3{1.0f, 0.0f, 0.0f}, pod * 0.7f)));
    }

    static const PartKind kBayParts[4] = {PART_ENGINE, PART_BATTERY, PART_ALTERNATOR,
                                          PART_RADIATOR};
    for (PartKind kind : kBayParts) {
        if (!sys.parts[kind].installed) {
            continue;
        }
        const PartDef& def = part_def(kind);
        device.draw_mesh(assets.mesh(def.mesh), offset_from(base, def.socket_pos - com));
    }

    for (u32 side = 0; side < 2; side++) {
        const f32 sign = side == 0 ? -1.0f : 1.0f;
        const Vec3 hinge = Vec3{sign * kDoorHingeX, 0.0f, kDoorHingeZ} - com;
        const Quat swing = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f},
                                                sign * sys.door_open[side] * kDoorOpenAngle);
        device.draw_mesh(assets.mesh(side == 0 ? "excel_door_l" : "excel_door_r"),
                         offset_from(base, hinge, swing));
    }

    device.draw_mesh(assets.mesh("excel_trunk_lid"),
                     offset_from(base, Vec3{0.0f, kTrunkHingeY, kTrunkHingeZ} - com,
                                 quat_from_axis_angle(Vec3{1.0f, 0.0f, 0.0f},
                                                      -sys.trunk_open * kTrunkOpenAngle)));

    device.draw_mesh(assets.mesh("excel_lever"),
                     offset_from(base, kLeverPos - com,
                                 quat_from_axis_angle(Vec3{1.0f, 0.0f, 0.0f},
                                                      f_lerp(kLeverAngleRest, kLeverAngleSet,
                                                             sys.lever_anim))));

    for (const CargoItem& c : sys.cargo) {
        if (!c.used) {
            continue;
        }
        const Quat cargo_rot = item_cargo_rot(c.item.kind);
        const Vec3 local = (c.pos - com) - rotate(cargo_rot, item_mesh_center(c.item.kind));
        device.draw_mesh(assets.mesh(item_mesh(c.item.kind)),
                         offset_from(base, local, cargo_rot));
    }

    device.draw_mesh(assets.mesh("excel_fuelcap"),
                     offset_from(base, Vec3{0.80f, 0.145f, 1.30f} - com,
                                 quat_from_axis_angle(Vec3{0.0f, 0.0f, 1.0f},
                                                      sys.cap_anim * 1.3f)));

    if (sys.key_inserted) {
        device.draw_mesh(assets.mesh("part_key"),
                         offset_from(base, Vec3{-0.22f, 0.05f, -0.25f} - com));
    }

    const Vec3 wiper_axis = normalize(Vec3{0.0f, 0.807f, -0.591f});
    const f32 wiper_angle = -(0.20f + sys.wiper_sweep * 1.30f);
    static const f32 kWiperX[2] = {-0.38f, 0.10f};
    for (f32 wx : kWiperX) {
        device.draw_mesh(assets.mesh("part_wiper"),
                         offset_from(base, Vec3{wx, 0.150f, -0.60f} - com,
                                     quat_from_axis_angle(wiper_axis, wiper_angle)));
    }

    device.draw_mesh(assets.mesh("jack_coax"),
                     offset_from(base, Vec3{0.35f, 0.515f, 0.36f} - com));
    device.draw_mesh(assets.mesh("jack_bus"),
                     offset_from(base, Vec3{0.32f, -0.02f, -0.75f} - com));

    const Mat4 deck = offset_from(base, Vec3{0.12f, -0.045f, -0.295f} - com);
    device.draw_mesh(assets.mesh("part_deck"), deck);
    if (sys.tape_inserted >= 0) {
        device.draw_mesh(assets.mesh("part_cassette"),
                         offset_from(deck, Vec3{0.0f, 0.006f, 0.052f}));
    }

    if (sys.parts[PART_ANTENNA].installed) {
        static const char* kAntennas[3] = {"antenna_whip", "antenna_std", "antenna_array"};
        i32 variant = sys.parts[PART_ANTENNA].variant;
        if (variant < 0 || variant > 2) {
            variant = 1;
        }
        device.draw_mesh(assets.mesh(kAntennas[variant]),
                         offset_from(base, part_def(PART_ANTENNA).socket_pos - com));
    }

    if (sys.parts[PART_COMPUTER].installed) {
        const Mat4 term = offset_from(base, part_def(PART_COMPUTER).socket_pos - com,
                                      part_computer_rest_rot());
        device.draw_mesh(assets.mesh("part_computer"), term);
        if (sys.floppy_disk >= 0) {
            device.draw_mesh(assets.mesh("part_floppy"),
                             offset_from(term, Vec3{0.0f, -0.119f, 0.223f}));
        }
        if (screen_texture) {
            device.draw_lit_quad(term * mat4_trs(Vec3{0.0f, 0.047f, 0.170f}, quat_identity(),
                                                 Vec3{0.304f, 0.19f, 1.0f}),
                                 screen_texture, 0.0f);
        } else if (sys.computer_on) {
            device.draw_mesh(assets.mesh("computer_glow"), term);
        }
    }

    const f32 dial_val[4] = {
        f_clamp01(f_abs(veh.forward_speed(phys)) * 3.6f / 200.0f),
        f_clamp01(drivetrain_rpm(veh.train()) / 7000.0f),
        f_clamp01(sys.fluids.fuel),
        f_clamp01((sys.fluids.coolant_temp - 20.0f) / 106.0f),
    };
    static const f32 kDialX[4] = {-0.44f, -0.30f, -0.405f, -0.335f};
    static const f32 kDialY[4] = {0.064f, 0.064f, 0.006f, 0.006f};
    static const f32 kDialScale[4] = {1.0f, 1.0f, 0.5f, 0.5f};
    for (u32 d = 0; d < 4; d++) {
        const f32 needle = 2.27f - dial_val[d] * 4.54f;
        device.draw_mesh(assets.mesh("excel_needle"),
                         base * mat4_trs(Vec3{kDialX[d], kDialY[d], -0.305f} - com,
                                         quat_from_axis_angle(Vec3{0.0f, 0.0f, 1.0f}, needle),
                                         Vec3{kDialScale[d], kDialScale[d], kDialScale[d]}));
    }

    const bool warn_on[4] = {
        sys.fluids.coolant_temp > kCoolantOverheatC,
        sys.fluids.oil < 0.3f,
        sys.elec.battery_charge < 0.15f,
        sys.handbrake_latched,
    };
    static const bool kWarnAmber[4] = {false, false, true, false};
    for (u32 w = 0; w < 4; w++) {
        if (!warn_on[w]) {
            continue;
        }
        device.draw_mesh(assets.mesh(kWarnAmber[w] ? "warn_amber" : "warn_red"),
                         base * mat4_trs(Vec3{-0.418f + 0.028f * static_cast<f32>(w), 0.030f,
                                              -0.304f} - com,
                                         quat_identity(), Vec3{0.014f, 0.014f, 0.008f}));
    }

    if (veh.input().brake > 0.05f) {
        device.draw_mesh(assets.mesh("excel_brakelight"), offset_from(base, -com));
    }
    if (veh.train().gear == -1) {
        device.draw_mesh(assets.mesh("excel_revlight"), offset_from(base, -com));
    }

    draw_effects(device, sys, veh, base, dt);
}

} // namespace anom
