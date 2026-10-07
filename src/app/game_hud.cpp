#include "app/game.h"
#include "carsys/items.h"
#include "game/ghosts/ghost_poses.h"
#include "math/glm_bridge.h"
#include "render/debug_draw.h"

#include <imgui.h>

#include <cstdio>
#include <string_view>

namespace anom {
namespace {

constexpr f32 kSpeedSize = 26.0f;
constexpr f32 kReadoutSize = 20.0f;
constexpr f32 kPromptSize = 20.0f;
constexpr f32 kHoldingSize = 18.0f;
constexpr f32 kNoteSize = 17.0f;
constexpr f32 kHoldBarWidth = 160.0f;
constexpr f32 kFuelBarWidth = 120.0f;
constexpr u64 kControlsCardTicks = 720;
constexpr f32 kCrosshair = 6.0f;
constexpr f32 kNameLift = 0.25f;

void text_at(ImDrawList* draw, f32 size, ImVec2 at, ImU32 color, const char* text)
{
    draw->AddText(ImGui::GetFont(), size, {at.x + 1.0f, at.y + 1.0f}, IM_COL32(0, 0, 0, 160), text);
    draw->AddText(ImGui::GetFont(), size, at, color, text);
}

void text_centered(ImDrawList* draw, f32 size, f32 cx, f32 y, ImU32 color, const char* text)
{
    const ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    text_at(draw, size, {cx - extent.x * 0.5f, y}, color, text);
}

void plot(const char* label, const Telemetry& t, f32 lo, f32 hi)
{
    ImGui::PlotLines(label, t.samples, static_cast<int>(kTelemetrySamples), static_cast<int>(t.head), nullptr, lo, hi,
                     {0.0f, 48.0f});
}

}

void Game::draw_play_hud()
{
    if (editor_.active() || toggles_.free_cam || terminal_focused()) {
        return;
    }
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    const f32 cx = size.x * 0.5f;
    const f32 cy = size.y * 0.5f;
    char line[160];

    if (play_) {
        HudFrame hud;
        hud.viewProj = to_glm(hud_view_proj_);
        hud.roster = &sim_.roster();
        hud.rules = &sim_.player_rules();
        for (const PlayerSlot& slot : sim_.slots()) {
            if (!slot.active || slot.id == local_ || !bodies_.visible(slot.id)) {
                continue;
            }
            const Player& p = slot.player;
            const Vec3 up = rotate(p.movement().state().frame, Vec3{0.0f, 1.0f, 0.0f});
            const Vec3 feet = lerp(p.prev_pos(), p.pos(), alpha_);
            HudPlayer other;
            other.id = slot.id;
            other.name = std::string(slot.name.view());
            other.color = to_glm(slot.color);
            other.feet = to_glm(feet);
            other.head = to_glm(feet + up * (p.movement().state().height + kNameLift));
            other.shroudFade = p.movement().state().shroud_time > 0.0f ? 1.0f : 0.0f;
            hud.others.push_back(std::move(other));
        }
        if (const PlayerSlot* me = sim_.slot(local_)) {
            hud.pickupProgress = me->gun.pickupProgress;
        }
        for (const PlayerSlot& slot : sim_.slots()) {
            const ghost::game::RosterEntry* entry = sim_.roster().find(slot.id);
            if (slot.active && entry && !entry->zombie) {
                hud.score.push_back({std::string(slot.name.view()) + (slot.id == local_ ? " (you)" : ""), entry->kills,
                                     slot.id == local_ ? glm::vec3(1.0f) : to_glm(slot.color)});
            }
        }
        play_->drawHud(hud);
    }
    if (sim_.roster().downed(local_)) {
        return;
    }

    if (!gun_out() && !viewfinder_) {
        draw->AddLine({cx - kCrosshair, cy}, {cx + kCrosshair, cy}, kDdWhite);
        draw->AddLine({cx, cy - kCrosshair}, {cx, cy + kCrosshair}, kDdWhite);
    }

    const bool driving_now = sim_.player(local_).driving();
    if (driving_now && !was_driving_) {
        seat_tick_ = sim_.tick_count();
    }
    was_driving_ = driving_now;
    if (driving_now) {
        const Vehicle& veh = sim_.vehicle();
        const CarSys& cs = sim_.carsys();
        const f32 speed = f_abs(veh.forward_speed(sim_.phys())) * 3.6f;
        const i32 gear = veh.train().gear;
        std::snprintf(line, sizeof(line), "%3.0f km/h", static_cast<f64>(speed));
        text_at(draw, kSpeedSize, {24.0f, size.y - 120.0f - kSpeedSize}, kDdWhite, line);
        char gear_text[8];
        if (gear < 0) {
            std::snprintf(gear_text, sizeof(gear_text), "R");
        } else if (gear == 0) {
            std::snprintf(gear_text, sizeof(gear_text), "N");
        } else {
            std::snprintf(gear_text, sizeof(gear_text), "%d", gear);
        }
        std::snprintf(line, sizeof(line), "%4.0f rpm  gear %s %s", static_cast<f64>(drivetrain_rpm(veh.train())), gear_text,
                      veh.train().manual ? "[M]" : "[A]");
        text_at(draw, kReadoutSize, {24.0f, size.y - 88.0f - kReadoutSize}, kDdWhite, line);

        const char* state = "OFF";
        u32 state_color = kDdGray;
        const StartBlocker blocker = cs.start_blocker();
        if (cs.engine_on) {
            state = "RUNNING";
            state_color = kDdWhite;
        } else if (cs.crank_active) {
            state = "CRANKING";
            state_color = kDdYellow;
        } else if (cs.stall_notice > 0.0f) {
            state = "STALLED";
            state_color = kDdOrange;
        } else if (cs.crank_request && blocker != StartBlocker::None) {
            state = start_blocker_text(blocker).data();
            state_color = kDdOrange;
        }
        std::snprintf(line, sizeof(line), "engine %s", state);
        text_at(draw, kReadoutSize, {24.0f, size.y - 60.0f - kReadoutSize}, state_color, line);

        const f32 fuel = f_clamp01(cs.fluids.fuel);
        const f32 bx = 24.0f;
        const f32 by = size.y - 44.0f;
        draw->AddRect({bx, by}, {bx + kFuelBarWidth, by + 6.0f}, kDdGray);
        draw->AddRectFilled({bx, by}, {bx + kFuelBarWidth * fuel, by + 6.0f}, fuel < 0.15f ? kDdOrange : kDdWhite);
        f32 gx = bx + kFuelBarWidth + 12.0f;
        if (cs.handbrake_latched) {
            text_at(draw, kReadoutSize, {gx, by - 6.0f}, kDdOrange, "(P)");
            gx += 46.0f;
        }
        if (cs.key_inserted) {
            text_at(draw, kReadoutSize, {gx, by - 6.0f}, kDdWhite, "KEY");
            gx += 52.0f;
        }
        if (veh.effects().headlights_on) {
            text_at(draw, kReadoutSize, {gx, by - 6.0f}, kDdWhite, "LIGHTS");
        }

        if (!controls_card_done_ && sim_.tick_count() - seat_tick_ < kControlsCardTicks) {
            text_centered(draw, kNoteSize, cx, size.y * 0.80f, kDdGray,
                          "[I] ignition  [Space] handbrake  [L] lights  [H] horn  [X] wipers  [C] camera  [V] look back");
        } else if (sim_.tick_count() - seat_tick_ >= kControlsCardTicks) {
            controls_card_done_ = true;
        }
    }

    const Interact& interact = sim_.interact(local_);
    const std::string_view prompt = interact.prompt();
    if (!prompt.empty() && !(play_ && play_->radialOpen())) {
        std::snprintf(line, sizeof(line), "%.*s", static_cast<int>(prompt.size()), prompt.data());
        text_centered(draw, kPromptSize, cx, cy + 60.0f - kPromptSize, kDdWhite, line);
        if (interact.action_is_hold() && interact.hold_progress() > 0.0f) {
            const f32 x0 = cx - kHoldBarWidth * 0.5f;
            draw->AddRect({x0, cy + 74.0f}, {x0 + kHoldBarWidth, cy + 82.0f}, kDdGray);
            draw->AddRectFilled({x0, cy + 74.0f}, {x0 + kHoldBarWidth * interact.hold_progress(), cy + 82.0f}, kDdYellow);
        }
    }

    if (interact.hands().kind != ITEM_NONE) {
        const std::string_view name = item_name(interact.hands().kind);
        std::snprintf(line, sizeof(line), "holding %.*s", static_cast<int>(name.size()), name.data());
        text_at(draw, kHoldingSize, {24.0f, 28.0f - kHoldingSize * 0.5f}, kDdYellow, line);
        if (throw_charge_ > 0.0f) {
            std::snprintf(line, sizeof(line), "throw %.0f%%", static_cast<f64>(throw_charge_ / kThrowChargeMax * 100.0f));
            text_at(draw, 16.0f, {24.0f, 50.0f - 8.0f}, kDdOrange, line);
        }
        if (place_active_) {
            text_centered(draw, kNoteSize, cx, size.y * 0.66f - kNoteSize, place_valid_ ? kDdWhite : kDdGray,
                          place_valid_ ? "release [F] to place | scroll to rotate" : "no surface to place on");
        }
    }

    if (const PlayerSlot* me = sim_.slot(local_); me && me->at_bench) {
        const bool full = me->gun.speedloaders.loaders[0].count() + me->gun.speedloaders.loaders[1].count()
                       == ghost::game::kSpeedloaderSlots * ghost::game::kSpeedloadersCarried;
        text_centered(draw, kPromptSize, cx, cy + 60.0f - kPromptSize, kDdWhite,
                      full ? "speedloaders full" : (me->bench_time > 0.0f ? "filling speedloaders..." : "hold E  fill speedloaders"));
    }
}

void Game::draw_debug_panels()
{
    if (toggles_.telemetry) {
        ImGui::SetNextWindowPos({ImGui::GetIO().DisplaySize.x - 290.0f, 16.0f}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Telemetry");
        plot("rpm", telem_rpm_, 0.0f, 7000.0f);
        plot("km/h", telem_speed_, 0.0f, 180.0f);
        plot("slip", telem_slip_, -1.0f, 1.0f);
        plot("load kN", telem_load_, 0.0f, 8.0f);
        ImGui::End();
    }
    if (toggles_.carsys) {
        ImGui::SetNextWindowPos({16.0f, ImGui::GetIO().DisplaySize.y - 260.0f}, ImGuiCond_FirstUseEver);
        ImGui::Begin("CarSys");
        ImGui::Text("engine %s", sim_.carsys().engine_on ? "running" : "off");
        ImGui::Text("fuel %.0f %%", static_cast<f64>(sim_.carsys().fluids.fuel * 100.0f));
        ImGui::Text("oil %.2f", static_cast<f64>(sim_.carsys().fluids.oil));
        ImGui::Text("coolant %.0f C", static_cast<f64>(sim_.carsys().fluids.coolant_temp));
        ImGui::Text("battery %.2f", static_cast<f64>(sim_.carsys().elec.battery_charge));
        ImGui::Text("weather %s %.2f", weather_mode_name(sim_.weather().mode()), static_cast<f64>(sim_.weather().rain()));
        ImGui::End();
    }
    if (toggles_.tuning) {
        ImGui::SetNextWindowPos({16.0f, 16.0f}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Tuning");
        VehicleConfig& cfg = sim_.vehicle().config();
        ImGui::SliderFloat("peak mu", &cfg.tire_peak_mu, 0.4f, 2.0f);
        ImGui::SliderFloat("slide mu", &cfg.tire_slide_mu, 0.2f, 1.6f);
        ImGui::SliderFloat("diff lock", &cfg.diff_lock, 0.0f, 1.0f);
        ImGui::SliderFloat("arb front", &cfg.arb_front, 0.0f, 40000.0f);
        ImGui::SliderFloat("arb rear", &cfg.arb_rear, 0.0f, 40000.0f);
        ImGui::Text("%.1f fps", static_cast<f64>(ImGui::GetIO().Framerate));
        ImGui::End();
    }
}

void Game::draw_viewfinder()
{
    if (terminal_focused()) {
        return;
    }
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    if (capture_flash_ > 0.0f) {
        draw->AddRectFilled({0.0f, 0.0f}, size, dd_rgba(235, 245, 235, static_cast<u8>(capture_flash_ * 210.0f)));
    }
    if (viewfinder_) {
        const f32 frame_h = size.y * 0.80f;
        const f32 frame_w = frame_h * 1.6f;
        const f32 x0 = (size.x - frame_w) * 0.5f;
        const f32 y0 = (size.y - frame_h) * 0.5f;
        const f32 x1 = x0 + frame_w;
        const f32 y1 = y0 + frame_h;
        const u32 shade = dd_rgba(8, 10, 8, 215);
        draw->AddRectFilled({0.0f, 0.0f}, {size.x, y0}, shade);
        draw->AddRectFilled({0.0f, y1}, size, shade);
        draw->AddRectFilled({0.0f, y0}, {x0, y1}, shade);
        draw->AddRectFilled({x1, y0}, {size.x, y1}, shade);
        const u32 line = dd_rgba(220, 230, 220, 200);
        const f32 b = 26.0f;
        draw->AddRect({x0, y0}, {x1, y1}, dd_rgba(160, 170, 160, 90));
        draw->AddRectFilled({x0, y0}, {x0 + b, y0 + 3.0f}, line);
        draw->AddRectFilled({x0, y0}, {x0 + 3.0f, y0 + b}, line);
        draw->AddRectFilled({x1 - b, y0}, {x1, y0 + 3.0f}, line);
        draw->AddRectFilled({x1 - 3.0f, y0}, {x1, y0 + b}, line);
        draw->AddRectFilled({x0, y1 - 3.0f}, {x0 + b, y1}, line);
        draw->AddRectFilled({x0, y1 - b}, {x0 + 3.0f, y1}, line);
        draw->AddRectFilled({x1 - b, y1 - 3.0f}, {x1, y1}, line);
        draw->AddRectFilled({x1 - 3.0f, y1 - b}, {x1, y1}, line);
        const f32 cx = size.x * 0.5f;
        const f32 cy = size.y * 0.5f;
        draw->AddRectFilled({cx - 14.0f, cy - 1.0f}, {cx + 14.0f, cy + 1.0f}, line);
        draw->AddRectFilled({cx - 1.0f, cy - 14.0f}, {cx + 1.0f, cy + 14.0f}, line);
        char text[32];
        std::snprintf(text, sizeof(text), "EXP %02u", sim_.disks().camera_exposures_left(&sim_.terminal().fs()));
        text_at(draw, kNoteSize, {x0 + 8.0f, y1 - 26.0f}, kDdWhite, text);
        text_at(draw, kNoteSize, {x1 - 170.0f, y1 - 26.0f}, kDdGray, "LMB SHUTTER");
    }
    if (capture_msg_until_ > 0.0f && sim_.player(local_).state() == PlayerState::OnFoot) {
        text_at(ImGui::GetForegroundDrawList(), 18.0f, {size.x * 0.5f - 80.0f, size.y * 0.80f - 9.0f}, kDdCyan,
                capture_msg_.c_str());
    }
}

}

namespace anom {

void Game::draw_creatures_panel()
{
    if (!toggles_.creatures) {
        return;
    }
    using ghost::game::GhostBehavior;
    ImGui::SetNextWindowPos({16.0f, 120.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({380.0f, 520.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Creatures", &toggles_.creatures)) {
        ImGui::End();
        return;
    }
    const auto& types = sim_.gameplay().ghostData().types;
    const bool client = net_.role() == NetSession::Role::Client;
    CreatureUi& ui = creature_ui_;
    if (client) {
        ImGui::TextDisabled("Only the host can spawn things (the world is theirs).");
    } else if (!types.empty()) {
        if (ImGui::TreeNodeEx("Inspect a pose", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("One ghost held in a pose in front of you. It does nothing and cannot be hit.");
            bool changed = false;
            ui.type = ui.type < 0 ? 0 : (ui.type >= static_cast<i32>(types.size()) ? static_cast<i32>(types.size()) - 1 : ui.type);
            if (ImGui::BeginCombo("Ghost", types[static_cast<size_t>(ui.type)].display.c_str())) {
                for (i32 t = 0; t < static_cast<i32>(types.size()); t++) {
                    if (ImGui::Selectable(types[static_cast<size_t>(t)].display.c_str(), t == ui.type) && t != ui.type) {
                        ui.type = t;
                        ui.pose = 0;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            const auto poses = ghost::game::posesOf(types[static_cast<size_t>(ui.type)].behavior);
            ui.pose = ui.pose < 0 ? 0 : (ui.pose >= static_cast<i32>(poses.size()) ? static_cast<i32>(poses.size()) - 1 : ui.pose);
            if (ImGui::BeginCombo("Pose", poses[static_cast<size_t>(ui.pose)].label)) {
                for (i32 p = 0; p < static_cast<i32>(poses.size()); p++) {
                    if (ImGui::Selectable(poses[static_cast<size_t>(p)].label, p == ui.pose)) {
                        ui.pose = p;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            if (types[static_cast<size_t>(ui.type)].behavior == GhostBehavior::Vasskraka) {
                changed = ImGui::SliderFloat("Size (health)", &ui.size, 0.3f, 1.5f, "%.2f") || changed;
            }
            const bool out = sim_.specimen().id != 0;
            if (ImGui::Button(out ? "Bring in front of me" : "Place in front of me")) {
                pending_.specimen_place = true;
                changed = true;
            }
            if (out) {
                ImGui::SameLine();
                if (ImGui::Button("Remove")) {
                    pending_.specimen_remove = true;
                }
            }
            if (changed) {
                pending_.specimen_type = ui.type;
                pending_.specimen_pose = ui.pose;
                pending_.specimen_size = ui.size;
            }
            ImGui::TreePop();
        }
        ImGui::SliderInt("How many", &ui.count, 1, 10);
        if (ImGui::TreeNodeEx("Spawn", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (size_t t = 0; t < types.size(); t++) {
                ImGui::PushID(static_cast<int>(t));
                if (ImGui::Button(types[t].display.c_str())) {
                    pending_.spawn_ghost = static_cast<i32>(t);
                    pending_.spawn_ghost_count = ui.count;
                }
                ImGui::PopID();
                if (t % 3 != 2 && t + 1 < types.size()) {
                    ImGui::SameLine();
                }
            }
            ImGui::TreePop();
        }
    }
    if (ImGui::CollapsingHeader("Ghosts", ImGuiTreeNodeFlags_DefaultOpen)) {
        const glm::vec3 me = to_glm(sim_.player(local_).pos());
        const auto& world = sim_.gameplay().ghosts();
        if (world.ghosts().empty()) {
            ImGui::TextDisabled("None out.");
        }
        for (const ghost::game::Ghost& g : world.ghosts()) {
            ImGui::Text("%s #%u  %s  hp %.0f  %s  %.1f m%s", world.def(g).display.c_str(), g.id,
                        ghost::game::ghostStateName(g.state), g.health, g.perceives ? "sees someone" : "unaware",
                        glm::distance(g.position, me), g.posed ? "  (posed)" : "");
        }
    }
    if (ImGui::CollapsingHeader("Players", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Cowboy hat and poncho: Auto gives it to an arena player %d kills ahead.",
                            sim_.player_rules().arenaHatLead);
        for (PlayerId id = 0; id < kMaxPlayers; id++) {
            const PlayerSlot* s = sim_.slot(id);
            if (!s || !s->active) {
                continue;
            }
            const ghost::game::RosterEntry* entry = sim_.roster().find(id);
            ImGui::PushID(static_cast<int>(id));
            ImGui::Text("%s  %d kills%s", s->name.c_str(), entry ? entry->kills : 0, sim_.wears_cowboy(id) ? "  [hat]" : "");
            if (!client) {
                const char* modes[3] = {"Auto", "Hat on", "Hat off"};
                for (u8 m = 0; m < 3; m++) {
                    ImGui::SameLine();
                    if (ImGui::RadioButton(modes[m], s->cowboy == m) && s->cowboy != m) {
                        pending_.cowboy_target = static_cast<i32>(id);
                        pending_.cowboy_mode = m;
                    }
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::End();
}

} // namespace anom
