#include "game/ghosts/ghost_data.h"

#include "engine/assets/asset_path.h"

#include <algorithm>
#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ghost::game {
using nlohmann::json;

std::optional<GhostTypeId> GhostData::find(std::string_view name) const {
    for (std::size_t i = 0; i < types.size(); ++i) {
        if (types[i].name == name) {
            return static_cast<GhostTypeId>(i);
        }
    }
    return std::nullopt;
}

GhostTypeId GhostData::type(std::string_view name) const {
    if (const auto id = find(name)) {
        return *id;
    }
    throw std::runtime_error("Unknown ghost type: " + std::string(name));
}

std::int16_t pickDisguise(const GhostDef& def, float kind, float which) {
    const std::vector<MaterialId>& materials = def.mimic.disguises;
    if (materials.empty() || kind < def.mimic.boxChance) {
        return kDisguiseBox;
    }
    return static_cast<std::int16_t>(materials[std::min(static_cast<std::size_t>(which * static_cast<float>(materials.size())), materials.size() - 1)]);
}

std::optional<GhostDrop> chooseDrop(const GhostDef& def, std::int16_t disguise, float pick, float chance) {
    if (disguise >= 0 && disguise < kDisguiseBox && chance < def.dropDisguiseChance) {
        return GhostDrop{static_cast<MaterialId>(disguise), 1};
    }
    if (def.drops.empty()) {
        return std::nullopt;
    }
    const auto index = std::min(static_cast<std::size_t>(pick * static_cast<float>(def.drops.size())), def.drops.size() - 1);
    return def.drops[index];
}

