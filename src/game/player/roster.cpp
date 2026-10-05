#include "game/player/roster.h"

#include "engine/assets/asset_path.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ghost::game {
namespace {
constexpr float kReportEvery = 0.4f;

}

PlayerRules parsePlayerRules(std::string_view text) {
    using nlohmann::json;
    PlayerRules rules;
    const json doc = json::parse(text);
    rules.regenDelay = doc.value("regenDelay", rules.regenDelay);
    rules.regenRate = doc.value("regenRate", rules.regenRate);
    rules.friendlyFire = doc.value("friendlyFire", rules.friendlyFire);
    if (doc.contains("pvp")) {
        const json& pvp = doc.at("pvp");
        rules.bodyDamage = pvp.value("body", rules.bodyDamage);
        rules.headDamage = pvp.value("head", rules.headDamage);
        rules.hitKnockback = pvp.value("knockback", rules.hitKnockback);
        if (pvp.contains("kind")) {
            const json& kind = pvp.at("kind");
            rules.kindScale[static_cast<std::size_t>(DamageKind::Plain)] = kind.value("plain", 1.0f);
            rules.kindScale[static_cast<std::size_t>(DamageKind::Fire)] = kind.value("fire", 1.0f);
            rules.kindScale[static_cast<std::size_t>(DamageKind::Wind)] = kind.value("wind", 1.0f);
            rules.kindScale[static_cast<std::size_t>(DamageKind::Lightning)] = kind.value("lightning", 1.0f);
        }
    }
    rules.burnPerSecond = doc.value("burnPerSecond", rules.burnPerSecond);
    rules.etherealScale = doc.value("etherealScale", rules.etherealScale);
    rules.ghostfirePerSecond = doc.value("ghostfirePerSecond", rules.ghostfirePerSecond);
    rules.strikeDamage = doc.value("strikeDamage", rules.strikeDamage);
    if (doc.contains("stealth")) {
        const json& stealth = doc.at("stealth");
        rules.stealthCrouch = stealth.value("crouch", rules.stealthCrouch);
        rules.stealthCrawl = stealth.value("crawl", rules.stealthCrawl);
    }
    if (doc.contains("revive")) {
        const json& revive = doc.at("revive");
        rules.reviveTime = revive.value("time", rules.reviveTime);
        rules.reviveHealth = revive.value("health", rules.reviveHealth);
        rules.reviveRange = revive.value("range", rules.reviveRange);
    }
    if (rules.reviveTime <= 0.0f || rules.reviveHealth <= 0.0f) {
        throw std::runtime_error("player.json: revive time and health must be above zero");
    }
    return rules;
}

PlayerRules loadPlayerRules(const std::filesystem::path& dataDirectory) {
    const std::filesystem::path path = dataDirectory / "player.json";
    const auto text = engine::readAsset(path);
    if (!text) {
        throw std::runtime_error("Missing data file: " + path.string());
    }
    return parsePlayerRules(*text);
}

void Roster::add(PlayerId id) {
    if (!find(id)) {
        RosterEntry e;
        e.id = id;
        m_entries.push_back(e);
    }
}

void Roster::remove(PlayerId id) {
    std::erase_if(m_entries, [id](const RosterEntry& e) { return e.id == id; });
    for (RosterEntry& e : m_entries) {
        if (e.reviver == id) {
            e.reviver = kNoPlayer;
            e.revive = 0.0f;
        }
    }
}

RosterEntry* Roster::entry(PlayerId id) {
    const auto it = std::find_if(m_entries.begin(), m_entries.end(), [id](const RosterEntry& e) { return e.id == id; });
    return it == m_entries.end() ? nullptr : &*it;
}

const RosterEntry* Roster::find(PlayerId id) const {
    const auto it = std::find_if(m_entries.begin(), m_entries.end(), [id](const RosterEntry& e) { return e.id == id; });
    return it == m_entries.end() ? nullptr : &*it;
}

bool Roster::downed(PlayerId id) const {
    const RosterEntry* e = find(id);
    return e && e->downed;
}

bool Roster::zombie(PlayerId id) const {
    const RosterEntry* e = find(id);
    return e && e->zombie;
}

bool Roster::possess(PlayerId player, PlayerId zombieId, float zombieHealth) {
    RosterEntry* e = entry(player);
    if (!e || !e->downed || e->zombie || e->possessedBy != kNoPlayer || find(zombieId)) {
        return false;
    }
    e->possessedBy = zombieId;
    e->revive = 0.0f;
    e->reviver = kNoPlayer;
    RosterEntry risen;
    risen.id = zombieId;
    risen.zombie = true;
    risen.health = zombieHealth;
    risen.sinceHurt = 0.0f;
    m_entries.push_back(risen);
    return true;
}

