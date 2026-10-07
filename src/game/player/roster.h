#pragma once

#include "game/ammo/element.h"
#include "game/events.h"
#include "game/player/player.h"
#include "game/player/player_hit.h"

#include <glm/glm.hpp>

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace ghost::game {
struct PlayerRules {
    float regenDelay = 4.0f;
    float regenRate = 0.12f;

    float bodyDamage = 0.35f;
    float headDamage = 0.7f;
    std::array<float, static_cast<std::size_t>(DamageKind::Count)> kindScale{1.0f, 1.0f, 0.6f, 1.2f};
    float hitKnockback = 1.5f;
    float burnPerSecond = 0.25f;

    float etherealScale = 0.3f;
    float ghostfirePerSecond = 0.2f;
    float strikeDamage = 0.6f;
    float reviveTime = 3.0f;
    float reviveHealth = 0.3f;
    float reviveRange = 2.0f;
    bool friendlyFire = true;

    bool arena = false;
    std::uint8_t arenaMap = 0;
    float arenaRespawn = 3.0f;
    int arenaHatLead = 5;

    float stealthCrouch = 0.6f;
    float stealthCrawl = 0.35f;

    float visibility(Stance stance) const {
        switch (stance) {
        case Stance::Crouch:
        case Stance::Slide:
            return stealthCrouch;
        case Stance::Crawl:
            return stealthCrawl;
        case Stance::Stand:
        case Stance::Dive:
        default:
            return 1.0f;
        }
    }

    float roundDamage(DamageKind kind, bool head) const {
        return (head ? headDamage : bodyDamage) * kindScale[static_cast<std::size_t>(kind)];
    }
};

PlayerRules parsePlayerRules(std::string_view json);
std::optional<PlayerId> dominantLeader(std::span<const struct RosterEntry> entries, int margin);
PlayerRules loadPlayerRules(const std::filesystem::path& dataDirectory);

struct RosterEntry {
    PlayerId id = 0;
    float health = 1.0f;
    float sinceHurt = 1e3f;
    bool downed = false;
    float revive = 0.0f;
    PlayerId reviver = kNoPlayer;
    float unreported = 0.0f;
    float sinceReport = 1e3f;
    float downTime = 0.0f;
    int kills = 0;

    bool zombie = false;
    PlayerId possessedBy = kNoPlayer;
};

struct RosterInput {
    PlayerId id = 0;
    glm::vec3 position{0.0f};
    bool interact = false;
};

class Roster {
public:
    explicit Roster(const PlayerRules& rules) : m_rules(&rules) {}

    void add(PlayerId id);
    void remove(PlayerId id);
    const std::vector<RosterEntry>& entries() const { return m_entries; }

    void replace(std::vector<RosterEntry> entries) { m_entries = std::move(entries); }
    const RosterEntry* find(PlayerId id) const;
    bool downed(PlayerId id) const;
    bool zombie(PlayerId id) const;

    bool possess(PlayerId player, PlayerId zombieId, float zombieHealth);

    PlayerId release(PlayerId player);

    PlayerId madeOf(PlayerId zombieId) const;

    void hurt(PlayerId id, float amount, const glm::vec3& from, EventList& events, bool continuous = false,
              PlayerId by = kNoPlayer);

    void reset();

    void tick(float dt, std::span<const RosterInput> players, EventList& events);

private:
    RosterEntry* entry(PlayerId id);

    const PlayerRules* m_rules;
    std::vector<RosterEntry> m_entries;
};

}
