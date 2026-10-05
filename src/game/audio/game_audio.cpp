#include "game/audio/game_audio.h"

#include "engine/assets/asset_path.h"
#include "engine/debug/log.h"
#include "game/audio/sound_synth.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace ghost::game {
namespace {
constexpr float kSpeedOfSound = 343.0f;
constexpr std::size_t kMaxLoopsPerKind = 6;
constexpr float kStrideLength = 1.9f;

const char* groupName(SoundGroup group) {
    switch (group) {
    case SoundGroup::Weapon:
        return "Weapon";
    case SoundGroup::World:
        return "World";
    case SoundGroup::Ghosts:
        return "Ghosts";
    case SoundGroup::Player:
        return "Player";
    case SoundGroup::Bench:
        return "Bench";
    case SoundGroup::Count:
    default:
        return "";
    }
}

const char* impactSound(Surface surface) {
    switch (surface) {
    case Surface::Concrete:
        return "impact.concrete";
    case Surface::Wood:
        return "impact.wood";
    case Surface::Steel:
        return "impact.steel";
    case Surface::Ghost:
    case Surface::Ground:
    default:
        return "impact.ground";
    }
}

}

GameAudio::GameAudio(const AmmoData& ammo) : m_ammo(&ammo) {
    buildPlaceholderSounds(m_bank);
    const std::size_t total = m_bank.names().size();

    int replaced = m_bank.loadAssets(engine::assetPath("audio"));
    if (engine::assetsEmbedded()) {
        replaced = std::max(replaced, m_bank.loadOverrides(engine::looseAssetPath("audio")));
    }
    GHOST_INFO("Audio: {} sounds ({} synthesized placeholders, {} from files)", total, total - static_cast<std::size_t>(replaced),
               replaced);
    m_engine.start();
}

float GameAudio::secondsAway(const glm::vec3& position) const {
    return glm::distance(position, m_listener) / kSpeedOfSound;
}

void GameAudio::play(std::string_view name, SoundGroup group, float volume, std::optional<glm::vec3> position, float pitch,
                     float delay) {
    engine::PlayParams params;
    params.volume = volume;

    params.pitch = pitch * (1.0f + 0.04f * (std::uniform_real_distribution<float>(-1.0f, 1.0f)(m_rng)));
    params.position = position;
    params.minDistance = 3.0f;
    params.maxDistance = 90.0f;
    params.group = static_cast<int>(group);
    params.delay = delay;
    m_engine.play(m_bank.pick(name, m_rng), params);
}

