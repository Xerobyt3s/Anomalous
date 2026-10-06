#include "test.h"

#include "core/arena.h"
#include "game/crafting/mortar.h"
#include "math/glm_bridge.h"
#include "sim/sim.h"
#include "terminal/programs/synth.h"
#include "terminal/screen.h"

#include <memory>
#include <string>

using namespace anom;

namespace {

constexpr const char* kZoneDir = "assets/zones/testzone";

struct Bay {
    Arena perm{megabytes(256)};
    Arena scratch{megabytes(256)};
    std::unique_ptr<Sim> sim = std::make_unique<Sim>();

    bool setup()
    {
        if (!sim->init(perm, scratch, kZoneDir)) {
            return false;
        }
        sim->carsys().parts[PART_TANK].installed = true;
        sim->carsys().parts[PART_PRINTER].installed = true;
        sim->carsys().elec.battery_charge = 0.9f;
        return true;
    }

    const ghost::game::AmmoData& ammo() const { return sim->gameplay().ammo(); }

    u8 material(std::string_view name) const { return static_cast<u8>(*ammo().findMaterial(name)); }

    ghost::game::ElementId element(std::string_view name) const { return ammo().element(name); }

    void run(u32 ticks)
    {
        for (u32 i = 0; i < ticks; i++) {
            PlayerCommand c;
            c.gameplay = true;
            sim->tick(c, kFixedDt);
            sim->events().clear();
        }
    }

    void print(std::initializer_list<u8> doses, u32 count)
    {
        TermRequest req;
        u32 n = 0;
        for (u8 d : doses) {
            req.synth_doses[n++] = d;
        }
        sim->queue_print(req.synth_doses, n, count);
    }
};

std::pair<u8, u8> unstable_pair(const ghost::game::AmmoData& ammo)
{
    for (u8 a = 0; a < ammo.materials.size(); a++) {
        for (u8 b = 0; b < ammo.materials.size(); b++) {
            ghost::game::Mortar m(ammo);
            if (m.addDose(a) == ghost::game::Mortar::AddResult::Added
                && m.addDose(b) == ghost::game::Mortar::AddResult::Fizzled) {
                return {a, b};
            }
        }
    }
    return {static_cast<u8>(255), static_cast<u8>(255)};
}

}

TEST(synth, the_preview_follows_the_mortar_rules)
{
    Bay bay;
    if (!bay.setup()) {
        FAIL("sim init");
        return;
    }
    u16 tank[kSynthMaterials] = {};
    const u8 powder = bay.material("gunpowder");
    const u8 ember = bay.material("ember_ash");
    tank[powder] = 10;
    tank[ember] = 4;
    const u8 fire[2] = {powder, ember};
    const SynthPreview ready = synth_preview(bay.ammo(), tank, fire, 2);
    CHECK(ready.verdict == SynthVerdict::Ready);
    CHECK(ready.element == static_cast<i32>(bay.element("fire")));
    CHECK(ready.affordable == 4u);
    const u8 lone[1] = {ember};
    CHECK(synth_preview(bay.ammo(), tank, lone, 1).verdict == SynthVerdict::NoPropellant);
    const u8 twice[2] = {powder, powder};
    CHECK(synth_preview(bay.ammo(), tank, twice, 2).verdict == SynthVerdict::TwoPropellants);
    const auto [a, b] = unstable_pair(bay.ammo());
    CHECK(a != 255);
    if (a != 255) {
        const u8 bad[3] = {powder, a, b};
        CHECK(synth_preview(bay.ammo(), tank, bad, 3).verdict == SynthVerdict::Unstable);
    }
}

TEST(synth, carried_materials_go_into_the_tank_when_poured_at_the_car)
{
    Bay bay;
    if (!bay.setup()) {
        FAIL("sim init");
        return;
    }
    const ghost::game::MaterialId ember = bay.material("ember_ash");
    bay.sim->gameplay().materials().add(ember, 5);
    const RigidBody* car = bay.sim->phys().body(bay.sim->vehicle().body());
    bay.sim->player(0).init(car->pos + Vec3{-2.4f, -0.4f, 0.0f}, 0.0f);
    bay.run(5);
    CHECK(bay.sim->carsys().synth.tank[ember] == 0);
    CHECK(bay.sim->gameplay().materials().count(ember) == 5);
    bay.sim->fill_tank(0);
    CHECK(bay.sim->carsys().synth.tank[ember] == 5);
    CHECK(bay.sim->gameplay().materials().count(ember) == 0);
}

