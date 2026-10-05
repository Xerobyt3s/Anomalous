#include "engine/assets/asset_path.h"
#include "game/ammo/ammo_data.h"
#include "game/ammo/pouch.h"
#include "game/crafting/mortar.h"

#include <doctest/doctest.h>
#include <glm/gtc/constants.hpp>

#include <array>

using namespace ghost::game;

namespace {
const AmmoData& data() {
    static const AmmoData d = loadAmmoData(ghost::engine::assetPath("data"));
    return d;
}

MaterialId material(const char* name) {
    const auto id = data().findMaterial(name);
    REQUIRE(id);
    return *id;
}

void grindFully(Mortar& m) {
    m.grind(glm::two_pi<float>() * data().turnsPerDose * static_cast<float>(m.doses().size()));
}

}

TEST_CASE("The shipped data loads and has the elements the game expects") {
    CHECK(data().elements[kPlainElement].name == "plain");
    CHECK(data().elements.find("fire"));
    CHECK(data().elements.find("wind"));
    CHECK(data().elements.find("inferno"));
    CHECK(data().elements[data().element("wind")].velocityScale > 1.0f);
}

TEST_CASE("Fire and wind react into inferno in either order; other pairs don't react") {
    const ReactionTable& r = data().reactions;
    const ElementId fire = data().element("fire");
    const ElementId wind = data().element("wind");
    const ElementId inferno = data().element("inferno");

    CHECK(r.react(fire, wind) == inferno);
    CHECK(r.react(wind, fire) == inferno);
    CHECK_FALSE(r.react(fire, fire));
    CHECK_FALSE(r.react(kPlainElement, fire));

    const std::array<ElementId, 2> mix{wind, fire};
    CHECK(r.combine(mix) == inferno);
    const std::array<ElementId, 1> single{fire};
    CHECK(r.combine(single) == fire);
    CHECK(r.combine({}) == kPlainElement);
}

TEST_CASE("Malformed data is rejected") {
    CHECK_THROWS(parseAmmoData(R"({"elements":[{"name":"fire"}]})", R"({"reactions":[]})", R"({"materials":[]})"));
    CHECK_THROWS(parseAmmoData(R"({"elements":[{"name":"plain"}]})",
                               R"({"reactions":[{"a":"plain","b":"nope","result":"plain"}]})",
                               R"({"materials":[]})"));
}

TEST_CASE("A mortar round needs propellant and full grinding") {
    Mortar m(data());
    CHECK(m.addDose(material("ember_ash")) == Mortar::AddResult::Added);
    grindFully(m);
    CHECK_FALSE(m.canPour());

    CHECK(m.addDose(material("gunpowder")) == Mortar::AddResult::Added);
    CHECK(m.groundFraction() < 1.0f);
    CHECK_FALSE(m.pour());
    grindFully(m);
    REQUIRE(m.canPour());

    const auto round = m.pour();
    REQUIRE(round);
    CHECK(round->element == data().element("fire"));
    CHECK(m.empty());
}

TEST_CASE("Gunpowder alone makes a plain round; fire + wind makes inferno") {
    Mortar plain(data());
    plain.addDose(material("gunpowder"));
    grindFully(plain);
    CHECK(plain.pour()->element == kPlainElement);

    Mortar inferno(data());
    inferno.addDose(material("gunpowder"));
    inferno.addDose(material("ember_ash"));
    inferno.addDose(material("gale_wisp"));
    grindFully(inferno);
    CHECK(inferno.pour()->element == data().element("inferno"));
}

TEST_CASE("The mortar holds a limited number of doses and only one propellant") {
    Mortar m(data());
    CHECK(m.addDose(material("gunpowder")) == Mortar::AddResult::Added);
    CHECK(m.addDose(material("gunpowder")) == Mortar::AddResult::SecondPropellant);
    CHECK(m.addDose(material("ember_ash")) == Mortar::AddResult::Added);
    CHECK(m.addDose(material("gale_wisp")) == Mortar::AddResult::Added);
    CHECK(m.addDose(material("gunpowder")) == Mortar::AddResult::Full);
}