void GameAudio::onEvent(const GameEvent& event) {
    using G = SoundGroup;
    if (const auto* fired = std::get_if<ShotFired>(&event)) {
        const ElementDef& def = m_ammo->elements[fired->round.element];

        const bool cold = def.hitscanRange > 0.0f || def.self != SelfEffect::None || def.breathRange > 0.0f;
        play(cold ? "revolver.discharge" : "revolver.shot", G::Weapon);
        if (def.ethereal) {
            play("haze.shot", G::Weapon);
        }
    } else if (std::holds_alternative<DryFired>(event)) {
        play("revolver.dry", G::Weapon);
    } else if (std::holds_alternative<HammerCocked>(event)) {
        play("revolver.cock", G::Weapon);
    } else if (std::holds_alternative<CylinderOpened>(event)) {
        play("revolver.open", G::Weapon);
    } else if (std::holds_alternative<CylinderClosed>(event)) {
        play("revolver.close", G::Weapon);
    } else if (std::holds_alternative<ChambersEjected>(event)) {
        play("revolver.eject", G::Weapon);
    } else if (const auto* holstered = std::get_if<GunHolstered>(&event)) {
        play(holstered->away ? "revolver.holster" : "revolver.draw", G::Weapon);
    } else if (std::holds_alternative<SpeedloaderUsed>(event)) {
        play("revolver.speedload", G::Weapon);
    } else if (std::holds_alternative<RoundLoaded>(event)) {
        play("revolver.load", G::Weapon);
    } else if (std::holds_alternative<RoundPickedUp>(event)) {
        play("revolver.load", G::Player, 0.6f, std::nullopt, 1.2f);
    } else if (const auto* impact = std::get_if<ProjectileImpact>(&event)) {
        if (impact->surface == Surface::Ghost) {
            return;
        }
        const float late = secondsAway(impact->point);
        play(impactSound(impact->surface), G::World, 0.9f, impact->point, 1.0f, late);
        if (impact->ricochet) {
            play("impact.ricochet", G::World, 0.8f, impact->point, 1.0f, late);
        }
        const ElementDef& def = m_ammo->elements[impact->element];
        if (def.trail == TrailKind::Embers) {
            play("fire.catch", G::World, 0.9f, impact->point, 1.0f, late);
        } else if (def.trail == TrailKind::FlamingWhirl) {
            play("inferno.start", G::World, 1.0f, impact->point, 1.0f, late);
        } else if (def.fog.height > 0.0f) {
            play("fog.burst", G::World, 1.0f, impact->point, 1.0f, late);
        }
    } else if (const auto* transformed = std::get_if<ProjectileTransformed>(&event)) {
        play("fire.catch", G::World, 1.0f, transformed->point, 1.3f);
    } else if (const auto* faded = std::get_if<ProjectileFaded>(&event)) {
        play("haze.fade", G::World, 1.0f, faded->point);
    } else if (std::holds_alternative<PlayerHit>(event)) {
        play("voodoo.hit", G::Player);
    } else if (const auto* cast = std::get_if<SelfCast>(&event)) {
        switch (cast->effect) {
        case SelfEffect::Haste:
            play("wind.gust", G::Player, 0.8f, std::nullopt, 1.25f);
            break;
        case SelfEffect::Shroud:
            play("fog.burst", G::Player, 0.8f, std::nullopt, 0.8f);
            break;
        case SelfEffect::Blink:
            play("blink", G::Player);
            break;
        case SelfEffect::Reveal:
            play("firelight.pulse", G::Player);
            break;
        case SelfEffect::Hit:
        case SelfEffect::None:
        default:
            break;
        }
    } else if (const auto* charged = std::get_if<FogElectrified>(&event)) {
        play("fog.electrify", G::World, 1.0f, charged->center);
    } else if (std::holds_alternative<BreathFired>(event)) {
        play("ghostfire.breath", G::Weapon);
    } else if (const auto* hurt = std::get_if<GhostHurt>(&event)) {
        play("ghost.hurt", G::Ghosts, hurt->killed ? 1.0f : 0.8f, hurt->point, hurt->killed ? 0.85f : 1.0f);
    } else if (const auto* dodged = std::get_if<GhostDodged>(&event)) {
        play("wisp.dodge", G::Ghosts, 1.0f, dodged->position);
    } else if (const auto* burst = std::get_if<GhostBurst>(&event)) {
        play("wisp.burst", G::Ghosts, 1.0f, burst->position);
    } else if (const auto* threw = std::get_if<GhostThrew>(&event)) {
        play("poltergeist.throw", G::Ghosts, 1.0f, threw->from);
    } else if (const auto* died = std::get_if<GhostDied>(&event)) {
        if (!died->burst) {
            play("ghost.death", G::Ghosts, 1.0f, died->position);
        }
    } else if (const auto* damaged = std::get_if<PlayerDamaged>(&event)) {
        play("player.hurt", G::Player, std::clamp(0.5f + damaged->amount, 0.5f, 1.0f));
    } else if (std::holds_alternative<PlayerDied>(event)) {
        play("player.hurt", G::Player, 1.0f, std::nullopt, 0.6f);
    } else if (const auto* dropped = std::get_if<MaterialDropped>(&event)) {
        play("material.drop", G::World, 1.0f, dropped->position);
    } else if (std::holds_alternative<MaterialPickedUp>(event)) {
        play("pickup", G::Player);
    } else if (const auto* revealed = std::get_if<GhostRevealed>(&event)) {
        play("ghost.reveal", G::Ghosts, 1.0f, revealed->position);
    } else if (const auto* arcCharged = std::get_if<BallArcCharged>(&event)) {
        play("ball.crackle", G::Ghosts, 0.7f, arcCharged->to);
    } else if (const auto* arc = std::get_if<BallArc>(&event)) {
        play("ball.arc", G::Ghosts, 1.0f, arc->to);
    } else if (const auto* hop = std::get_if<BallHopped>(&event)) {
        play("ball.hop", G::Ghosts, 1.0f, hop->to);
    } else if (const auto* unmasked = std::get_if<MimicRevealed>(&event)) {
        play("mimic.reveal", G::Ghosts, 1.0f, unmasked->position);
    } else if (const auto* wormUp = std::get_if<NecromiteEmerged>(&event)) {
        play("necromite.emerge", G::Ghosts, 0.9f, wormUp->position);
    } else if (const auto* wormIn = std::get_if<NecromiteEntered>(&event)) {
        play("necromite.enter", G::Ghosts, 1.0f, wormIn->position);
        play("zombie.groan", G::Ghosts, 1.0f, wormIn->position, 1.0f, 0.5f);
    } else if (const auto* fell = std::get_if<ZombieFell>(&event)) {
        play("zombie.groan", G::Ghosts, 0.8f, fell->position, 0.8f);
    } else if (const auto* scattered = std::get_if<KrakaScattered>(&event)) {
        play("kraka.scatter", G::Ghosts, 0.8f, scattered->position);
    } else if (const auto* dive = std::get_if<KrakaDive>(&event)) {
        play("kraka.dive", G::Ghosts, 1.0f, dive->position);
    } else if (const auto* ball = std::get_if<KrakaBall>(&event)) {
        play("kraka.ball", G::Ghosts, 1.0f, ball->position);
    } else if (const auto* shrapnel = std::get_if<KrakaBurst>(&event)) {
        play("kraka.burst", G::Ghosts, 1.0f, shrapnel->position);
    } else if (const auto* merged = std::get_if<KrakaMerged>(&event)) {
        play("kraka.merge", G::Ghosts, 0.9f, merged->position);
    } else if (const auto* split = std::get_if<KrakaSplit>(&event)) {
        play("kraka.scatter", G::Ghosts, 1.0f, split->position);
    } else if (const auto* folded = std::get_if<MimicConcealed>(&event)) {
        play("mimic.conceal", G::Ghosts, 0.8f, folded->position);
    } else if (const auto* thrash = std::get_if<MimicThrash>(&event)) {
        play("mimic.lash", G::Ghosts, 1.0f, thrash->position);
    } else if (const auto* steam = std::get_if<SteamExplosion>(&event)) {
        play("steam.explosion", G::World, 1.0f, steam->center, 1.0f, secondsAway(steam->center));
    } else if (const auto* reacted = std::get_if<VolumeReacted>(&event)) {
        const bool inferno = m_ammo->elements[reacted->element].trail == TrailKind::FlamingWhirl;
        play(inferno ? "inferno.start" : "fire.catch", G::World, 1.0f, reacted->point);
    } else if (const auto* pressure = std::get_if<PressureBurst>(&event)) {
        play("wind.blast", G::World, 1.0f, pressure->center, 1.0f, secondsAway(pressure->center));
    } else if (const auto* gust = std::get_if<GustBurst>(&event)) {
        play("wind.gust", G::World, 1.0f, gust->center, 1.0f, secondsAway(gust->center));
    } else if (std::holds_alternative<LightningBolt>(event)) {
        play("lightning.crack", G::Weapon);
    } else if (const auto* called = std::get_if<StrikeCalled>(&event)) {
        const float length = m_bank.has("thunder.build") ? m_bank.variations("thunder.build").front()->seconds() : 1.2f;
        engine::PlayParams params;
        params.pitch = length / std::max(called->delay, 0.1f);
        params.position = called->point;
        params.minDistance = 6.0f;
        params.maxDistance = 120.0f;
        params.group = static_cast<int>(G::World);
        m_engine.play(m_bank.pick("thunder.build", m_rng), params);
    } else if (const auto* strike = std::get_if<LightningStrike>(&event)) {
        engine::PlayParams params;
        params.position = strike->point;
        params.minDistance = 10.0f;
        params.maxDistance = 200.0f;
        params.group = static_cast<int>(G::World);
        params.delay = secondsAway(strike->point);
        m_engine.play(m_bank.pick("thunder.strike", m_rng), params);
    }
}