TEST(synth, a_bulk_order_takes_its_material_up_front_and_prints_over_time_on_battery)
{
    Bay bay;
    if (!bay.setup()) {
        FAIL("sim init");
        return;
    }
    SynthBay& s = bay.sim->carsys().synth;
    const u8 powder = bay.material("gunpowder");
    const u8 ember = bay.material("ember_ash");
    s.tank[powder] = 10;
    s.tank[ember] = 6;
    bay.print({powder, ember}, 8);
    CHECK(s.result == SynthResult::Queued);
    CHECK(s.job_left == 6);
    CHECK(s.tank[powder] == 4);
    CHECK(s.tank[ember] == 0);
    const f32 battery = bay.sim->carsys().elec.battery_charge;
    const f32 per = bay.sim->vehicle().config().synth.print_time;
    bay.run(static_cast<u32>(per * 120.0f * 2.5f));
    CHECK(s.tray[bay.element("fire")] == 2);
    CHECK(s.job_left == 4);
    bay.run(static_cast<u32>(per * 120.0f * 4.5f));
    CHECK(s.tray[bay.element("fire")] == 6);
    CHECK(s.job_left == 0);
    CHECK(bay.sim->carsys().elec.battery_charge < battery);
}

TEST(synth, an_unstable_mix_wastes_one_set_and_prints_nothing)
{
    Bay bay;
    if (!bay.setup()) {
        FAIL("sim init");
        return;
    }
    SynthBay& s = bay.sim->carsys().synth;
    const auto [a, b] = unstable_pair(bay.ammo());
    if (a == 255) {
        FAIL("no unstable pair in the data");
        return;
    }
    const u8 powder = bay.material("gunpowder");
    s.tank[powder] = 5;
    s.tank[a] = static_cast<u16>(s.tank[a] + 5);
    s.tank[b] = static_cast<u16>(s.tank[b] + 5);
    const u32 before = s.tank_total();
    bay.print({powder, a, b}, 5);
    CHECK(s.result == SynthResult::Unstable);
    CHECK(s.job_left == 0);
    CHECK(s.tank_total() == before - 3);
}

TEST(synth, the_printer_stalls_without_power_or_tray_room)
{
    Bay bay;
    if (!bay.setup()) {
        FAIL("sim init");
        return;
    }
    SynthBay& s = bay.sim->carsys().synth;
    s.tank[bay.material("gunpowder")] = 4;
    bay.print({bay.material("gunpowder")}, 4);
    CHECK(s.job_left == 4);
    bay.sim->carsys().elec.battery_charge = 0.05f;
    bay.run(600);
    CHECK(s.stalled);
    CHECK(s.tray_total() == 0u);
    bay.sim->carsys().elec.battery_charge = 0.9f;
    s.tray[0] = static_cast<u16>(bay.sim->vehicle().config().synth.tray_max);
    bay.run(600);
    CHECK(s.stalled);
    CHECK(s.job_left == 4);
}

TEST(synth, taking_the_tray_puts_the_rounds_in_your_own_pouch)
{
    Bay bay;
    if (!bay.setup()) {
        FAIL("sim init");
        return;
    }
    SynthBay& s = bay.sim->carsys().synth;
    const ghost::game::ElementId fire = bay.element("fire");
    s.tray[fire] = 7;
    PlayerSlot& me = *bay.sim->slot(0);
    const int before = me.gun.pouch.count(fire);
    bay.sim->take_tray(me, 0);
    CHECK(me.gun.pouch.count(fire) == before + 7);
    CHECK(s.tray_total() == 0u);
}

