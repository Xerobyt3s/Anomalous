#include "terminal/programs/synth.h"
#include "game/ammo/ammo_data.h"
#include "game/crafting/mortar.h"
#include "terminal/screen.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace anom {
namespace {

constexpr u32 kMaxCount = 99;
constexpr i32 kTankRow = 3;
constexpr i32 kRecipeRow = 12;
constexpr i32 kPrinterRow = 16;
constexpr i32 kResultRow = 19;

std::string upper(std::string_view text)
{
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

const char* result_text(SynthResult result)
{
    switch (result) {
    case SynthResult::Queued:
        return "BATCH QUEUED.";
    case SynthResult::Unstable:
        return "UNSTABLE MIX. ONE SET OF MATERIAL LOST.";
    case SynthResult::NoPropellant:
        return "REJECTED: A ROUND NEEDS ONE PROPELLANT.";
    case SynthResult::NoStock:
        return "REJECTED: NOT ENOUGH MATERIAL IN THE TANK.";
    case SynthResult::Busy:
        return "REJECTED: PRINTER IS BUSY WITH ANOTHER ROUND.";
    case SynthResult::BadRecipe:
        return "REJECTED: RECIPE NOT VALID.";
    case SynthResult::NoHardware:
        return "REJECTED: TANK OR PRINTER NOT INSTALLED.";
    default:
        return "";
    }
}

}

SynthPreview synth_preview(const ghost::game::AmmoData& ammo, const u16* tank, const u8* doses, u32 dose_count)
{
    SynthPreview out;
    if (dose_count == 0) {
        return out;
    }
    ghost::game::Mortar mortar(ammo);
    u32 need[kSynthMaterials] = {};
    for (u32 i = 0; i < dose_count; i++) {
        need[doses[i] % kSynthMaterials]++;
        const auto added = mortar.addDose(doses[i]);
        if (added == ghost::game::Mortar::AddResult::Fizzled) {
            out.verdict = SynthVerdict::Unstable;
        } else if (added == ghost::game::Mortar::AddResult::SecondPropellant) {
            out.verdict = SynthVerdict::TwoPropellants;
            return out;
        }
    }
    u32 sets = kMaxCount;
    for (u32 m = 0; m < kSynthMaterials; m++) {
        if (need[m] > 0) {
            sets = std::min(sets, static_cast<u32>(tank[m]) / need[m]);
        }
    }
    out.affordable = sets;
    if (out.verdict == SynthVerdict::Unstable) {
        return out;
    }
    if (!mortar.hasPropellant()) {
        out.verdict = SynthVerdict::NoPropellant;
        return out;
    }
    out.verdict = SynthVerdict::Ready;
    out.element = static_cast<i32>(mortar.resultElement());
    return out;
}

void SynthProgram::enter(const ProgramContext& ctx, const TermView& view)
{
    (void)ctx;
    (void)view;
}

void SynthProgram::update(const ProgramContext& ctx, const TermView& view, f32 dt)
{
    (void)dt;
    Screen& s = *ctx.screen;
    s.grid_clear();
    s.grid_title("SYNTH 1.2  ROUND PRINTER");
    if (!view.sys || !view.ammo) {
        s.grid_text(2, 2, TC_RED, "NO SYNTH HARDWARE ON THE BUS.");
        return;
    }
    const CarSys& sys = *view.sys;
    const SynthBay& bay = sys.synth;
    const ghost::game::AmmoData& ammo = *view.ammo;
    const bool tank = sys.parts[PART_TANK].installed;
    const bool printer = sys.parts[PART_PRINTER].installed;

    s.grid_text(kTankRow - 1, 2, TC_BRIGHT, "MATERIAL TANK%s", tank ? "" : "  -- NOT INSTALLED");
    const u32 shown = std::min<u32>(static_cast<u32>(ammo.materials.size()), 8);
    for (u32 m = 0; m < shown; m++) {
        const ghost::game::MaterialDef& def = ammo.materials[m];
        std::string role = def.propellant ? "PROPELLANT" : "";
        if (def.element) {
            role = upper(ammo.elements[*def.element].display);
        }
        const u8 color = bay.tank[m] > 0 ? TC_GREEN : TC_DIM;
        s.grid_text(kTankRow + static_cast<i32>(m), 2, color, "[%u] %-16s %4u  %s", m + 1, upper(def.display).c_str(),
                    static_cast<u32>(bay.tank[m]), role.c_str());
    }

    std::string recipe;
    for (u32 i = 0; i < dose_count_; i++) {
        recipe += (i ? " + " : "") + upper(ammo.materials[doses_[i]].display);
    }
    s.grid_text(kRecipeRow, 2, TC_GREEN, "RECIPE   %s", recipe.empty() ? "-- PICK UP TO 3 DOSES --" : recipe.c_str());
    const SynthPreview preview = synth_preview(ammo, bay.tank, doses_, dose_count_);
    switch (preview.verdict) {
    case SynthVerdict::Ready:
        s.grid_text(kRecipeRow + 1, 2, TC_BRIGHT, "RESULT   %s ROUND", upper(ammo.elements[static_cast<ghost::game::ElementId>(preview.element)].display).c_str());
        break;
    case SynthVerdict::Unstable:
        s.grid_text(kRecipeRow + 1, 2, TC_RED, "RESULT   UNSTABLE -- PRINTING WILL WASTE ONE SET");
        break;
    case SynthVerdict::NoPropellant:
        s.grid_text(kRecipeRow + 1, 2, TC_AMBER, "RESULT   NEEDS ONE PROPELLANT DOSE");
        break;
    case SynthVerdict::TwoPropellants:
        s.grid_text(kRecipeRow + 1, 2, TC_AMBER, "RESULT   ONLY ONE PROPELLANT PER ROUND");
        break;
    default:
        s.grid_text(kRecipeRow + 1, 2, TC_DIM, "RESULT   --");
        break;
    }
    s.grid_text(kRecipeRow + 2, 2, TC_GREEN, "COUNT    %2u   (STOCK FOR %u)", count_, preview.affordable);

    if (!printer) {
        s.grid_text(kPrinterRow, 2, TC_RED, "PRINTER  NOT INSTALLED");
    } else if (bay.job_left > 0) {
        const std::string name = upper(ammo.elements[static_cast<ghost::game::ElementId>(static_cast<u32>(bay.job_element) % ammo.elements.size())].display);
        s.grid_text(kPrinterRow, 2, bay.stalled ? TC_AMBER : TC_GREEN, "PRINTER  %s %u/%u%s", name.c_str(),
                    static_cast<u32>(bay.job_total - bay.job_left), static_cast<u32>(bay.job_total),
                    bay.stalled ? "  STALLED: POWER OR TRAY" : "");
        s.grid_bar(kPrinterRow, 44, 20, bay.progress, TC_GREEN);
    } else {
        s.grid_text(kPrinterRow, 2, TC_GREEN, "PRINTER  IDLE");
    }
    s.grid_text(kPrinterRow + 1, 2, TC_GREEN, "TRAY     %u ROUNDS", bay.tray_total());
    if (bay.result != SynthResult::None) {
        s.grid_text(kResultRow, 2, bay.result == SynthResult::Queued ? TC_BRIGHT : TC_RED, "%s", result_text(bay.result));
    }
    s.grid_text(22, 2, TC_DIM, "1-8 ADD DOSE  BKSP REMOVE  LEFT/RIGHT COUNT  ENTER PRINT  ESC QUIT");
}

bool SynthProgram::key(const ProgramContext& ctx, const TermView& view, TermKey key)
{
    (void)view;
    switch (key) {
    case TermKey::Backspace:
        dose_count_ = dose_count_ > 0 ? dose_count_ - 1 : 0;
        return false;
    case TermKey::Left:
    case TermKey::Down:
        count_ = count_ > 1 ? count_ - 1 : 1;
        return false;
    case TermKey::Right:
    case TermKey::Up:
        count_ = std::min(count_ + 1, kMaxCount);
        return false;
    case TermKey::Enter:
        if (dose_count_ > 0 && ctx.request) {
            ctx.request->synth_print = true;
            ctx.request->synth_dose_count = dose_count_;
            for (u32 i = 0; i < dose_count_; i++) {
                ctx.request->synth_doses[i] = doses_[i];
            }
            ctx.request->synth_count = count_;
        }
        return false;
    default:
        return Program::key(ctx, view, key);
    }
}

void SynthProgram::key_char(const ProgramContext& ctx, const TermView& view, char c)
{
    (void)ctx;
    if (c >= '1' && c <= '8' && view.ammo) {
        const u32 material = static_cast<u32>(c - '1');
        if (material < view.ammo->materials.size() && dose_count_ < kSynthDoses) {
            doses_[dose_count_++] = static_cast<u8>(material);
        }
    } else if (c == '+') {
        count_ = std::min(count_ + 1, kMaxCount);
    } else if (c == '-') {
        count_ = count_ > 1 ? count_ - 1 : 1;
    }
}

}
