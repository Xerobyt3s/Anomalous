#include "carsys/carsys.h"
#include "physics/world.h"
#include "vehicle/vehicle.h"

namespace anom {
namespace {

constexpr f32 kBumpStartSpeed = 2.5f;
constexpr f32 kColdCrankExtra = 1.1f;
constexpr f32 kImpactAccelThreshold = 110.0f;
constexpr f32 kImpactSeverityScale = 0.0022f;
constexpr f32 kImpactCooldown = 0.25f;
constexpr f32 kStallNotice = 3.0f;
constexpr f32 kStarterMinCharge = 0.12f;
constexpr f32 kOverrevDmgPerS = 0.02f;
constexpr f32 kCrankRpm = 350.0f;
constexpr f32 kBumpStartRpm = 250.0f;
constexpr f32 kRpmToRad = kTau / 60.0f;

bool cargo_overlap_xz(Vec3 pos_a, Vec3 half_a, Vec3 pos_b, Vec3 half_b)
{
    return f_abs(pos_a.x - pos_b.x) < half_a.x + half_b.x - 0.005f
        && f_abs(pos_a.z - pos_b.z) < half_a.z + half_b.z - 0.005f;
}

} // namespace

void CarSys::init()
{
    *this = CarSys{};
    parts_init(parts);
    elec.init();
    fluids.init();
    impact_cooldown = 1.0f;
    handbrake_latched = true;
    lever_anim = 1.0f;
    floppy_disk = -1;
    tape_inserted = -1;
}

bool CarSys::cargo_add(Item item, Vec3 pos)
{
    for (u32 i = 0; i < kCargoMax; i++) {
        if (cargo[i].used) {
            continue;
        }
        cargo[i].used = true;
        cargo[i].item = item;
        cargo[i].pos = pos;
        cargo[i].vel = Vec3{0.0f, 0.0f, 0.0f};
        cargo[i].supported = true;
        return true;
    }
    return false;
}

bool CarSys::cargo_take(u32 index, Item& out_item)
{
    if (index >= kCargoMax || !cargo[index].used) {
        return false;
    }
    out_item = cargo[index].item;
    cargo[index].used = false;
    return true;
}

u32 CarSys::cargo_count() const
{
    u32 count = 0;
    for (u32 i = 0; i < kCargoMax; i++) {
        count += cargo[i].used ? 1 : 0;
    }
    return count;
}

f32 CarSys::cargo_place_y(ItemKind kind, f32 x, f32 z, bool* out_ok) const
{
    const Vec3 half = item_cargo_half(kind);
    const Vec3 at{x, 0.0f, z};
    f32 y = car_layout().trunk_min.y + half.y;
    for (u32 j = 0; j < kCargoMax; j++) {
        const CargoItem& other = cargo[j];
        if (!other.used) {
            continue;
        }
        const Vec3 other_half = item_cargo_half(other.item.kind);
        if (cargo_overlap_xz(at, half, other.pos, other_half)) {
            y = f_max(y, other.pos.y + other_half.y + half.y);
        }
    }
    if (out_ok) {
        *out_ok = y + half.y <= car_layout().trunk_max.y + 0.12f;
    }
    return y;
}

void CarSys::cargo_tick(Vec3 apparent, f32 dt)
{
    for (u32 i = 0; i < kCargoMax; i++) {
        CargoItem& c = cargo[i];
        if (!c.used) {
            continue;
        }
        const Vec3 half = item_cargo_half(c.item.kind);
        c.vel += apparent * dt;
        c.vel *= std::exp(-(c.supported ? 8.0f : 0.5f) * dt);
        const f32 shove = std::sqrt(apparent.x * apparent.x + apparent.z * apparent.z);
        if (c.supported && shove < 3.5f && length(c.vel) < 0.3f) {
            c.vel = Vec3{0.0f, 0.0f, 0.0f};
        }
        c.pos += c.vel * dt;

        const f32 lo_x = car_layout().trunk_min.x + half.x;
        const f32 hi_x = f_max(car_layout().trunk_max.x - half.x, lo_x);
        const f32 lo_z = car_layout().trunk_min.z + half.z;
        const f32 hi_z = f_max(car_layout().trunk_max.z - half.z, lo_z);
        if (c.pos.x < lo_x || c.pos.x > hi_x) {
            c.pos.x = f_clamp(c.pos.x, lo_x, hi_x);
            c.vel.x *= -0.2f;
        }
        if (c.pos.z < lo_z || c.pos.z > hi_z) {
            c.pos.z = f_clamp(c.pos.z, lo_z, hi_z);
            c.vel.z *= -0.2f;
        }
    }

    for (u32 i = 0; i < kCargoMax; i++) {
        CargoItem& a = cargo[i];
        if (!a.used) {
            continue;
        }
        const Vec3 half_a = item_cargo_half(a.item.kind);
        for (u32 j = i + 1; j < kCargoMax; j++) {
            CargoItem& b = cargo[j];
            if (!b.used) {
                continue;
            }
            const Vec3 half_b = item_cargo_half(b.item.kind);
            if (!cargo_overlap_xz(a.pos, half_a, b.pos, half_b)) {
                continue;
            }
            if (f_abs(a.pos.y - b.pos.y) >= half_a.y + half_b.y - 0.01f) {
                continue;
            }
            const f32 pen_x = half_a.x + half_b.x - f_abs(a.pos.x - b.pos.x);
            const f32 pen_z = half_a.z + half_b.z - f_abs(a.pos.z - b.pos.z);
            if (pen_x < pen_z) {
                const f32 dir = a.pos.x <= b.pos.x ? -1.0f : 1.0f;
                a.pos.x += dir * pen_x * 0.5f;
                b.pos.x -= dir * pen_x * 0.5f;
                a.vel.x *= 0.2f;
                b.vel.x *= 0.2f;
            } else {
                const f32 dir = a.pos.z <= b.pos.z ? -1.0f : 1.0f;
                a.pos.z += dir * pen_z * 0.5f;
                b.pos.z -= dir * pen_z * 0.5f;
                a.vel.z *= 0.2f;
                b.vel.z *= 0.2f;
            }
        }
    }

    for (u32 i = 0; i < kCargoMax; i++) {
        CargoItem& c = cargo[i];
        if (!c.used) {
            continue;
        }
        const Vec3 half = item_cargo_half(c.item.kind);
        f32 rest_y = car_layout().trunk_min.y + half.y;
        for (u32 j = 0; j < kCargoMax; j++) {
            const CargoItem& other = cargo[j];
            if (j == i || !other.used) {
                continue;
            }
            const Vec3 other_half = item_cargo_half(other.item.kind);
            if (cargo_overlap_xz(c.pos, half, other.pos, other_half)
                && other.pos.y + other_half.y <= c.pos.y + 0.02f) {
                rest_y = f_max(rest_y, other.pos.y + other_half.y + half.y);
            }
        }
        if (c.pos.y < rest_y) {
            c.pos.y = rest_y;
            c.vel.y *= -0.2f;
        }
        const f32 hi_y = f_max(car_layout().trunk_max.y - half.y, rest_y);
        if (c.pos.y > hi_y && c.vel.y > 0.0f) {
            c.pos.y = hi_y;
            c.vel.y *= -0.2f;
        }
        c.supported = c.pos.y <= rest_y + 0.005f;
    }
}

bool CarSys::can_run() const
{
    return parts[PART_ENGINE].condition > 0.02f && fluids.fuel > 0.0f;
}

f32 CarSys::crank_time() const
{
    const f32 cold = f_clamp01((45.0f - fluids.coolant_temp) / 45.0f);
    return kCrankTime + kColdCrankExtra * cold;
}

bool CarSys::try_start(Vehicle& veh)
{
    if (engine_on || crank_timer > 0.0f || !can_run()) {
        return false;
    }
    key_inserted = true;
    if (drivetrain_rpm(veh.train()) > kBumpStartRpm && elec.powered[CONSUMER_IGNITION]) {
        engine_on = true;
        return true;
    }
    crank_timer = crank_time();
    return true;
}

void CarSys::stop_engine()
{
    engine_on = false;
    crank_timer = 0.0f;
}

StartBlocker CarSys::start_blocker() const
{
    if (engine_on) {
        return StartBlocker::Running;
    }
    if (!key_inserted) {
        return StartBlocker::NoKey;
    }
    if (fluids.fuel <= 0.0f) {
        return StartBlocker::NoFuel;
    }
    if (parts[PART_ENGINE].condition <= 0.02f) {
        return StartBlocker::EngineDead;
    }
    if (elec.battery_charge <= kStarterMinCharge) {
        return StartBlocker::BatteryFlat;
    }
    if (!parts[PART_BATTERY].installed) {
        return StartBlocker::StarterUnpowered;
    }
    return StartBlocker::None;
}

std::string_view start_blocker_text(StartBlocker blocker)
{
    switch (blocker) {
    case StartBlocker::NoKey:
        return "no key";
    case StartBlocker::NoFuel:
        return "no fuel";
    case StartBlocker::EngineDead:
        return "engine dead";
    case StartBlocker::BatteryFlat:
        return "battery flat";
    case StartBlocker::StarterUnpowered:
        return "starter unpowered";
    case StartBlocker::Running:
        return "running";
    case StartBlocker::None:
        break;
    }
    return "";
}

void CarSys::detect_impacts(Vehicle& veh, const RigidBody& body, Vec3 dv, f32 dt)
{
    impact_cooldown = f_max(impact_cooldown - dt, 0.0f);
    const f32 accel = length(dv) / dt;
    if (accel <= kImpactAccelThreshold || impact_cooldown > 0.0f) {
        return;
    }
    impact_cooldown = kImpactCooldown;
    const f32 severity = (accel - kImpactAccelThreshold) * kImpactSeverityScale;
    last_impact_severity = severity;
    impact_serial++;

    const Vec3 dir_local = rotate(conjugate(body.rot), normalize(-dv));
    const Vec3 he = veh.config().half_extents;
    const Vec3 point{f_clamp(dir_local.x * 2.0f, -1.0f, 1.0f) * he.x,
                     f_clamp(dir_local.y * 2.0f, -1.0f, 1.0f) * he.y * 0.5f,
                     f_clamp(dir_local.z * 2.0f, -1.0f, 1.0f) * he.z};
    parts_apply_impact(parts, point + veh.config().com_offset, severity);
}

namespace {

void move_print_job(SynthBay& to, SynthBay& from)
{
    for (u32 e = 0; e < kSynthElements; e++) {
        to.tray[e] = from.tray[e];
        from.tray[e] = 0;
    }
    to.job_element = from.job_element;
    to.job_total = from.job_total;
    to.job_left = from.job_left;
    to.progress = from.progress;
    to.stalled = false;
    from.job_element = -1;
    from.job_total = 0;
    from.job_left = 0;
    from.progress = 0.0f;
    from.stalled = false;
}

}

i32 CarSys::claim_tank()
{
    for (u32 i = 0; i < kLooseTanks; i++) {
        if (!loose_tanks[i].used) {
            loose_tanks[i] = LooseTank{};
            loose_tanks[i].used = true;
            return static_cast<i32>(i + 1);
        }
    }
    return 0;
}

i32 CarSys::claim_printer()
{
    for (u32 i = 0; i < kLoosePrinters; i++) {
        if (!loose_printers[i].used) {
            loose_printers[i] = LoosePrinter{};
            loose_printers[i].used = true;
            return static_cast<i32>(i + 1);
        }
    }
    return 0;
}

LooseTank* CarSys::loose_tank(i32 id)
{
    return id >= 1 && id <= static_cast<i32>(kLooseTanks) && loose_tanks[id - 1].used ? &loose_tanks[id - 1] : nullptr;
}

const LooseTank* CarSys::loose_tank(i32 id) const
{
    return id >= 1 && id <= static_cast<i32>(kLooseTanks) && loose_tanks[id - 1].used ? &loose_tanks[id - 1] : nullptr;
}

LoosePrinter* CarSys::loose_printer(i32 id)
{
    return id >= 1 && id <= static_cast<i32>(kLoosePrinters) && loose_printers[id - 1].used ? &loose_printers[id - 1]
                                                                                            : nullptr;
}

const LoosePrinter* CarSys::loose_printer(i32 id) const
{
    return id >= 1 && id <= static_cast<i32>(kLoosePrinters) && loose_printers[id - 1].used ? &loose_printers[id - 1]
                                                                                            : nullptr;
}

i32 CarSys::stash_tank(u16* doses)
{
    const i32 id = claim_tank();
    LooseTank* tank = loose_tank(id);
    for (u32 m = 0; m < kSynthMaterials; m++) {
        if (tank) {
            tank->doses[m] = doses[m];
        }
        doses[m] = 0;
    }
    return id;
}

void CarSys::fill_tank_from(i32 id, u16* doses)
{
    LooseTank* tank = loose_tank(id);
    for (u32 m = 0; m < kSynthMaterials; m++) {
        doses[m] = tank ? tank->doses[m] : 0;
    }
    if (tank) {
        tank->used = false;
    }
}

i32 CarSys::stash_installed_printer()
{
    const i32 id = claim_printer();
    if (LoosePrinter* printer = loose_printer(id)) {
        move_print_job(printer->bay, synth);
    }
    return id;
}

void CarSys::install_printer_from(i32 id)
{
    LoosePrinter* printer = loose_printer(id);
    if (!printer) {
        return;
    }
    move_print_job(synth, printer->bay);
    if (printer->has_tank && !parts[PART_TANK].installed) {
        parts[PART_TANK].installed = true;
        parts[PART_TANK].condition = printer->tank_condition;
        for (u32 m = 0; m < kSynthMaterials; m++) {
            synth.tank[m] = printer->bay.tank[m];
        }
    }
    printer->used = false;
}

bool CarSys::bus_on_loose_printer(i32 id) const
{
    return id != 0 && bus_target == kBusTargetLoosePrinter && bus_printer == id
        && cables[CABLE_BUS].state == CableState::Plugged;
}

SynthBay* CarSys::bus_bay(bool& has_tank, bool& has_printer)
{
    if (bus_target == kBusTargetLoosePrinter) {
        LoosePrinter* printer = loose_printer(bus_printer);
        has_printer = printer != nullptr;
        has_tank = printer && printer->has_tank;
        return printer ? &printer->bay : nullptr;
    }
    has_printer = parts[PART_PRINTER].installed;
    has_tank = parts[PART_TANK].installed;
    return &synth;
}

const SynthBay* CarSys::bus_bay(bool& has_tank, bool& has_printer) const
{
    return const_cast<CarSys*>(this)->bus_bay(has_tank, has_printer);
}

void CarSys::synth_tick(const SynthTuning& tuning, f32 dt)
{
    tick_bay(synth, parts[PART_PRINTER].installed, tuning, dt);
    for (u32 i = 0; i < kLoosePrinters; i++) {
        if (loose_printers[i].used) {
            tick_bay(loose_printers[i].bay, bus_on_loose_printer(static_cast<i32>(i + 1)), tuning, dt);
        }
    }
}

void CarSys::tick_bay(SynthBay& s, bool present, const SynthTuning& tuning, f32 dt)
{
    s.stalled = false;
    if (s.job_left == 0 || s.job_element < 0) {
        s.progress = 0.0f;
        return;
    }
    if (!present || s.tray_total() >= tuning.tray_max || elec.battery_charge < tuning.min_battery) {
        s.stalled = true;
        return;
    }
    s.progress += dt / f_max(tuning.print_time, 0.05f);
    elec.battery_charge = f_max(elec.battery_charge - tuning.battery_per_round * dt / f_max(tuning.print_time, 0.05f), 0.0f);
    if (s.progress < 1.0f) {
        return;
    }
    s.progress = 0.0f;
    s.tray[static_cast<u32>(s.job_element) % kSynthElements]++;
    s.job_left--;
    s.serial++;
    if (s.job_left == 0) {
        s.job_element = -1;
    }
}

void CarSys::tick(Vehicle& veh, PhysWorld& world, f32 dt)
{
    RigidBody* body = world.body(veh.body());
    if (!body) {
        return;
    }
    synth_tick(veh.config().synth, dt);

    const Vec3 dv = body->vel - prev_vel;
    prev_vel = body->vel;
    detect_impacts(veh, *body, dv, dt);

    const Vec3 accel_world = dv * (1.0f / dt);
    const Vec3 apparent = rotate(conjugate(body->rot), world.gravity_at(body->pos) - accel_world);
    cargo_tick(apparent, dt);

    const f32 rpm = drivetrain_rpm(veh.train());
    const f32 idle_rpm = veh.config().idle_rpm;
    const bool cranking = crank_timer > 0.0f
                       || (key_inserted && crank_request && !engine_on);

    ElectricsInput ein;
    ein.rpm = rpm;
    ein.idle_rpm = idle_rpm;
    ein.engine_on = engine_on;
    ein.cranking = cranking;
    ein.headlights_switch = headlight_switch;
    ein.deck_on = deck_play;
    ein.wipers_on = wiper_mode > 0;
    elec.tick(parts, ein, dt);

    if (deck_play && !elec.powered[CONSUMER_DECK]) {
        deck_play = false;
    }

    const bool wipers_run = wiper_mode > 0 && elec.powered[CONSUMER_WIPERS];
    if (wipers_run) {
        const f32 cycle = wiper_mode == 1 ? 2.6f : 1.0f;
        wiper_phase += dt / cycle;
        if (wiper_phase >= 1.0f) {
            wiper_phase -= 1.0f;
        }
    } else {
        wiper_phase = f_approach_exp(wiper_phase, 0.0f, 4.0f, dt);
    }
    const f32 travel = wiper_mode == 1 ? f_clamp01(wiper_phase * 1.6f) : wiper_phase;
    const f32 prev_sweep = wiper_sweep;
    wiper_sweep = travel < 0.5f ? travel * 2.0f : (1.0f - travel) * 2.0f;
    if (wipers_run) {
        windshield_wet = f_max(windshield_wet - f_abs(wiper_sweep - prev_sweep) * 0.75f, 0.0f);
    }
    windshield_wet = f_clamp01(windshield_wet + rain_level * 0.05f * dt
                               - (1.0f - rain_level) * 0.012f * dt);
    glass_wet = f_clamp01(glass_wet + rain_level * 0.05f * dt
                          - (1.0f - rain_level) * 0.010f * dt);

    if (cranking) {
        if (!elec.powered[CONSUMER_STARTER] || !can_run()) {
            crank_timer = 0.0f;
        } else {
            veh.train().engine_omega = f_max(veh.train().engine_omega, kCrankRpm * kRpmToRad);
            crank_timer -= dt;
            if (crank_timer <= 0.0f) {
                crank_timer = 0.0f;
                if (elec.powered[CONSUMER_FUEL_PUMP] && elec.powered[CONSUMER_IGNITION]) {
                    engine_on = true;
                }
            }
        }
    }

    crank_active = false;
    if (key_inserted && !engine_on && crank_request && can_run()) {
        const f32 roll_speed = length(body->vel);
        if (roll_speed > kBumpStartSpeed && elec.powered[CONSUMER_IGNITION]
            && elec.powered[CONSUMER_FUEL_PUMP]) {
            engine_on = true;
        } else if (elec.powered[CONSUMER_STARTER]) {
            crank_active = true;
            veh.train().engine_omega = f_max(veh.train().engine_omega, kCrankRpm * kRpmToRad);
            crank_hold += dt;
            if (crank_hold >= crank_time() && elec.powered[CONSUMER_FUEL_PUMP]
                && elec.powered[CONSUMER_IGNITION]) {
                engine_on = true;
                crank_hold = 0.0f;
            }
        }
    }
    if (!crank_request || engine_on) {
        crank_hold = 0.0f;
    }

    stall_notice = f_max(stall_notice - dt, 0.0f);
    if (engine_on) {
        if (!can_run() || !elec.powered[CONSUMER_FUEL_PUMP]
            || !elec.powered[CONSUMER_IGNITION]) {
            engine_on = false;
            stall_notice = kStallNotice;
        }
    }

    if (computer_on && parts[PART_COMPUTER].installed && elec.battery_charge < 0.02f) {
        computer_on = false;
    }

    const f32 speed = length(body->vel);
    FluidsInput fin;
    fin.rpm = rpm;
    fin.max_rpm = veh.config().max_rpm;
    fin.throttle = veh.input().throttle;
    fin.engine_on = engine_on;
    fin.fuel_pump_powered = elec.powered[CONSUMER_FUEL_PUMP];
    fin.speed = speed;
    fluids.tick(parts, fin, dt);

    if (rpm > veh.config().max_rpm * 0.985f) {
        parts[PART_ENGINE].condition = f_max(parts[PART_ENGINE].condition
                                                 - kOverrevDmgPerS * dt,
                                             0.0f);
    }

    hood_open = f_approach_exp(hood_open, hood_target ? 1.0f : 0.0f, 9.0f, dt);
    trunk_open = f_approach_exp(trunk_open, trunk_target ? 1.0f : 0.0f, 9.0f, dt);
    for (u32 i = 0; i < 2; i++) {
        door_open[i] = f_approach_exp(door_open[i], door_target[i] ? 1.0f : 0.0f, 12.0f, dt);
    }
    lever_anim = f_approach_exp(lever_anim, handbrake_latched ? 1.0f : 0.0f, 14.0f, dt);
    cap_anim = f_approach_exp(cap_anim, fuel_cap_open ? 1.0f : 0.0f, 10.0f, dt);

    VehicleEffects& fx = veh.effects();
    const f32 engine_cond = parts[PART_ENGINE].condition;
    fx.engine_power_mul = (0.35f + 0.65f * engine_cond) * fluids.overheat_power_mul();
    fx.ignition_ok = engine_on;
    for (u32 i = 0; i < kWheelCount; i++) {
        const PartSlot& tire = parts[PART_TIRE_FL + i];
        if (!tire.installed) {
            fx.tire_radius_mul[i] = 0.64f;
            fx.tire_grip_mul[i] = 0.15f;
        } else {
            const f32 inflate = f_clamp01(tire.condition / 0.25f);
            fx.tire_radius_mul[i] = f_lerp(0.85f, 1.0f, inflate);
            fx.tire_grip_mul[i] = 0.35f + 0.65f * f_clamp01(tire.condition * 1.4f);
        }
    }
    fx.brake_mul = 1.0f;
    fx.headlights_on = headlight_switch && elec.powered[CONSUMER_HEADLIGHTS]
                    && parts[PART_HEADLIGHTS].condition > 0.1f;
    popup_anim = f_approach_exp(popup_anim, fx.headlights_on ? 1.0f : 0.0f, 6.0f, dt);
}

} // namespace anom