void GameAudio::followEmitters(std::vector<Loop>& loops, const std::vector<SoundEmitter>& emitters, std::string_view sound,
                               SoundGroup group, float maxDistance, const glm::vec3& listener) {
    std::vector<const SoundEmitter*> nearest;
    for (const SoundEmitter& e : emitters) {
        if (glm::distance(e.position, listener) < maxDistance) {
            nearest.push_back(&e);
        }
    }
    std::sort(nearest.begin(), nearest.end(), [&](const SoundEmitter* a, const SoundEmitter* b) {
        return glm::distance(a->position, listener) < glm::distance(b->position, listener);
    });
    if (nearest.size() > kMaxLoopsPerKind) {
        nearest.resize(kMaxLoopsPerKind);
    }
    for (Loop& loop : loops) {
        loop.seen = false;
    }
    for (const SoundEmitter* e : nearest) {
        auto it = std::find_if(loops.begin(), loops.end(), [&](const Loop& l) { return l.id == e->id; });
        if (it == loops.end()) {
            engine::PlayParams params;
            params.volume = e->level;
            params.pitch = e->pitch;
            params.position = e->position;
            params.minDistance = 2.5f;
            params.maxDistance = maxDistance;
            params.loop = true;
            params.group = static_cast<int>(group);
            params.fadeIn = 0.25f;
            const engine::VoiceId voice = m_engine.play(m_bank.pick(sound, m_rng), params);
            if (voice == 0) {
                continue;
            }
            loops.push_back({e->id, voice, true});
            continue;
        }
        it->seen = true;
        m_engine.set(it->voice, e->level, e->pitch, e->position);
    }
    for (const Loop& loop : loops) {
        if (!loop.seen) {
            m_engine.stop(loop.voice, 0.4f);
        }
    }
    std::erase_if(loops, [](const Loop& l) { return !l.seen; });
}