TEST(synth, the_program_composes_a_recipe_and_asks_for_a_bulk_print)
{
    Bay bay;
    if (!bay.setup()) {
        FAIL("sim init");
        return;
    }
    auto screen = std::make_unique<Screen>();
    TermScene scene;
    TermRequest request;
    ProgramContext ctx;
    ctx.screen = screen.get();
    ctx.scene = &scene;
    ctx.request = &request;
    TermView view;
    view.sys = &bay.sim->carsys();
    view.synth_bay = &bay.sim->carsys().synth;
    view.synth_tank = true;
    view.synth_printer = true;
    view.ammo = &bay.ammo();
    bay.sim->carsys().synth.tank[bay.material("gunpowder")] = 9;
    bay.sim->carsys().synth.tank[bay.material("ember_ash")] = 9;
    SynthProgram program;
    program.enter(ctx, view);
    program.key_char(ctx, view, static_cast<char>('1' + bay.material("gunpowder")));
    program.key_char(ctx, view, static_cast<char>('1' + bay.material("ember_ash")));
    for (int i = 0; i < 6; i++) {
        program.key(ctx, view, TermKey::Right);
    }
    program.update(ctx, view, 0.016f);
    std::string text;
    for (u32 row = 0; row < kTermRows; row++) {
        for (u32 col = 0; col < kTermCols; col++) {
            const u16 g = screen->glyph(row, col);
            text.push_back(g >= 32 && g < 127 ? static_cast<char>(g) : ' ');
        }
        text.push_back('\n');
    }
    CHECK(text.find("FIRE ROUND") != std::string::npos);
    CHECK(!program.key(ctx, view, TermKey::Enter));
    CHECK(request.synth_print);
    CHECK(request.synth_dose_count == 2u);
    CHECK(request.synth_count == 12u);
    bay.sim->queue_print(request.synth_doses, request.synth_dose_count, request.synth_count);
    CHECK(bay.sim->carsys().synth.job_left == 9);
}

namespace {

Vec3 socket_world(const Sim& sim, PartKind kind)
{
    const RigidBody* car = sim.phys().body(sim.vehicle().body());
    return car->pos + rotate(car->rot, part_def(kind).socket_pos - sim.vehicle().config().com_offset);
}

void look_at_part(Bay& bay, PartKind kind)
{
    const RigidBody* car = bay.sim->phys().body(bay.sim->vehicle().body());
    const Vec3 target = socket_world(*bay.sim, kind);
    const Vec3 side = rotate(car->rot, Vec3{1.0f, 0.0f, 0.0f});
    bay.sim->player(0).init(Vec3{target.x, car->pos.y - 0.6f, target.z} + side * 1.4f, 0.0f);
    for (u32 i = 0; i < 4; i++) {
        PlayerCommand c;
        c.gameplay = true;
        const Player& p = bay.sim->player(0);
        c.view_origin = p.pos() + p.up() * kPlayerEyeHeight;
        c.view_dir = normalize(target - c.view_origin);
        bay.sim->tick(c, kFixedDt);
        bay.sim->events().clear();
    }
}

}

TEST(synth, the_tank_mounts_only_on_the_printer_and_the_printer_stays_while_it_does)
{
    Bay bay;
    if (!bay.setup()) {
        FAIL("sim init");
        return;
    }
    CarSys& sys = bay.sim->carsys();
    sys.parts[PART_TANK].installed = false;
    sys.parts[PART_PRINTER].installed = false;
    bay.sim->interact(0).hands() = Item{ITEM_TANK};
    look_at_part(bay, PART_TANK);
    CHECK(bay.sim->interact(0).action() != InteractAction::InstallPart);

    sys.parts[PART_PRINTER].installed = true;
    look_at_part(bay, PART_TANK);
    CHECK(bay.sim->interact(0).action() == InteractAction::InstallPart);

    sys.parts[PART_TANK].installed = true;
    bay.sim->interact(0).hands() = Item{};
    look_at_part(bay, PART_PRINTER);
    CHECK(bay.sim->interact(0).action() == InteractAction::Info);
    CHECK(bay.sim->interact(0).prompt().find("tank off") != std::string_view::npos);

    sys.parts[PART_TANK].installed = false;
    look_at_part(bay, PART_PRINTER);
    CHECK(bay.sim->interact(0).action() == InteractAction::RemovePart);
}

