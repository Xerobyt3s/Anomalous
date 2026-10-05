#include "game/ammo/ammo_data.h"

#include "engine/assets/asset_path.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ghost::game {
namespace {
using nlohmann::json;

glm::vec3 vec3Or(const json& j, const char* key, glm::vec3 fallback) {
    if (!j.contains(key)) {
        return fallback;
    }
    const auto& a = j.at(key);
    return {a.at(0).get<float>(), a.at(1).get<float>(), a.at(2).get<float>()};
}

TrailKind parseTrail(const std::string& name) {
    if (name == "embers") return TrailKind::Embers;
    if (name == "whirl") return TrailKind::Whirl;
    if (name == "flaming_whirl") return TrailKind::FlamingWhirl;
    if (name == "haze") return TrailKind::Haze;
    if (name == "ribbon") return TrailKind::Ribbon;
    if (name == "none") return TrailKind::None;
    throw std::runtime_error("Unknown trail kind: " + name);
}

std::string readFile(const std::filesystem::path& path) {
    if (auto text = engine::readAsset(path)) {
        return std::move(*text);
    }
    throw std::runtime_error("Missing data file: " + path.string());
}

}

std::optional<MaterialId> AmmoData::findMaterial(std::string_view name) const {
    for (std::size_t i = 0; i < materials.size(); ++i) {
        if (materials[i].name == name) {
            return static_cast<MaterialId>(i);
        }
    }
    return std::nullopt;
}

ElementId AmmoData::element(std::string_view name) const {
    if (const auto id = elements.find(name)) {
        return *id;
    }
    throw std::runtime_error("Unknown element: " + std::string(name));
}