PlayerId Roster::release(PlayerId player) {
    RosterEntry* e = entry(player);
    if (!e || e->possessedBy == kNoPlayer) {
        return kNoPlayer;
    }
    const PlayerId was = e->possessedBy;
    e->possessedBy = kNoPlayer;
    remove(was);
    return was;
}

PlayerId Roster::madeOf(PlayerId zombieId) const {
    for (const RosterEntry& e : m_entries) {
        if (e.possessedBy == zombieId) {
            return e.id;
        }
    }
    return kNoPlayer;
}

void Roster::reset() {
    std::erase_if(m_entries, [](const RosterEntry& e) { return e.zombie; });
    for (RosterEntry& e : m_entries) {
        const PlayerId id = e.id;
        e = RosterEntry{};
        e.id = id;
    }
}

void Roster::hurt(PlayerId id, float amount, const glm::vec3& from, EventList& events, bool continuous, PlayerId by) {
    RosterEntry* e = entry(id);
    if (!e || e->downed || amount <= 0.0f) {
        return;
    }
    e->health = std::max(e->health - amount, 0.0f);
    e->sinceHurt = 0.0f;
    const bool down = e->health <= 0.0f;
    if (continuous && !down) {
        e->unreported += amount;
        if (e->sinceReport >= kReportEvery) {
            events.push_back(PlayerDamaged{e->unreported, from, id});
            e->unreported = 0.0f;
            e->sinceReport = 0.0f;
        }
    } else {
        events.push_back(PlayerDamaged{amount + e->unreported, from, id});
        e->unreported = 0.0f;
        e->sinceReport = 0.0f;
    }
    if (down) {
        e->downed = true;
        e->revive = 0.0f;
        e->reviver = kNoPlayer;
        e->downTime = 0.0f;
        events.push_back(PlayerDowned{id, by});
        if (RosterEntry* killer = (by != id) ? entry(by) : nullptr) {
            ++killer->kills;
        }
    }
}

void Roster::tick(float dt, std::span<const RosterInput> players, EventList& events) {
    auto inputOf = [&players](PlayerId id) -> const RosterInput* {
        for (const RosterInput& in : players) {
            if (in.id == id) {
                return &in;
            }
        }
        return nullptr;
    };

    for (RosterEntry& e : m_entries) {
        e.sinceHurt += dt;
        e.sinceReport += dt;
        if (!e.downed) {
            if (!m_rules->arena && !e.zombie && e.sinceHurt > m_rules->regenDelay) {
                e.health = std::min(e.health + m_rules->regenRate * dt, 1.0f);
            }
            continue;
        }
        e.downTime += dt;
        if (m_rules->arena) {
            if (e.downTime >= m_rules->arenaRespawn) {
                e.downed = false;
                e.health = 1.0f;
                e.sinceHurt = 1e3f;
                events.push_back(PlayerRespawned{e.id});
            }
            continue;
        }
        if (e.zombie || e.possessedBy != kNoPlayer) {
            e.revive = 0.0f;
            e.reviver = kNoPlayer;
            continue;
        }

        const RosterInput* self = inputOf(e.id);
        PlayerId helper = kNoPlayer;
        float nearest = m_rules->reviveRange;
        for (const RosterInput& in : players) {
            if (!self || in.id == e.id || !in.interact || downed(in.id) || zombie(in.id)) {
                continue;
            }
            const float distance = glm::distance(in.position, self->position);
            if (distance <= nearest) {
                nearest = distance;
                helper = in.id;
            }
        }
        if (helper == kNoPlayer) {
            e.revive = 0.0f;
            e.reviver = kNoPlayer;
            continue;
        }
        e.reviver = helper;
        e.revive += dt / m_rules->reviveTime;
        if (e.revive >= 1.0f) {
            e.downed = false;
            e.health = m_rules->reviveHealth;
            e.sinceHurt = 0.0f;
            e.revive = 0.0f;
            e.reviver = kNoPlayer;
            events.push_back(PlayerRevived{e.id, helper});
        }
    }

    const bool anyReal = std::any_of(m_entries.begin(), m_entries.end(), [](const RosterEntry& e) { return !e.zombie; });
    if (!m_rules->arena && anyReal && std::all_of(m_entries.begin(), m_entries.end(), [](const RosterEntry& e) { return e.downed || e.zombie; })) {
        std::erase_if(m_entries, [](const RosterEntry& e) { return e.zombie; });
        for (RosterEntry& e : m_entries) {
            const PlayerId id = e.id;
            e = RosterEntry{};
            e.id = id;
        }
        events.push_back(PlayerDied{});
    }
}

}