TEST_CASE("Counts track rounds per element") {
    AmmoPouch pouch(data().elements.size());
    pouch.add(data().element("fire"), 2);
    CHECK(pouch.take(data().element("fire")));
    CHECK(pouch.count(data().element("fire")) == 1);
    CHECK_FALSE(pouch.take(data().element("wind")));
    CHECK(pouch.total() == 1);
}

TEST_CASE("Lightning and wind make a thunderstrike, in either order") {
    const ReactionTable& r = data().reactions;
    const ElementId lightning = data().element("lightning");
    const ElementId wind = data().element("wind");
    CHECK(r.react(lightning, wind) == data().element("thunderstrike"));
    CHECK(r.react(wind, lightning) == data().element("thunderstrike"));
    CHECK(data().elements[lightning].hitscanRange > 0.0f);
    CHECK(data().elements[data().element("thunderstrike")].strikeDelay > 0.0f);

    Mortar m(data());
    m.addDose(material("gunpowder"));
    m.addDose(material("storm_glass"));
    m.addDose(material("gale_wisp"));
    grindFully(m);
    CHECK(m.pour()->element == data().element("thunderstrike"));
}

TEST_CASE("A material that doesn't combine fizzles and the mix stays the first material") {
    Mortar m(data());
    CHECK(m.addDose(material("gunpowder")) == Mortar::AddResult::Added);
    CHECK(m.addDose(material("storm_glass")) == Mortar::AddResult::Added);
    CHECK(m.addDose(material("ember_ash")) == Mortar::AddResult::Fizzled);
    CHECK(m.doses().size() == 2);
    CHECK(m.resultElement() == data().element("lightning"));

    CHECK(m.addDose(material("storm_glass")) == Mortar::AddResult::Fizzled);

    CHECK(m.addDose(material("gale_wisp")) == Mortar::AddResult::Added);
    CHECK(m.resultElement() == data().element("thunderstrike"));
}

TEST_CASE("Haze and voodoo load; pairs without a reaction still fizzle") {
    const ElementDef& haze = data().elements[data().element("haze")];
    CHECK(haze.ethereal);
    CHECK(haze.stealthy);
    CHECK(haze.fadeSpeed > 0.0f);
    CHECK(data().elements[data().element("voodoo")].self == SelfEffect::Hit);

    Mortar m(data());
    m.addDose(material("gunpowder"));
    CHECK(m.addDose(material("ectoplasm")) == Mortar::AddResult::Added);
    CHECK(m.addDose(material("storm_glass")) == Mortar::AddResult::Fizzled);
    CHECK(m.resultElement() == data().element("haze"));
}

TEST_CASE("Voodoo reacts with wind, haze and lightning into self-cast rounds") {
    const ReactionTable& r = data().reactions;
    const ElementId voodoo = data().element("voodoo");
    const struct {
        const char* partner;
        const char* material;
        const char* result;
        SelfEffect effect;
    } cases[] = {{"wind", "gale_wisp", "tailwind", SelfEffect::Haste},
                 {"haze", "ectoplasm", "shroud", SelfEffect::Shroud},
                 {"lightning", "storm_glass", "blink", SelfEffect::Blink}};
    for (const auto& c : cases) {
        const ElementId partner = data().element(c.partner);
        const ElementId result = data().element(c.result);
        CHECK(r.react(partner, voodoo) == result);
        CHECK(r.react(voodoo, partner) == result);
        CHECK(data().elements[result].self == c.effect);

        Mortar m(data());
        m.addDose(material("gunpowder"));
        m.addDose(material("effigy_thread"));
        CHECK(m.addDose(material(c.material)) == Mortar::AddResult::Added);
        grindFully(m);
        CHECK(m.pour()->element == result);
    }
    CHECK(data().elements[data().element("tailwind")].selfSpeedScale == doctest::Approx(1.5f));
    CHECK(data().elements[data().element("blink")].selfDistance == doctest::Approx(9.75f));
}

