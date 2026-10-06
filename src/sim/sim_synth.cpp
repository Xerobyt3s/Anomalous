#include "sim/sim.h"
#include "terminal/programs/synth.h"

#include <algorithm>

namespace anom {
namespace {

constexpr f32 kTankReach = 6.0f;
constexpr u32 kMaxBatch = 99;

}

void Sim::queue_print(const u8* doses, u32 dose_count, u32 count)
{
    SynthBay& bay = carsys_.synth;
    const auto answer = [&](SynthResult result) {
        bay.result = result;
        bay.result_serial++;
    };
    if (!carsys_.parts[PART_TANK].installed || !carsys_.parts[PART_PRINTER].installed) {
        answer(SynthResult::NoHardware);
        return;
    }
    const ghost::game::AmmoData& ammo = gameplay_.ammo();
    if (dose_count == 0 || dose_count > kSynthDoses) {
        answer(SynthResult::BadRecipe);
        return;
    }
    u32 need[kSynthMaterials] = {};
    for (u32 i = 0; i < dose_count; i++) {
        if (doses[i] >= ammo.materials.size() || doses[i] >= kSynthMaterials) {
            answer(SynthResult::BadRecipe);
            return;
        }
        need[doses[i]]++;
    }
    const SynthPreview preview = synth_preview(ammo, bay.tank, doses, dose_count);
    if (preview.affordable == 0) {
        answer(SynthResult::NoStock);
        return;
    }
    const auto spend = [&](u32 sets) {
        for (u32 m = 0; m < kSynthMaterials; m++) {
            bay.tank[m] = static_cast<u16>(bay.tank[m] - need[m] * sets);
        }
    };
    if (preview.verdict == SynthVerdict::Unstable) {
        spend(1);
        answer(SynthResult::Unstable);
        return;
    }
    if (preview.verdict != SynthVerdict::Ready) {
        answer(preview.verdict == SynthVerdict::NoPropellant ? SynthResult::NoPropellant : SynthResult::BadRecipe);
        return;
    }
    if (bay.job_left > 0 && bay.job_element != preview.element) {
        answer(SynthResult::Busy);
        return;
    }
    const u32 batch = std::min({std::max(count, 1u), preview.affordable, kMaxBatch - std::min<u32>(bay.job_left, kMaxBatch)});
    if (batch == 0) {
        answer(SynthResult::Busy);
        return;
    }
    spend(batch);
    if (bay.job_left == 0) {
        bay.job_total = 0;
        bay.progress = 0.0f;
    }
    bay.job_element = preview.element;
    bay.job_total = static_cast<u16>(bay.job_total + batch);
    bay.job_left = static_cast<u16>(bay.job_left + batch);
    answer(SynthResult::Queued);
}

void Sim::fill_tank()
{
    if (!has_car() || !carsys_.parts[PART_TANK].installed) {
        return;
    }
    const RigidBody* car = phys_.body(vehicle_.body());
    ghost::game::MaterialInventory& carried = gameplay_.materials();
    if (!car || carried.total() == 0) {
        return;
    }
    bool near = false;
    for (const PlayerSlot& s : slots_) {
        near = near || (s.active && s.zombie_of == kNoPlayer && length(s.player.pos() - car->pos) < kTankReach);
    }
    if (!near) {
        return;
    }
    SynthBay& bay = carsys_.synth;
    const u32 capacity = vehicle_.config().synth.tank_capacity;
    for (std::size_t m = 0; m < carried.kinds() && m < kSynthMaterials; m++) {
        const auto id = static_cast<ghost::game::MaterialId>(m);
        while (carried.count(id) > 0 && bay.tank_total() < capacity) {
            carried.take(id);
            bay.tank[m]++;
        }
    }
}

void Sim::take_tray(PlayerSlot& s)
{
    SynthBay& bay = carsys_.synth;
    for (u32 e = 0; e < kSynthElements; e++) {
        for (u16 k = 0; k < bay.tray[e]; k++) {
            const ghost::game::Round round{static_cast<ghost::game::ElementId>(e)};
            if (owns(s)) {
                s.gun.pouch.add(round.element);
            } else {
                gives_out_.push_back({s.id, round});
            }
        }
        bay.tray[e] = 0;
    }
}

}
