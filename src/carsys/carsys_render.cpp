#include "carsys/carsys_render.h"
#include "carsys/car_lights.h"
#include "carsys/carsys.h"
#include "physics/world.h"
#include "render/device.h"
#include "vehicle/vehicle.h"

namespace anom {
namespace {

constexpr f32 kLeverAngleRest = 0.12f;
constexpr f32 kLeverAngleSet = 0.55f;

const Vec3 kOne{1.0f, 1.0f, 1.0f};

constexpr f32 kSmokeBuoyancy = 0.06f;

Mat4 offset_from(const Mat4& base, Vec3 local, Quat rot = quat_identity())
{
    return base * mat4_trs(local, rot, kOne);
}

void draw_named(RenderDevice& device, const FixedString<32>& name, const Mat4& model)
{
    if (!name.empty()) {
        device.draw_mesh(device.assets().mesh(name.view()), model);
    }
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

void CarSysRenderer::draw_sparks(RenderDevice& device, Vec3 gravity, f32 dt)
{
    for (Puff& p : puffs_) {
        if (!p.used) {
            continue;
        }
        p.life -= dt;
        if (p.life <= 0.0f) {
            p.used = false;
            continue;
        }
        p.vel += gravity * ((p.spark ? 1.0f : -kSmokeBuoyancy) * dt);
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

    const CarLayout& panes = veh.config().layout;
    device.set_wipers(panes.wiper_x[0], panes.wiper_x[1], panes.wiper_glass_y, panes.wiper_base.z, panes.wiper_reach);
    if (!panes.glass_mesh.empty()) {
        device.draw_glass(device.assets().mesh(panes.glass_mesh.view()), offset_from(base, -com), time);
    }
    if (sys.parts[PART_TANK].installed) {
        device.draw_glass(device.assets().mesh("part_tank_glass"), offset_from(base, part_def(PART_TANK).socket_pos - com), time);
    }
    const CarLayout& layout = veh.config().layout;
    for (u32 side = 0; side < 2; side++) {
        const f32 sign = side == 0 ? -1.0f : 1.0f;
        const Vec3 hinge = Vec3{sign * layout.door_hinge.x, layout.door_hinge.y, layout.door_hinge.z} - com;
        const Quat swing = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f},
                                                sign * sys.door_open[side] * layout.door_angle);
        if (!layout.door_glass_mesh[side].empty()) {
            device.draw_glass(device.assets().mesh(layout.door_glass_mesh[side].view()), offset_from(base, hinge, swing), time);
        }
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

    const CarLayout& layout = veh.config().layout;
    const Mat4 hood = offset_from(base, layout.hood_hinge - com,
                                  quat_from_axis_angle(Vec3{1.0f, 0.0f, 0.0f},
                                                       sys.hood_open * layout.hood_angle));
    draw_named(device, layout.hood_mesh, hood);
    draw_named(device, layout.interior_mesh, offset_from(base, -com));

    for (u32 p = 0; p < 2; p++) {
        const f32 sign = p == 0 ? -1.0f : 1.0f;
        f32 pod = sys.popup_anim;
        if (p == 1 && sys.parts[PART_HEADLIGHTS].condition < 0.4f) {
            pod = f_min(pod, 0.38f);
        }
        draw_named(device, layout.popup_mesh,
                   offset_from(hood, Vec3{sign * layout.popup_offset.x, layout.popup_offset.y, layout.popup_offset.z},
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
        const Vec3 hinge = Vec3{sign * layout.door_hinge.x, layout.door_hinge.y, layout.door_hinge.z} - com;
        const Quat swing = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f},
                                                sign * sys.door_open[side] * layout.door_angle);
        draw_named(device, layout.door_mesh[side], offset_from(base, hinge, swing));
    }

    draw_named(device, layout.trunk_mesh,
               offset_from(base, layout.trunk_hinge - com,
                           quat_from_axis_angle(Vec3{1.0f, 0.0f, 0.0f}, -sys.trunk_open * layout.trunk_angle)));

    draw_named(device, layout.lever_mesh,
               offset_from(base, layout.lever - com,
                           quat_from_axis_angle(Vec3{1.0f, 0.0f, 0.0f},
                                                f_lerp(kLeverAngleRest, kLeverAngleSet, sys.lever_anim))));

    for (const CargoItem& c : sys.cargo) {
        if (!c.used) {
            continue;
        }
        const Quat cargo_rot = item_cargo_rot(c.item.kind);
        const Vec3 local = (c.pos - com) - rotate(cargo_rot, item_mesh_center(c.item.kind));
        device.draw_mesh(assets.mesh(item_mesh(c.item.kind)),
                         offset_from(base, local, cargo_rot));
    }

    draw_named(device, layout.fuelcap_mesh,
               offset_from(base, layout.fuelcap - com,
                           quat_from_axis_angle(Vec3{0.0f, 0.0f, 1.0f}, sys.cap_anim * 1.3f)));

    if (sys.key_inserted) {
        device.draw_mesh(assets.mesh("part_key"),
                         offset_from(base, layout.key - com));
    }

    const Vec3 wiper_axis = normalize(layout.wiper_axis);
    const f32 wiper_angle = -(0.20f + sys.wiper_sweep * 1.30f);
    for (f32 wx : layout.wiper_x) {
        device.draw_mesh(assets.mesh("part_wiper"),
                         offset_from(base, Vec3{wx, layout.wiper_base.y, layout.wiper_base.z} - com,
                                     quat_from_axis_angle(wiper_axis, wiper_angle)));
    }

    device.draw_mesh(assets.mesh("jack_coax"),
                     offset_from(base, layout.jack_coax - com));
    device.draw_mesh(assets.mesh("jack_bus"),
                     offset_from(base, layout.jack_bus - com));

    const Mat4 deck = offset_from(base, layout.deck - com);
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

    for (PartKind kind : {PART_TANK, PART_PRINTER}) {
        if (sys.parts[kind].installed) {
            device.draw_mesh(assets.mesh(part_def(kind).mesh), offset_from(base, part_def(kind).socket_pos - com));
        }
    }
    if (sys.parts[PART_PRINTER].installed) {
        device.draw_mesh(assets.mesh("jack_bus"), offset_from(base, car_layout().printer_jack - com));
    }

    if (sys.parts[PART_COIL].installed) {
        device.draw_mesh(assets.mesh(part_def(PART_COIL).mesh),
                         offset_from(base, part_def(PART_COIL).socket_pos - com));
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
    static const f32 kDialRate[4] = {8.0f, 14.0f, 1.0f, 1.0f};
    for (u32 d = 0; d < 4; d++) {
        dial_sm_[d] = dial_valid_ ? f_approach_exp(dial_sm_[d], dial_val[d], kDialRate[d], dt) : dial_val[d];
        const f32 needle = 2.27f - dial_sm_[d] * 4.54f;
        device.draw_mesh(assets.mesh("excel_needle"),
                         base * mat4_trs(Vec3{layout.dial_x[d], layout.dial_y[d], layout.dial_z} - com,
                                         quat_from_axis_angle(Vec3{0.0f, 0.0f, 1.0f}, needle),
                                         Vec3{layout.dial_scale[d], layout.dial_scale[d], layout.dial_scale[d]}));
    }
    dial_valid_ = true;

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
                         base * mat4_trs(layout.warn_first + Vec3{layout.warn_step * static_cast<f32>(w), 0.0f, 0.0f} - com,
                                         quat_identity(), Vec3{0.014f, 0.014f, 0.008f}));
    }

    if (veh.input().brake > 0.05f || veh.input().handbrake) {
        draw_named(device, layout.brakelight_mesh, offset_from(base, -com));
    }
    if (veh.train().gear == -1) {
        draw_named(device, layout.revlight_mesh, offset_from(base, -com));
    }

    const RigidBody* car = phys.body(veh.body());
    draw_sparks(device, car ? phys.gravity_at(car->pos) : phys.gravity(), dt);
}

} // namespace anom