void GameAudio::holdLoop(engine::VoiceId& voice, bool on, std::string_view sound, SoundGroup group, float volume, float pitch) {
    if (on && voice == 0) {
        engine::PlayParams params;
        params.volume = volume;
        params.pitch = pitch;
        params.loop = true;
        params.group = static_cast<int>(group);
        params.fadeIn = 0.2f;
        voice = m_engine.play(m_bank.pick(sound, m_rng), params);
    } else if (on) {
        m_engine.set(voice, volume, pitch);
    } else if (voice != 0) {
        m_engine.stop(voice, 0.4f);
        voice = 0;
    }
}

void GameAudio::update(const AudioFrame& frame) {
    m_listener = frame.listener;
    m_engine.setListener({frame.listener, frame.listenerRight});

    followEmitters(m_fireLoops, frame.fires, "fire.loop", SoundGroup::World, 35.0f, frame.listener);
    followEmitters(m_infernoLoops, frame.infernos, "inferno.loop", SoundGroup::World, 70.0f, frame.listener);
    followEmitters(m_wispLoops, frame.wisps, "wisp.loop", SoundGroup::Ghosts, 30.0f, frame.listener);

    holdLoop(m_tailwind, frame.hasted, "tailwind.loop", SoundGroup::Player, 1.0f);
    holdLoop(m_shroud, frame.shrouded, "shroud.loop", SoundGroup::Player, 1.0f);
    holdLoop(m_grind, frame.grinding, "bench.grind.loop", SoundGroup::Bench, 1.0f);

    const float danger = std::clamp((0.6f - frame.health) / 0.4f, 0.0f, 1.0f);
    holdLoop(m_heartbeat, frame.onFoot && danger > 0.0f, "heartbeat.loop", SoundGroup::Player, danger, 1.1f + 0.7f * danger);

    const float speed = glm::length(glm::vec2(frame.velocity.x, frame.velocity.z));

    holdLoop(m_slide, frame.onFoot && frame.sliding, "slide.loop", SoundGroup::Player, std::clamp(speed / 6.0f, 0.2f, 1.0f),
             0.8f + speed * 0.05f);
    if (frame.onFoot && frame.grounded && speed > 0.5f && !frame.sliding) {
        m_stride += speed * frame.dt;
        if (m_stride >= (frame.crawling ? 0.6f : kStrideLength)) {
            m_stride = 0.0f;
            play("step", SoundGroup::Player, std::clamp(0.5f + speed / 8.0f, 0.5f, 1.0f) * (frame.crawling ? 0.25f : frame.crouched ? 0.5f : 1.0f),
                 std::nullopt, frame.crawling ? 0.7f : 1.0f);
        }
    } else if (speed <= 0.5f) {
        m_stride = kStrideLength * 0.6f;
    }
    if (frame.onFoot && frame.grounded && !m_wasGrounded && m_fallSpeed > 2.0f) {
        play("land", SoundGroup::Player, std::clamp(m_fallSpeed / 8.0f, 0.3f, 1.0f));
    }
    m_wasGrounded = frame.grounded;
    if (!frame.grounded) {
        m_fallSpeed = std::max(0.0f, -frame.velocity.y);
    }

    if (m_lastChamber >= 0 && frame.alignedChamber != m_lastChamber) {
        play("revolver.turn", SoundGroup::Weapon);
    }
    m_lastChamber = frame.alignedChamber;
}

void GameAudio::drawDebugUi() {
    if (!ImGui::CollapsingHeader("Audio")) {
        return;
    }
    ImGui::Text("%s   voices: %zu", m_engine.running() ? "device open" : "no device (silent)", m_engine.voiceCount());
    if (ImGui::Checkbox("Mute", &m_muted)) {
        if (m_muted) {
            m_volumeBeforeMute = m_engine.masterVolume();
            m_engine.setMasterVolume(0.0f);
        } else {
            m_engine.setMasterVolume(m_volumeBeforeMute);
        }
    }
    float master = m_engine.masterVolume();
    if (!m_muted && ImGui::SliderFloat("Master", &master, 0.0f, 1.5f)) {
        m_engine.setMasterVolume(master);
    }
    for (int g = 0; g < static_cast<int>(SoundGroup::Count); ++g) {
        float volume = m_engine.groupVolume(g);
        if (ImGui::SliderFloat(groupName(static_cast<SoundGroup>(g)), &volume, 0.0f, 1.5f)) {
            m_engine.setGroupVolume(g, volume);
        }
    }

    if (ImGui::TreeNode("Sounds")) {
        for (const std::string& name : m_bank.names()) {
            if (ImGui::SmallButton(name.c_str())) {
                play(name, SoundGroup::World);
            }
        }
        ImGui::TreePop();
    }
}

}