TEST(synth, the_bus_cable_can_plug_straight_into_the_printer)
{
    Bay bay;
    if (!bay.setup()) {
        FAIL("sim init");
        return;
    }
    CarSys& sys = bay.sim->carsys();
    sys.parts[PART_COMPUTER].installed = true;
    sys.cables[CABLE_BUS].state = CableState::Plugged;
    sys.bus_target = kBusTargetPrinter;
    bay.run(60);
    const RigidBody* car = bay.sim->phys().body(bay.sim->vehicle().body());
    const Vec3 jack = car->pos + rotate(car->rot, kPrinterJackLocal - bay.sim->vehicle().config().com_offset);
    const Cable& bus = sys.cables[CABLE_BUS];
    CHECK(bus.state == CableState::Plugged);
    CHECK(length(bus.p[kCablePoints - 1] - jack) < 0.1f);

    sys.parts[PART_TANK].installed = false;
    sys.parts[PART_PRINTER].installed = false;
    bay.run(2);
    CHECK(sys.cables[CABLE_BUS].state == CableState::Stowed);
    CHECK(sys.bus_target == kBusTargetCar);
}

namespace {

struct Hands {
    Bay& bay;

    void face(Vec3 target, bool down, bool pressed)
    {
        PlayerCommand c;
        c.gameplay = true;
        const Player& p = bay.sim->player(0);
        c.view_origin = p.pos() + p.up() * kPlayerEyeHeight;
        c.view_dir = normalize(target - c.view_origin);
        c.use_down = down;
        c.use_pressed = pressed;
        bay.sim->tick(c, kFixedDt);
        bay.sim->events().clear();
    }
    void look(Vec3 target)
    {
        for (u32 i = 0; i < 4; i++) {
            face(target, false, false);
        }
    }
    void tap(Vec3 target)
    {
        face(target, true, true);
        face(target, true, false);
        face(target, false, false);
    }
    void hold(Vec3 target)
    {
        face(target, true, true);
        for (u32 i = 0; i < static_cast<u32>((kInteractHoldTime + 0.2f) / kFixedDt); i++) {
            face(target, true, false);
        }
        face(target, false, false);
    }
};

void drop_beside_car(Bay& bay, Item item)
{
    const RigidBody* car = bay.sim->phys().body(bay.sim->vehicle().body());
    const Vec3 side = rotate(car->rot, Vec3{-1.0f, 0.0f, 0.0f});
    Vec3 at = car->pos + side * 4.0f;
    at.y = bay.sim->terrain().heightfield().sample(at.x, at.z) + 0.3f;
    interact_spawn_pickup(bay.sim->world(), bay.sim->phys(), item, at, 0.0f, Vec3{});
}

const Entity* pickup_of(Bay& bay, ItemKind kind)
{
    const Entity* found = nullptr;
    for (u32 idx : bay.sim->world().entities().live_indices()) {
        const Entity* e = bay.sim->world().entities().at(idx);
        if (e && e->kind == EntityKind::PartPickup && static_cast<ItemKind>(e->aux_kind) == kind) {
            found = e;
        }
    }
    return found;
}

void hold_at_part(Bay& bay, PartKind kind)
{
    const u32 ticks = static_cast<u32>((kInteractHoldTime + 0.2f) / kFixedDt);
    for (u32 i = 0; i <= ticks + 1; i++) {
        const RigidBody* car = bay.sim->phys().body(bay.sim->vehicle().body());
        const Vec3 target = socket_world(*bay.sim, kind);
        const Vec3 side = rotate(car->rot, Vec3{1.0f, 0.0f, 0.0f});
        bay.sim->player(0).init(Vec3{target.x, car->pos.y - 0.6f, target.z} + side * 1.4f, 0.0f);
        Hands{bay}.face(target, i <= ticks, i == 0);
    }
}

void stand_by(Bay& bay, Vec3 target)
{
    const RigidBody* car = bay.sim->phys().body(bay.sim->vehicle().body());
    const Vec3 side = rotate(car->rot, Vec3{-1.0f, 0.0f, 0.0f});
    Vec3 at = target + side * 1.3f;
    at.y = bay.sim->terrain().heightfield().sample(at.x, at.z) + 0.05f;
    bay.sim->player(0).init(at, 0.0f);
}

}