TEST_CASE("Haze and wind make fog") {
    const ElementId fog = data().element("fog");
    CHECK(data().reactions.react(data().element("haze"), data().element("wind")) == fog);
    CHECK(data().reactions.react(data().element("wind"), data().element("haze")) == fog);
    CHECK(data().elements[fog].fog.height > 0.0f);
    CHECK(data().elements[fog].volumeRadius == doctest::Approx(6.0f));
    CHECK_FALSE(data().elements[fog].ethereal);

    Mortar m(data());
    m.addDose(material("gunpowder"));
    m.addDose(material("ectoplasm"));
    CHECK(m.addDose(material("gale_wisp")) == Mortar::AddResult::Added);
    grindFully(m);
    CHECK(m.pour()->element == fog);
}

TEST_CASE("Fire and haze make ghostfire, a breath instead of a bullet") {
    const ElementId ghostfire = data().element("ghostfire");
    CHECK(data().reactions.react(data().element("fire"), data().element("haze")) == ghostfire);
    CHECK(data().reactions.react(data().element("haze"), data().element("fire")) == ghostfire);
    CHECK(data().elements[ghostfire].breathRange == doctest::Approx(5.0f));
    CHECK(data().elements[ghostfire].breathHalfAngle == doctest::Approx(0.279f).epsilon(0.01));

    Mortar m(data());
    m.addDose(material("gunpowder"));
    m.addDose(material("ember_ash"));
    CHECK(m.addDose(material("ectoplasm")) == Mortar::AddResult::Added);
    grindFully(m);
    CHECK(m.pour()->element == ghostfire);
}

TEST_CASE("Fire and voodoo make firelight, a pulse that reveals") {
    const ElementId firelight = data().element("firelight");
    CHECK(data().reactions.react(data().element("fire"), data().element("voodoo")) == firelight);
    CHECK(data().reactions.react(data().element("voodoo"), data().element("fire")) == firelight);
    const ElementDef& def = data().elements[firelight];
    CHECK(def.self == SelfEffect::Reveal);
    CHECK(def.selfDistance == doctest::Approx(30.0f));
    CHECK(def.selfSpeed == doctest::Approx(18.0f));
    CHECK(def.selfDuration == doctest::Approx(2.0f));

    Mortar m(data());
    m.addDose(material("gunpowder"));
    m.addDose(material("effigy_thread"));
    CHECK(m.addDose(material("ember_ash")) == Mortar::AddResult::Added);
    grindFully(m);
    CHECK(m.pour()->element == firelight);
}

TEST_CASE("Fire and fog make steam, an explosion rather than a round") {
    const ElementId steam = data().element("steam");
    CHECK(data().reactions.react(data().element("fire"), data().element("fog")) == steam);
    CHECK(data().reactions.react(data().element("fog"), data().element("fire")) == steam);
    const ElementDef& def = data().elements[steam];
    CHECK(def.explosionRadiusScale > 0.0f);
    CHECK(def.explosionImpulse > 0.0f);
    CHECK(def.explosionGhostDamage > 0.0f);
    CHECK(def.explosionPlayerDamage > 0.0f);
    CHECK(def.damage == DamageKind::Fire);
    CHECK_FALSE(data().reactions.react(data().element("inferno"), data().element("fog")));
}

TEST_CASE("A plain round leaves a ribbon trace") {
    CHECK(data().elements[kPlainElement].trail == TrailKind::Ribbon);
}

TEST_CASE("Each ghost material drops with a look of its own; gunpowder, never dropped, just glows") {
    const std::pair<const char*, MaterialDef::Look> expected[] = {
        {"storm_glass", MaterialDef::Look::StormOrb},  {"ectoplasm", MaterialDef::Look::HazeBubble},
        {"gale_wisp", MaterialDef::Look::WindCloud},   {"ember_ash", MaterialDef::Look::SmallFire},
        {"effigy_thread", MaterialDef::Look::ThreadKnot}, {"gunpowder", MaterialDef::Look::Glow},
    };
    for (const auto& [name, look] : expected) {
        const MaterialDef* found = nullptr;
        for (const MaterialDef& m : data().materials) {
            if (m.name == name) {
                found = &m;
            }
        }
        REQUIRE(found != nullptr);
        CHECK(found->look == look);
    }
}
