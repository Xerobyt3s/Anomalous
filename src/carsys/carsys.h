#pragma once

#include "carsys/cables.h"
#include "carsys/electrics.h"
#include "carsys/fluids.h"
#include "carsys/items.h"
#include "carsys/parts.h"
#include "core/types.h"
#include "math/vmath.h"

#include <string_view>

namespace anom {

class PhysWorld;
class Vehicle;

inline constexpr f32 kCrankTime = 0.7f;
inline constexpr u32 kCargoMax = 8;
inline constexpr i32 kCoaxTargetAntenna = 0;
inline constexpr i32 kCoaxTargetCamera = 1;
inline constexpr i32 kBusTargetCar = 0;
inline constexpr i32 kBusTargetTower = 1;
inline constexpr i32 kBusTargetPrinter = 2;
inline constexpr i32 kBusTargetLoosePrinter = 3;


inline constexpr u32 kSynthMaterials = 16;
inline constexpr u32 kSynthElements = 32;
inline constexpr u32 kSynthDoses = 3;

enum class SynthResult : u8 {
    None,
    Queued,
    Unstable,
    NoPropellant,
    NoStock,
    Busy,
    BadRecipe,
    NoHardware,
};

struct SynthBay {
    u16 tank[kSynthMaterials] = {};
    u16 tray[kSynthElements] = {};
    i32 job_element = -1;
    u16 job_total = 0;
    u16 job_left = 0;
    f32 progress = 0.0f;
    bool stalled = false;
    u32 serial = 0;
    SynthResult result = SynthResult::None;
    u32 result_serial = 0;

    u32 tank_total() const
    {
        u32 n = 0;
        for (u16 c : tank) {
            n += c;
        }
        return n;
    }
    u32 tray_total() const
    {
        u32 n = 0;
        for (u16 c : tray) {
            n += c;
        }
        return n;
    }
};

inline constexpr u32 kLooseTanks = 8;
inline constexpr u32 kLoosePrinters = 4;

struct LooseTank {
    bool used = false;
    u16 doses[kSynthMaterials] = {};

    u32 total() const
    {
        u32 n = 0;
        for (u16 c : doses) {
            n += c;
        }
        return n;
    }
};

struct LoosePrinter {
    bool used = false;
    bool has_tank = false;
    f32 tank_condition = 1.0f;
    SynthBay bay;
};

struct SynthTuning {
    f32 print_time = 3.0f;
    f32 battery_per_round = 0.01f;
    f32 min_battery = 0.15f;
    u32 tray_max = 24;
    u32 tank_capacity = 60;
    u32 start_each = 0;
    u32 start_propellant = 0;
};

struct CargoItem {
    Item item;
    Vec3 pos;
    Vec3 vel;
    bool used = false;
    bool supported = false;
};

enum class StartBlocker : u32 {
    None = 0,
    NoKey,
    NoFuel,
    EngineDead,
    BatteryFlat,
    StarterUnpowered,
    Running,
};

std::string_view start_blocker_text(StartBlocker blocker);

class CarSys {
public:
    void init();
    void tick(Vehicle& veh, PhysWorld& world, f32 dt);

    bool try_start(Vehicle& veh);
    void stop_engine();
    StartBlocker start_blocker() const;

    bool cargo_add(Item item, Vec3 pos);
    bool cargo_take(u32 index, Item& out_item);
    u32 cargo_count() const;
    f32 cargo_place_y(ItemKind kind, f32 x, f32 z, bool* out_ok) const;

    PartSlot parts[PART_COUNT];
    Electrics elec;
    Fluids fluids;
    Cable cables[CABLE_KIND_COUNT];
    CargoItem cargo[kCargoMax];
    SynthBay synth;
    LooseTank loose_tanks[kLooseTanks];
    LoosePrinter loose_printers[kLoosePrinters];
    i32 bus_printer = 0;

    bool engine_on = false;
    f32 crank_timer = 0.0f;
    bool headlight_switch = false;
    f32 hood_open = 0.0f;
    bool hood_target = false;
    f32 door_open[2] = {};
    bool door_target[2] = {};
    f32 trunk_open = 0.0f;
    bool trunk_target = false;
    bool handbrake_latched = true;
    f32 lever_anim = 1.0f;
    bool key_inserted = false;
    bool crank_request = false;
    bool crank_active = false;
    f32 crank_hold = 0.0f;
    bool fuel_cap_open = false;
    f32 cap_anim = 0.0f;
    f32 popup_anim = 0.0f;
    bool computer_on = false;
    i32 floppy_disk = -1;
    f32 floppy_cond = 1.0f;
    i32 tape_inserted = -1;
    f32 tape_cond = 1.0f;
    bool deck_play = false;
    i32 wiper_mode = 0;
    f32 wiper_phase = 0.0f;
    f32 wiper_sweep = 0.0f;
    f32 windshield_wet = 0.0f;
    f32 glass_wet = 0.0f;
    f32 rain_level = 0.0f;
    i32 coax_target = kCoaxTargetAntenna;
    i32 bus_target = kBusTargetCar;
    Vec3 prev_vel;
    f32 impact_cooldown = 1.0f;
    f32 last_impact_severity = 0.0f;
    u32 impact_serial = 0;
    f32 stall_notice = 0.0f;
    bool horn_on = false;

    i32 claim_tank();
    i32 claim_printer();
    LooseTank* loose_tank(i32 id);
    const LooseTank* loose_tank(i32 id) const;
    LoosePrinter* loose_printer(i32 id);
    const LoosePrinter* loose_printer(i32 id) const;
    i32 stash_tank(u16* doses);
    void fill_tank_from(i32 id, u16* doses);
    i32 stash_installed_printer();
    void install_printer_from(i32 id);
    bool bus_on_loose_printer(i32 id) const;
    SynthBay* bus_bay(bool& has_tank, bool& has_printer);
    const SynthBay* bus_bay(bool& has_tank, bool& has_printer) const;

private:
    bool can_run() const;
    f32 crank_time() const;
    void detect_impacts(Vehicle& veh, const struct RigidBody& body, Vec3 dv, f32 dt);
    void cargo_tick(Vec3 apparent, f32 dt);
    void synth_tick(const SynthTuning& tuning, f32 dt);
    void tick_bay(SynthBay& s, bool present, const SynthTuning& tuning, f32 dt);
};

} // namespace anom