TEST(synth, a_tank_keeps_its_materials_when_taken_off_the_car_and_put_back)
{
    Bay bay;
    if (!bay.setup()) {
        FAIL("sim init");
        return;
    }
    CarSys& sys = bay.sim->carsys();
    const u8 ember = bay.material("ember_ash");
    sys.synth.tank[ember] = 7;
    Hands hands{bay};
    look_at_part(bay, PART_TANK);
    CHECK(bay.sim->interact(0).action() == InteractAction::RemovePart);
    hold_at_part(bay, PART_TANK);
    const Item held = bay.sim->interact(0).hands();
    CHECK(held.kind == ITEM_TANK);
    CHECK(!sys.parts[PART_TANK].installed);
    CHECK(sys.synth.tank_total() == 0u);
    const LooseTank* loose = sys.loose_tank(held.aux);
    CHECK(loose && loose->doses[ember] == 7);

    look_at_part(bay, PART_TANK);
    CHECK(bay.sim->interact(0).action() == InteractAction::InstallPart);
    hold_at_part(bay, PART_TANK);
    CHECK(sys.parts[PART_TANK].installed);
    CHECK(sys.synth.tank[ember] == 7);
    CHECK(!sys.loose_tank(held.aux));
}

TEST(synth, looking_at_a_tank_offers_to_pour_and_a_tap_pours)
{
    Bay bay;
    if (!bay.setup()) {
        FAIL("sim init");
        return;
    }
    CarSys& sys = bay.sim->carsys();
    const u8 ember = bay.material("ember_ash");
    bay.sim->gameplay().materials().add(ember, 4);
    Hands hands{bay};

    look_at_part(bay, PART_TANK);
    CHECK(bay.sim->interact(0).prompt().find("pour 4") != std::string_view::npos);
    hands.tap(socket_world(*bay.sim, PART_TANK));
    CHECK(sys.synth.tank[ember] == 4);
    CHECK(sys.parts[PART_TANK].installed);

    bay.sim->gameplay().materials().add(ember, 3);
    drop_beside_car(bay, Item{ITEM_TANK});
    bay.run(60);
    const Entity* tank = pickup_of(bay, ITEM_TANK);
    CHECK(tank != nullptr);
    if (!tank) {
        return;
    }
    const Vec3 aim = tank->pos + Vec3{0.0f, 0.15f, 0.0f};
    stand_by(bay, tank->pos);
    hands.look(aim);
    CHECK(bay.sim->interact(0).prompt().find("pour 3") != std::string_view::npos);
    hands.tap(aim);
    tank = pickup_of(bay, ITEM_TANK);
    const LooseTank* store = tank ? sys.loose_tank(static_cast<i32>(tank->aux_data)) : nullptr;
    CHECK(store && store->doses[ember] == 3);
    CHECK(bay.sim->gameplay().materials().count(ember) == 0);
}