AmmoData parseAmmoData(std::string_view elementsJson, std::string_view reactionsJson, std::string_view materialsJson) {
    AmmoData data;

    const json elementsDoc = json::parse(elementsJson);
    for (const json& e : elementsDoc.at("elements")) {
        ElementDef def;
        def.name = e.at("name").get<std::string>();
        def.display = e.value("display", def.name);
        def.tint = vec3Or(e, "tint", def.tint);
        def.glow = vec3Or(e, "glow", def.glow);
        def.velocityScale = e.value("velocityScale", 1.0f);
        def.dragScale = e.value("dragScale", 1.0f);
        def.trail = parseTrail(e.value("trail", std::string("none")));
        {
            const std::string kind = e.value("damage", std::string("plain"));
            if (kind == "plain") def.damage = DamageKind::Plain;
            else if (kind == "fire") def.damage = DamageKind::Fire;
            else if (kind == "wind") def.damage = DamageKind::Wind;
            else if (kind == "lightning") def.damage = DamageKind::Lightning;
            else throw std::runtime_error("Unknown damage kind: " + kind);
        }
        if (e.contains("impact")) {
            const json& impact = e.at("impact");
            def.volumeRadius = impact.value("volumeRadius", 0.0f);
            def.volumeLifetime = impact.value("volumeLifetime", 0.0f);
            def.gustImpulse = impact.value("gustImpulse", 0.0f);
            def.blastRadius = impact.value("blastRadius", 0.0f);
            def.blastKnockback = impact.value("blastKnockback", 0.0f);
            def.flameCone = impact.value("flameCone", 0.0f);
            def.driftSpeed = impact.value("driftSpeed", 0.0f);
            if (impact.contains("vortex")) {
                const json& v = impact.at("vortex");
                VortexParams& p = def.vortex;
                p.radius = v.value("radius", p.radius);
                p.coreRadius = v.value("coreRadius", p.coreRadius);
                p.height = v.value("height", p.height);
                p.inflowSpeed = v.value("inflowSpeed", p.inflowSpeed);
                p.swirlSpeed = v.value("swirlSpeed", p.swirlSpeed);
                p.liftSpeed = v.value("liftSpeed", p.liftSpeed);
                p.outflowSpeed = v.value("outflowSpeed", p.outflowSpeed);
                p.rampIn = v.value("rampIn", p.rampIn);
                p.rampOut = v.value("rampOut", p.rampOut);
                p.objectCoupling = v.value("objectCoupling", p.objectCoupling);
                p.playerCoupling = v.value("playerCoupling", p.playerCoupling);
            }
        }
        if (e.contains("fog")) {
            const json& fg = e.at("fog");
            def.fog.height = fg.value("height", 3.0f);
            def.fog.bulletHole = fg.value("bulletHole", def.fog.bulletHole);
            def.fog.holeLife = fg.value("holeLife", def.fog.holeLife);
            def.fog.blastHoleLife = fg.value("blastHoleLife", def.fog.blastHoleLife);
            def.fog.electrifyTime = fg.value("electrifyTime", def.fog.electrifyTime);
            def.fog.gustHole = fg.value("gustHole", def.fog.gustHole);
            def.fog.windTunnel = fg.value("windTunnel", def.fog.windTunnel);
        }
        if (e.contains("explosion")) {
            const json& ex = e.at("explosion");
            def.explosionRadiusScale = ex.value("radiusScale", 1.0f);
            def.explosionImpulse = ex.value("impulse", 0.0f);
            def.explosionGhostDamage = ex.value("ghostDamage", 0.0f);
            def.explosionPlayerDamage = ex.value("playerDamage", 0.0f);
            def.explosionLifetime = ex.value("lifetime", def.explosionLifetime);
        }
        if (e.contains("breath")) {
            const json& br = e.at("breath");
            def.breathRange = br.value("range", 5.0f);
            def.breathHalfAngle = br.value("halfAngle", 16.0f) * 3.14159265f / 180.0f;
            def.breathDuration = br.value("duration", def.breathDuration);
        }
        if (e.contains("ethereal")) {
            const json& eth = e.at("ethereal");
            def.ethereal = true;
            def.etherealDrag = eth.value("dragPerMetre", def.etherealDrag);
            def.gravityScale = eth.value("gravityScale", def.gravityScale);
            def.fadeSpeed = eth.value("fadeSpeed", 2.0f);
        }
        def.stealthy = e.value("stealthy", false);
        if (e.contains("self")) {
            const json& s = e.at("self");
            const std::string effect = s.at("effect").get<std::string>();
            if (effect == "hit") def.self = SelfEffect::Hit;
            else if (effect == "haste") def.self = SelfEffect::Haste;
            else if (effect == "shroud") def.self = SelfEffect::Shroud;
            else if (effect == "blink") def.self = SelfEffect::Blink;
            else if (effect == "reveal") def.self = SelfEffect::Reveal;
            else throw std::runtime_error("unknown self effect '" + effect + "'");
            def.selfHitStagger = s.value("stagger", 0.0f);
            def.selfDuration = s.value("duration", s.value("afterglow", 0.0f));
            def.selfSpeedScale = s.value("speedScale", 1.0f);
            def.selfDistance = s.value("distance", s.value("range", 0.0f));
            def.selfSpeed = s.value("speed", 0.0f);
        }
        if (e.contains("hitscan")) {
            def.hitscanRange = e.at("hitscan").value("range", 200.0f);
        }
        if (e.contains("strike")) {
            const json& s = e.at("strike");
            def.strikeDelay = s.value("delay", 1.2f);
            def.strikeCloudHeight = s.value("cloudHeight", def.strikeCloudHeight);
            def.strikeRadius = s.value("radius", def.strikeRadius);
            def.strikeImpulse = s.value("impulse", def.strikeImpulse);
            def.strikePower = s.value("power", def.strikePower);
        }
        data.elements.add(std::move(def));
    }
    if (data.elements.size() == 0 || data.elements[kPlainElement].name != "plain") {
        throw std::runtime_error("elements.json must list 'plain' first");
    }

    const json reactionsDoc = json::parse(reactionsJson);
    for (const json& r : reactionsDoc.at("reactions")) {
        data.reactions.add(data.element(r.at("a").get<std::string>()), data.element(r.at("b").get<std::string>()),
                           data.element(r.at("result").get<std::string>()));
    }

    const json materials = json::parse(materialsJson);
    data.maxDoses = materials.value("maxDoses", 3);
    data.turnsPerDose = materials.value("turnsPerDose", 2.5f);
    for (const json& m : materials.at("materials")) {
        MaterialDef def;
        def.name = m.at("name").get<std::string>();
        def.display = m.value("display", def.name);
        def.propellant = m.value("propellant", false);
        if (m.contains("element")) {
            def.element = data.element(m.at("element").get<std::string>());
        }
        def.color = vec3Or(m, "color", def.color);
        const std::string look = m.value("look", std::string("glow"));
        using Look = MaterialDef::Look;
        def.look = look == "storm_orb" ? Look::StormOrb
                   : look == "haze_bubble" ? Look::HazeBubble
                   : look == "wind_cloud"  ? Look::WindCloud
                   : look == "small_fire"  ? Look::SmallFire
                   : look == "thread_knot" ? Look::ThreadKnot
                                           : Look::Glow;
        data.materials.push_back(std::move(def));
    }
    return data;
}

AmmoData loadAmmoData(const std::filesystem::path& dataDir) {
    return parseAmmoData(readFile(dataDir / "elements.json"), readFile(dataDir / "reactions.json"),
                         readFile(dataDir / "materials.json"));
}

}