GhostData parseGhostData(std::string_view ghostsJson, const AmmoData& ammo) {
    GhostData data;
    const json doc = json::parse(ghostsJson);
    data.roundDamage = doc.value("roundDamage", data.roundDamage);
    for (const json& g : doc.at("ghosts")) {
        GhostDef def;
        def.name = g.at("name").get<std::string>();
        def.display = g.value("display", def.name);
        const std::string behavior = g.value("behavior", std::string("wisp"));
        if (behavior == "poltergeist") {
            def.behavior = GhostBehavior::Poltergeist;
        } else if (behavior == "ball_lightning") {
            def.behavior = GhostBehavior::BallLightning;
        } else if (behavior == "vasskraka") {
            def.behavior = GhostBehavior::Vasskraka;
        } else if (behavior == "necromite") {
            def.behavior = GhostBehavior::Necromite;
        } else if (behavior == "mimic") {
            def.behavior = GhostBehavior::Mimic;
        } else if (behavior == "wisp") {
            def.behavior = GhostBehavior::Wisp;
        } else {
            throw std::runtime_error("Unknown ghost behavior: " + behavior);
        }
        def.health = g.value("health", def.health);
        def.radius = g.value("radius", def.radius);
        def.weight = g.value("weight", def.weight);
        def.sightRange = g.value("sightRange", def.sightRange);
        def.seesThroughWalls = g.value("seesThroughWalls", false);
        def.hearingRange = g.value("hearingRange", def.hearingRange);
        def.memory = g.value("memory", def.memory);
        def.dodgeChance = g.value("dodgeChance", 0.0f);
        def.invisible = g.value("invisible", false);
        if (g.contains("hitstop")) {
            def.hitstopMin = g.at("hitstop").value("min", def.hitstopMin);
            def.hitstopMax = g.at("hitstop").value("max", def.hitstopMax);
        }
        if (g.contains("poltergeist")) {
            const json& pg = g.at("poltergeist");
            PoltergeistParams& pp = def.poltergeist;
            pp.hoverHeight = pg.value("hoverHeight", pp.hoverHeight);
            pp.speed = pg.value("speed", pp.speed);
            pp.wanderRadius = pg.value("wanderRadius", pp.wanderRadius);
            pp.grabRange = pg.value("grabRange", pp.grabRange);
            pp.liftHeight = pg.value("liftHeight", pp.liftHeight);
            pp.holdTime = pg.value("holdTime", pp.holdTime);
            pp.throwSpeed = pg.value("throwSpeed", pp.throwSpeed);
            pp.throwDamage = pg.value("throwDamage", pp.throwDamage);
            pp.cooldown = pg.value("cooldown", pp.cooldown);
            pp.flinchTime = pg.value("flinchTime", pp.flinchTime);
        }
        if (g.contains("damage")) {
            const json& d = g.at("damage");
            def.damage[static_cast<std::size_t>(DamageKind::Plain)] = d.value("plain", 1.0f);
            def.damage[static_cast<std::size_t>(DamageKind::Fire)] = d.value("fire", 1.0f);
            def.damage[static_cast<std::size_t>(DamageKind::Wind)] = d.value("wind", 1.0f);
            def.damage[static_cast<std::size_t>(DamageKind::Lightning)] = d.value("lightning", 1.0f);
        }
        if (g.contains("drops")) {
            for (const json& drop : g.at("drops")) {
                const std::string material = drop.at("material").get<std::string>();
                const auto id = ammo.findMaterial(material);
                if (!id) {
                    throw std::runtime_error("Ghost '" + def.name + "' drops unknown material: " + material);
                }
                def.drops.push_back({*id, drop.value("count", 1)});
            }
        }
        if (g.contains("wisp")) {
            const json& w = g.at("wisp");
            WispParams& p = def.wisp;
            p.hoverHeight = w.value("hoverHeight", p.hoverHeight);
            p.wanderSpeed = w.value("wanderSpeed", p.wanderSpeed);
            p.wanderRadius = w.value("wanderRadius", p.wanderRadius);
            p.lureDistance = w.value("lureDistance", p.lureDistance);
            p.lureSpeed = w.value("lureSpeed", p.lureSpeed);
            p.sway = w.value("sway", p.sway);
            p.swaySpeed = w.value("swaySpeed", p.swaySpeed);
            p.rushSpeed = w.value("rushSpeed", p.rushSpeed);
            p.rushTrigger = w.value("rushTrigger", p.rushTrigger);
            p.lookAwayTime = w.value("lookAwayTime", p.lookAwayTime);
            p.burstRadius = w.value("burstRadius", p.burstRadius);
            p.burstDamage = w.value("burstDamage", p.burstDamage);
            p.flinchTime = w.value("flinchTime", p.flinchTime);
        }
        def.dropDisguiseChance = g.value("dropDisguiseChance", 0.0f);
        if (g.contains("ballLightning")) {
            const json& b = g.at("ballLightning");
            BallLightningParams& p = def.ballLightning;
            p.hoverHeight = b.value("hoverHeight", p.hoverHeight);
            p.chargeTime = b.value("chargeTime", p.chargeTime);
            p.arcEvery = b.value("arcEvery", p.arcEvery);
            p.arcsPerVolley = b.value("arcsPerVolley", p.arcsPerVolley);
            p.arcReach = b.value("arcReach", p.arcReach);
            p.arcDelay = b.value("arcDelay", p.arcDelay);
            p.arcRadius = b.value("arcRadius", p.arcRadius);
            p.arcDamage = b.value("arcDamage", p.arcDamage);
            p.hops = b.value("hops", p.hops);
            p.hopEvery = b.value("hopEvery", p.hopEvery);
            p.hopMin = b.value("hopMin", p.hopMin);
            p.hopMax = b.value("hopMax", p.hopMax);
            p.circleRange = b.value("circleRange", p.circleRange);
            p.circleRadius = b.value("circleRadius", p.circleRadius);
            p.flinchTime = b.value("flinchTime", p.flinchTime);
            p.wanderSpeed = b.value("wanderSpeed", p.wanderSpeed);
            p.wanderRadius = b.value("wanderRadius", p.wanderRadius);
            p.driftSpeed = b.value("driftSpeed", p.driftSpeed);
        }
        if (def.behavior == GhostBehavior::Mimic) {
            MimicParams& p = def.mimic;
            if (g.contains("mimic")) {
                const json& m = g.at("mimic");
                p.bodyRadius = m.value("bodyRadius", p.bodyRadius);
                p.standHeight = m.value("standHeight", p.standHeight);
                p.disguisedRadius = m.value("disguisedRadius", p.disguisedRadius);
                p.revealNear = m.value("revealNear", p.revealNear);
                p.revealLinger = m.value("revealLinger", p.revealLinger);
                p.revealGrab = m.value("revealGrab", p.revealGrab);
                p.revealTime = m.value("revealTime", p.revealTime);
                p.revealRadius = m.value("revealRadius", p.revealRadius);
                p.revealDamage = m.value("revealDamage", p.revealDamage);
                p.revealShove = m.value("revealShove", p.revealShove);
                p.burstSpeed = m.value("burstSpeed", p.burstSpeed);
                p.turnRate = m.value("turnRate", p.turnRate);
                p.acceleration = m.value("acceleration", p.acceleration);
                p.burstMin = m.value("burstMin", p.burstMin);
                p.burstMax = m.value("burstMax", p.burstMax);
                p.pauseMin = m.value("pauseMin", p.pauseMin);
                p.pauseMax = m.value("pauseMax", p.pauseMax);
                p.climbHeight = m.value("climbHeight", p.climbHeight);
                p.dropRange = m.value("dropRange", p.dropRange);
                p.wallSearch = m.value("wallSearch", p.wallSearch);
                p.leapMin = m.value("leapMin", p.leapMin);
                p.leapMax = m.value("leapMax", p.leapMax);
                p.leapSpeed = m.value("leapSpeed", p.leapSpeed);
                p.thrashRange = m.value("thrashRange", p.thrashRange);
                p.thrashReach = m.value("thrashReach", p.thrashReach);
                p.thrashWindup = m.value("thrashWindup", p.thrashWindup);
                p.thrashDamage = m.value("thrashDamage", p.thrashDamage);
                p.thrashShove = m.value("thrashShove", p.thrashShove);
                p.thrashCooldown = m.value("thrashCooldown", p.thrashCooldown);
                p.leapDamage = m.value("leapDamage", p.leapDamage);
                p.leapHitRadius = m.value("leapHitRadius", p.leapHitRadius);
                p.leapShove = m.value("leapShove", p.leapShove);
                p.spotMin = m.value("spotMin", p.spotMin);
                p.spotMax = m.value("spotMax", p.spotMax);
                p.patience = m.value("patience", p.patience);
                p.senseRange = m.value("senseRange", p.senseRange);
                p.fleeMin = m.value("fleeMin", p.fleeMin);
                p.fleeMax = m.value("fleeMax", p.fleeMax);
                p.concealTime = m.value("concealTime", p.concealTime);
                p.boxChance = m.value("boxChance", p.boxChance);
                p.flinchTime = m.value("flinchTime", p.flinchTime);
            }
            for (std::size_t m = 0; m < ammo.materials.size(); ++m) {
                p.disguises.push_back(static_cast<MaterialId>(m));
            }
        }
        if (g.contains("vasskraka")) {
            const json& k = g.at("vasskraka");
            VasskrakaParams& p = def.vasskraka;
            const std::pair<const char*, float*> numbers[] = {
                {"roostSearch", &p.roostSearch},   {"returnSpeed", &p.returnSpeed},     {"circleRadius", &p.circleRadius},
                {"flySpeed", &p.flySpeed},
                {"circleHeight", &p.circleHeight}, {"circleSpeed", &p.circleSpeed},     {"attackEvery", &p.attackEvery},
                {"ballChance", &p.ballChance},     {"diveWindup", &p.diveWindup},       {"diveSpeed", &p.diveSpeed},
                {"diveRadius", &p.diveRadius},     {"diveDamage", &p.diveDamage},       {"diveShove", &p.diveShove},
                {"gatherTime", &p.gatherTime},     {"ballHeight", &p.ballHeight},       {"burstRadius", &p.burstRadius},
                {"burstDamage", &p.burstDamage},   {"burstShove", &p.burstShove},       {"reformTime", &p.reformTime},
                {"scatterTime", &p.scatterTime},   {"scatterDistance", &p.scatterDistance}, {"mergeRange", &p.mergeRange},
                {"maxHealth", &p.maxHealth},       {"splitAbove", &p.splitAbove},       {"splitCooldown", &p.splitCooldown},
                {"pushDamage", &p.pushDamage},     {"minSize", &p.minSize},           {"fallLean", &p.fallLean},
                {"firstAttack", &p.firstAttack},
            };
            for (const auto& [key, value] : numbers) {
                *value = k.value(key, *value);
            }
        }
        if (g.contains("necromite")) {
            const json& n = g.at("necromite");
            NecromiteParams& p = def.necromite;
            const std::pair<const char*, float*> worm[] = {
                {"spawnChance", &p.spawnChance}, {"spawnDelay", &p.spawnDelay}, {"spawnMin", &p.spawnMin},   {"spawnMax", &p.spawnMax},
                {"emergeTime", &p.emergeTime},   {"crawlSpeed", &p.crawlSpeed}, {"enterRange", &p.enterRange}, {"enterTime", &p.enterTime},
            };
            for (const auto& [key, value] : worm) {
                *value = n.value(key, *value);
            }
            if (n.contains("zombie")) {
                const json& z = n.at("zombie");
                ZombieTuning& t = p.zombie;
                const std::pair<const char*, float*> body[] = {
                    {"health", &t.health},             {"riseTime", &t.riseTime},       {"shotEveryMin", &t.shotEveryMin},
                    {"shotEveryMax", &t.shotEveryMax}, {"spreadDeg", &t.spreadDeg},     {"aimWander", &t.aimWander},
                    {"diveRange", &t.diveRange},       {"diveMin", &t.diveMin},         {"diveDelay", &t.diveDelay},
                    {"leapCooldown", &t.leapCooldown}, {"standDelay", &t.standDelay},   {"ramSpeed", &t.ramSpeed},
                    {"ramDamage", &t.ramDamage},       {"ramShove", &t.ramShove},       {"ramCooldown", &t.ramCooldown},
                };
                for (const auto& [key, value] : body) {
                    *value = z.value(key, *value);
                }
            }
        }
        data.types.push_back(std::move(def));
    }
    return data;
}

GhostData loadGhostData(const std::filesystem::path& dataDirectory, const AmmoData& ammo) {
    const std::filesystem::path path = dataDirectory / "ghosts.json";
    const auto text = engine::readAsset(path);
    if (!text) {
        throw std::runtime_error("Missing data file: " + path.string());
    }
    return parseGhostData(*text, ammo);
}

}