TEST(synth, a_tank_on_a_loose_printer_is_filled_and_prints_over_the_cable)
{
    Bay bay;
    if (!bay.setup()) {
        FAIL("sim init");
        return;
    }
    CarSys& sys = bay.sim->carsys();
    sys.parts[PART_TANK].installed = false;
    sys.parts[PART_PRINTER].installed = false;
    sys.parts[PART_COMPUTER].installed = true;
    drop_beside_car(bay, Item{ITEM_PRINTER});
    bay.run(60);
    const Entity* printer = pickup_of(bay, ITEM_PRINTER);
    CHECK(printer != nullptr);
    if (!printer) {
        return;
    }
    const Vec3 aim = printer->pos + Vec3{0.0f, 0.1f, 0.0f};
    stand_by(bay, printer->pos);
    bay.sim->interact(0).hands() = Item{ITEM_TANK};
    Hands hands{bay};
    hands.look(aim);
    CHECK(bay.sim->interact(0).action() == InteractAction::PlaceTankOnPrinter);
    hands.tap(aim);
    CHECK(bay.sim->interact(0).hands().kind == ITEM_NONE);
    printer = pickup_of(bay, ITEM_PRINTER);
    const i32 id = printer ? static_cast<i32>(printer->aux_data) : 0;
    const LoosePrinter* loose = sys.loose_printer(id);
    CHECK(loose && loose->has_tank);
    if (!loose) {
        return;
    }

    const u8 powder = bay.material("gunpowder");
    const u8 ember = bay.material("ember_ash");
    bay.sim->gameplay().materials().add(powder, 4);
    bay.sim->gameplay().materials().add(ember, 4);
    hands.look(aim);
    CHECK(bay.sim->interact(0).action() == InteractAction::TakeTankOffPrinter);
    hands.tap(aim);
    CHECK(loose->bay.tank[powder] == 4);
    CHECK(loose->bay.tank[ember] == 4);

    sys.cables[CABLE_BUS].state = CableState::Plugged;
    sys.bus_target = kBusTargetLoosePrinter;
    sys.bus_printer = id;
    bay.run(30);
    CHECK(sys.cables[CABLE_BUS].state == CableState::Plugged);
    printer = pickup_of(bay, ITEM_PRINTER);
    CHECK(printer && length(sys.cables[CABLE_BUS].p[kCablePoints - 1]
                            - (printer->pos + rotate(printer->rot, kLoosePrinterJackLocal))) < 0.1f);
    bay.print({powder, ember}, 2);
    CHECK(loose->bay.job_left == 2);
    CHECK(sys.synth.job_left == 0);
    bay.run(static_cast<u32>(8.0f / kFixedDt));
    CHECK(loose->bay.tray_total() == 2u);

    hands.look(aim);
    CHECK(bay.sim->interact(0).action() == InteractAction::TakeRounds);
    const i32 before = bay.sim->slot(0)->gun.pouch.count(bay.element("fire"));
    hands.tap(aim);
    CHECK(bay.sim->slot(0)->gun.pouch.count(bay.element("fire")) == before + 2);

    hands.look(aim);
    hands.hold(aim);
    CHECK(bay.sim->interact(0).hands().kind == ITEM_TANK);
    CHECK(!loose->has_tank);
}

TEST(synth, a_printer_carrying_a_tank_installs_both_into_the_car)
{
    Bay bay;
    if (!bay.setup()) {
        FAIL("sim init");
        return;
    }
    CarSys& sys = bay.sim->carsys();
    sys.parts[PART_TANK].installed = false;
    sys.parts[PART_PRINTER].installed = false;
    const i32 id = sys.claim_printer();
    LoosePrinter* printer = sys.loose_printer(id);
    CHECK(printer != nullptr);
    if (!printer) {
        return;
    }
    printer->has_tank = true;
    printer->bay.tank[bay.material("ember_ash")] = 6;
    printer->bay.tray[0] = 3;
    bay.sim->interact(0).hands() = Item{ITEM_PRINTER, 1.0f, id};
    look_at_part(bay, PART_PRINTER);
    CHECK(bay.sim->interact(0).action() == InteractAction::InstallPart);
    hold_at_part(bay, PART_PRINTER);
    CHECK(sys.parts[PART_PRINTER].installed);
    CHECK(sys.parts[PART_TANK].installed);
    CHECK(sys.synth.tank[bay.material("ember_ash")] == 6);
    CHECK(sys.synth.tray[0] == 3);
    CHECK(!sys.loose_printer(id));
}

TEST(synth, a_tank_found_in_the_zone_starts_stocked)
{
    Bay bay;
    if (!bay.setup()) {
        FAIL("sim init");
        return;
    }
    const Entity* tank = pickup_of(bay, ITEM_TANK);
    CHECK(tank != nullptr);
    if (!tank) {
        return;
    }
    const LooseTank* store = bay.sim->carsys().loose_tank(static_cast<i32>(tank->aux_data));
    CHECK(store != nullptr);
    if (!store) {
        return;
    }
    for (u8 m = 0; m < bay.ammo().materials.size(); m++) {
        CHECK(store->doses[m] == (bay.ammo().materials[m].propellant ? 40 : 10));
    }
    CHECK(store->doses[bay.material("gunpowder")] == 40);
    CHECK(store->total() <= bay.sim->vehicle().config().synth.tank_capacity);
}
