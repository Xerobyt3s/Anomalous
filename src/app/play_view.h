#pragma once

#include "core/types.h"
#include "engine/physics/physics_world.h"
#include "engine/render/mesh.h"
#include "engine/render/post_process.h"
#include "engine/render/primitives.h"
#include "engine/render/shader.h"
#include "game/ammo/ammo_data.h"
#include "game/audio/game_audio.h"
#include "game/fx/blob_body.h"
#include "game/fx/comet.h"
#include "game/fx/draw_order.h"
#include "game/fx/fire_fx.h"
#include "game/fx/fire_tornado.h"
#include "game/fx/fog_volume.h"
#include "game/fx/lightning.h"
#include "game/fx/material_orb.h"
#include "game/fx/mimic_body.h"
#include "game/fx/mimic_fx.h"
#include "game/fx/reveal_fx.h"
#include "game/fx/shard_flock.h"
#include "game/fx/shards.h"
#include "game/fx/strands.h"
#include "game/fx/vortex.h"
#include "game/fx/wisp_fx.h"
#include "app/player_bodies.h"
#include "game/render/materials.h"
#include "sim/gameplay.h"
#include "game/ammo/pouch.h"
#include "game/events.h"
#include "game/player/player.h"
#include "game/weapons/revolver_mechanism.h"
#include "game/weapons/revolver_view.h"
#include "game/weapons/viewmodel.h"
#include "math/vmath.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <deque>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <random>
#include <vector>

namespace anom {

class Input;
struct Camera;
struct PlayerCommand;

using namespace ::ghost::game;
namespace engine = ::ghost::engine;

struct InfernoTuning {
    float flashSize = 2.4f;
    float flashLight = 45.0f;
    float lightTime = 0.35f;
    float shockRadius = 4.0f;
    float shockTime = 0.35f;

    float height = 5.0f;
    float baseRadius = 0.34f;
    float topRadius = 1.8f;
    float touchdownTime = 0.6f;
    float rageTime = 2.5f;
    float ropeOutTime = 1.5f;
    TornadoStyle style;

    int embers = 130;
    float shakeDeg = 2.2f;
    float shakeRange = 16.0f;
    float scorchRadius = 1.5f;
    float hazeStrength = 6.0f;
};
struct LightningTuning {
    float jaggedness = 0.16f;
    float gunWidth = 0.022f;
    float strikeWidth = 0.09f;
    int restrikes = 3;
    float flashDecay = 0.035f;
    float afterglow = 0.16f;
    float leaderTime = 0.14f;
    float gunFlash = 0.16f;
    float strikeFlash = 0.7f;
    float light = 30.0f;
    float cloudRadius = 3.2f;
};
struct HazeTuning {
    float headRadius = 0.07f;
    float tailLinger = 0.3f;
    float brightness = 1.0f;
    float refraction = 12.0f;
};
struct SelfFxTuning {
    float whirlwind = 1.0f;
    float rushStrength = 0.8f;
    float hasteFov = 6.0f;
    float mistDensity = 1.0f;
    float gunDissolve = 0.5f;
    float arcRate = 24.0f;
};
struct SpreadTuning {
    float hipDeg = 0.5f;
    float aimedDeg = 0.15f;
    float crouchScale = 0.6f;
};

struct CameraRecoilTuning {
    float permanentDeg = 1.4f;
    float kickImpulse = 170.0f;
    float yawImpulse = 40.0f;
    float rollImpulse = 90.0f;
    float stiffness = 260.0f;
    float dampingRatio = 0.75f;
    float shakeDeg = 0.35f;
    float shakeTime = 0.14f;
    float fovPunch = 70.0f;
};

struct EffectsTuning {
    float flashSize = 0.13f;
    float flashTime = 0.07f;
    float flashLight = 14.0f;
    float smokeOpacity = 0.42f;
    float smokeLife = 1.8f;
    float impactScale = 1.0f;
};

struct PlayFrame {
    u8 local = 0;
    ghost::game::PlayerState state;
    ghost::game::PlayerState previous;
    ghost::game::PlayerState world;
    bool onFoot = true;
    bool armed = true;
    bool windowBlocked = false;
    bool downed = false;
    bool canDraw = true;
    bool holdingItem = false;
    float lowering = -1.0f;
    bool driving = false;
    ghost::game::MechanismView mechanism;
    const ghost::game::MechanismState* gun = nullptr;
    const ghost::game::AmmoPouch* pouch = nullptr;
    const ghost::game::SpeedloaderRack* speedloaders = nullptr;
    float walkSpeed = 3.6f;
    float hasteFov = 0.0f;
};

struct FrameLights {
    glm::vec3 sunDirection{0.4f, 0.8f, 0.45f};
    glm::vec3 sunColor{3.0f, 2.9f, 2.7f};
};

struct HudPlayer {
    u8 id = 0;
    std::string name;
    glm::vec3 color{1.0f};
    glm::vec3 feet{0.0f};
    glm::vec3 head{0.0f};
    float shroudFade = 0.0f;
};

struct ScoreLine {
    std::string name;
    int kills = 0;
    glm::vec3 color{1.0f};
};

struct HudFrame {
    glm::mat4 viewProj{1.0f};
    std::vector<HudPlayer> others;
    const ghost::game::Roster* roster = nullptr;
    const ghost::game::PlayerRules* rules = nullptr;
    float pickupProgress = 0.0f;
    std::vector<ScoreLine> score;
};

struct PropView {
    glm::vec3 center{0.0f};
    glm::vec3 half{0.5f};
    glm::vec3 color{0.5f};
    ghost::game::Surface surface = ghost::game::Surface::Concrete;
};

struct TintedCylinder {
    glm::mat4 model{1.0f};
    glm::vec3 color{0.5f};
};

struct SeeThrough {
    float distance = 0.0f;
    std::function<void()> draw;
    std::function<void(const DrawWindow&)> span;
};

struct OthersHooks {
    std::function<void(u8, const glm::vec3&, float, bool)> jolt;
    std::function<void(u8)> recoil;
    std::function<bool(u8, BodyPose&, float&, glm::vec3&, float&)> pose;
};

class PlayView {
public:
    PlayView(ghost::game::Gameplay& play, const engine::PhysicsWorld& physics);
    void setOthers(OthersHooks hooks) { m_others = std::move(hooks); }
    void setPost(engine::PostProcess* post) { m_post = post; }

    void readInput(const Input& input, bool allowed, float frameDt, const PlayFrame& frame, PlayerCommand& pending);
    void onEvents(const ghost::game::EventList& events, const PlayFrame& frame, bool client);
    void renderEffects(const glm::mat4& viewProj, const Camera& camera, float alpha, std::span<const SeeThrough> extras = {});
    unsigned sceneDepthCopy() const { return m_sceneDepthCopy; }
    void finishFrame(const glm::mat4& viewProj, const glm::vec3& eye);
    float hurtShown() const { return m_hurtShown; }
    glm::mat4 itemToView() const { return m_itemViewmodel.modelToView(); }
    void drawHud(const HudFrame& hud);
    void renderCylinders(const std::vector<TintedCylinder>& cylinders, const glm::mat4& viewProj, const glm::vec3& cameraPos,
                         const FrameLights& lights);
    void renderProps(const std::vector<PropView>& props, const glm::mat4& viewProj, const glm::vec3& cameraPos, const FrameLights& lights);
    void renderPropShadows(const std::vector<PropView>& props, const glm::mat4& lightViewProj, unsigned program);
    GunPoints gunPoints() const { return {m_revolver.muzzle(), m_revolver.gripCenter(), m_revolver.roundCenter(), true}; }
    void renderOtherGuns(const std::vector<OtherGun>& guns, const glm::mat4& viewProj, const glm::vec3& cameraPos);
    const GameAudio& audio() const { return m_audio; }
    GameAudio& gameAudio() { return m_audio; }
    void update(float dt, const PlayFrame& frame);
    void adjustCamera(Camera& camera, const PlayFrame& frame, float dt);
    void prepareGun(const Camera& camera, float aspect);
    void renderGun(const Camera& camera, float aspect, const PlayFrame& frame, const FrameLights& lights);
    void renderWorld(const glm::mat4& viewProj, const glm::vec3& cameraPos, const FrameLights& lights);

    glm::vec3 aimOrigin() const { return m_aimOrigin; }
    glm::vec3 aimDirection() const { return m_aimDirection; }
    float takeRecoilPitch();
    bool radialOpen() const { return m_radialOpen; }
    const std::vector<ghost::game::ElementId>& radialOptions() const { return m_radialOptions; }
    int radialChoice() const { return m_radialChoice; }
    bool gunVisible() const { return m_gunVisible; }
    const ghost::game::RevolverView& revolver() const { return m_revolver; }
    const ghost::game::Viewmodel& viewmodel() const { return m_viewmodel; }

private:
    struct ImpactMark {
        glm::vec3 point;
        glm::vec3 normal;
        Surface surface;
        float spin;
        std::uint32_t body = 0;
        bool attached = false;
    };
    struct ImpactFlash {
        glm::vec3 point;
        Surface surface;
        float age = 0.0f;
        float seed = 0.0f;
        glm::vec3 color{-1.0f};
        float sizeScale = 1.0f;
        float duration = 0.1f;
        float spikes = -1.0f;
    };
    struct LightSpike {
        glm::vec3 position{0.0f};
        glm::vec3 color{1.0f};
        float age = 0.0f;
        float duration = 0.3f;
    };
    struct Spark {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec3 color{1.0f, 0.6f, 0.2f};
        float age = 0.0f;
        float life = 1.5f;
        float size = 0.04f;
        float gravity = 1.0f;
        glm::vec3 coolTo{0.8f, 0.12f, 0.02f};
        glm::vec3 orbitCenter{0.0f};
        float orbit = 0.0f;
        glm::vec3 up{0.0f};
    };
    struct Ring {
        glm::vec3 center{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        float maxRadius = 2.0f;
        float duration = 0.35f;
        float age = 0.0f;
        bool fire = false;
        float seed = 0.0f;
    };
    struct Scorch {
        glm::vec3 point{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        float radius = 1.0f;
        float age = 0.0f;
        float seed = 0.0f;
        std::uint32_t body = 0;
        bool attached = false;
    };
    struct InfernoBlast {
        glm::vec3 center{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        float scale = 1.0f;
        float age = 0.0f;
        float duration = 10.0f;
        float seed = 0.0f;
        float smokeTimer = 0.0f;

        std::array<float, kTornadoGroundCells> ground{};
        float groundBase = 0.0f;
        glm::vec3 groundAt{0.0f};
        float groundTimer = 0.0f;
    };
    struct TrailSegment {
        glm::vec3 from{0.0f};
        glm::vec3 to{0.0f};
        ElementId element = kPlainElement;
        float age = 0.0f;
        float seed = 0.0f;
        float life = 0.8f;
    };
    struct ActiveJet {
        glm::vec3 origin{0.0f};
        glm::vec3 direction{0.0f, 0.0f, -1.0f};
        float length = 1.0f;
        float age = 0.0f;
    };
    struct ActiveBolt {
        Bolt bolt;
        glm::vec3 from{0.0f};
        glm::vec3 to{0.0f};
        std::uint32_t seed = 0;
        std::uint32_t strikeId = 0;
        float age = 0.0f;
        float leaderStart = -1.0f;
        bool struck = true;
        float strikeAge = 0.0f;
        float hold = 0.0f;
        std::vector<float> flashTimes;
        int flashesDone = 0;
        float width = 0.03f;
        float power = 1.0f;
        float frameFlash = 0.0f;
        float light = 0.0f;
        float jitterSeed = 0.0f;
        bool fromMuzzle = false;
    };
    struct StormCloud {
        std::uint32_t id = 0;
        glm::vec3 center{0.0f};
        glm::vec3 target{0.0f};
        float age = 0.0f;
        float delay = 1.2f;
        bool struck = false;
        float flickerTimer = 0.2f;
        float spawnBudget = 0.0f;
    };
    struct HazeComet {
        std::uint32_t id = 0;
        glm::vec3 head{0.0f};
        glm::vec3 direction{0.0f, 0.0f, -1.0f};
        float travelled = 0.0f;
        float tail = 0.0f;
        float fade = 1.0f;
        bool alive = true;
        float seed = 0.0f;
        float moteBudget = 0.0f;
    };
    struct Ripple {
        glm::vec3 point{0.0f};
        float age = 0.0f;
        float duration = 0.4f;
        float radius = 0.5f;
        float seed = 0.0f;
    };
    struct SelfFx {
        float haste = 0.0f;
        float shroud = 0.0f;
        float shroudCast = 10.0f;
        float blinkGlow = 0.0f;
        float blinkDuration = 1.6f;
        float blinkRush = 0.0f;
        float mistBudget = 0.0f;
        float arcBudget = 0.0f;
        float gunArcBudget = 0.0f;
    };
    struct GunArc {
        Bolt bolt;
        float age = 0.0f;
        float life = 0.12f;
        float seed = 0.0f;
    };
    struct Puff {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec3 color{0.5f};
        float age = 0.0f;
        float life = 1.5f;
        float startSize = 0.05f;
        float endSize = 0.4f;
        float opacity = 0.3f;
        float seed = 0.0f;
    };
    struct ShownDrop {
        MaterialId material = 0;
        glm::vec3 position{0.0f};
        float age = 0.0f;
        float scale = 1.0f;
        bool resting = true;
    };
    struct Absorb {
        glm::vec3 from{0.0f};
        glm::vec3 position{0.0f};
        glm::vec3 target{0.0f};
        glm::vec3 color{1.0f};
        float age = 0.0f;
        float life = 0.7f;
        float seed = 0.0f;
        MaterialDef::Look look = MaterialDef::Look::Glow;
        MaterialId material = 0;
        PlayerId player = 0;
    };
    struct ArcMark {
        glm::vec3 position{0.0f};
        float delay = 0.35f;
        float age = 0.0f;
        float seed = 0.0f;
    };
    struct RevealShot {
        glm::vec3 heart{0.0f};
        glm::vec3 cloud{0.0f};
        glm::vec3 tail{0.0f, 1.0f, 0.0f};
        float radius = 0.4f;
        float seed = 0.0f;
        float phase = 0.0f;
        bool apparition = false;
        float flow = 0.0f;
        float age = 0.0f;
        float duration = 2.0f;

        bool body = false;
        BodyPose pose;
        float headRadius = 0.25f;
        glm::vec3 color{1.0f};
    };
    struct WispCloud {
        std::uint32_t id = 0;
        glm::vec3 center{0.0f};
        glm::vec3 velocity{0.0f};
        float phase = 0.0f;
        float flow = 0.0f;
        float radius = 0.7f;
        float glow = 1.0f;
        float seed = 0.0f;
        float dispersing = 0.0f;
        bool apparition = false;
        float show = 0.0f;
        bool alive = true;
    };
    struct HitMark {
        enum class Kind { Hit, Kill, NoEffect };
        float age01 = 0.0f;
        Kind kind = Kind::Hit;
        std::uint32_t ghost = 0;
    };
    struct BulletWake {
        std::uint32_t id = 0;
        glm::vec3 from{0.0f};
        glm::vec3 to{0.0f};
        float fade = 0.0f;
        bool alive = true;
        int ricochets = 0;
    };
    struct SmokePuff {
        FogCloud cloud;
        glm::vec3 velocity{0.0f};
    };
    struct SteamBurst {
        glm::vec3 center{0.0f};
        float radius = 6.0f;
        float height = 4.5f;
        float age = 0.0f;
        float life = 1.6f;
        float strength = 1.0f;
        std::uint32_t id = 0;
        FogReach reach = filledReach(kFogUnlimited);
    };
    struct BreathFx {
        glm::vec3 origin{0.0f};
        glm::vec3 direction{0.0f, 0.0f, -1.0f};
        float range = 5.0f;
        float halfAngle = 0.28f;
        float duration = 0.5f;
        float age = 0.0f;
        float budget = 0.0f;
        bool mine = true;
    };
    struct RibbonPiece {
        glm::vec3 from{0.0f};
        glm::vec3 to{0.0f};
        float age = 0.0f;
    };
    struct MimicShown {
        MimicBody body;
        float lashAge = 1e6f;
        int steps = 0;
        glm::vec3 lashAt{0.0f};
    };
    struct Debris {
        glm::mat4 spawn{1.0f};
        ghost::game::ElementId element = ghost::game::kPlainElement;
        glm::vec3 spawnCenter{0.0f};
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 spin{0.0f};
        float age = 0.0f;
        bool resting = false;
        bool viewmodelLayer = false;
    };

    void spawnEmber(const glm::vec3& at, float strength);
    static float absorbFlare(const Absorb& a);
    void spawnSparks(const glm::vec3& at, int count, float speed, float upBias,
                     const glm::vec3& color = glm::vec3(-1.0f));
    void startInferno(const glm::vec3& point, const glm::vec3& normal, float scale, bool scorch = true);
    void updateElementFx(float dt, float alpha);
    void updateComet(const Projectile& projectile, const glm::vec3& from, const glm::vec3& now, float dt);
    void drawComets(const glm::mat4& viewProj, const glm::vec3& cameraPos);
    void addTrail(const glm::vec3& from, const glm::vec3& to, ElementId element);
    void drawElementFx(const glm::mat4& viewProj, const glm::mat4& invView);
    void addBolt(ActiveBolt bolt, const BoltParams& params);
    void updateSelfFx(float dt);
    void drawWhirlwind(const glm::mat4& viewProj, const glm::vec3& eye, const glm::vec3& forward);
    void drawGunArcs(const glm::mat4& viewProj, const glm::vec3& cameraPos, const glm::mat4& gunToWorld);
    void updateBreathFx(float dt);
    void updateRevealFx(float dt);
    void drawRevealMarks(const glm::mat4& viewProj, const glm::mat4& invView, const glm::vec3& cameraPos,
                         const glm::vec3& cameraForward);
    glm::vec3 ghostShownAt(const Ghost& ghost) const;
    glm::vec3 wispColor(const Ghost& ghost) const;
    float wispGlow(const Ghost& ghost) const;
    void updateAudio(float dt);
    void updateGhostFx(float dt);
    void setGhostLook(bool apparition);
    void drawWisps(const glm::mat4& viewProj, const glm::vec3& cameraPos, const glm::vec3& cameraForward);
    float disguiseShown(const Ghost& ghost, glm::vec3& shake) const;
    std::vector<ShownDrop> shownDrops() const;
    void updateFlocks(float dt);
    void drawFlocks(const glm::mat4& viewProj, const glm::vec3& cameraPos);
    void updateMimicBodies(float dt);
    void drawMimics(const glm::mat4& viewProj, const glm::vec3& cameraPos);
    void drawMaterialOrbs(const glm::mat4& viewProj, const glm::vec3& cameraPos);
    void drawGhosts(const glm::mat4& viewProj, const glm::mat4& invView);
    void updateFogFx(float dt);
    void drawFog(const glm::mat4& viewProj, const glm::vec3& cameraPos, const glm::vec3& cameraForward);
    void updateLightning(float dt);
    void drawRibbons(const glm::mat4& viewProj, const glm::vec3& cameraPos);
    void drawLightning(const glm::mat4& viewProj, const glm::vec3& cameraPos);
    void gatherDistortion(const glm::mat4& viewProj, const glm::vec3& eye);
    void toBodySpace(std::uint32_t body, glm::vec3& point, glm::vec3& normal) const;
    void toWorldSpace(std::uint32_t body, bool attached, glm::vec3& point, glm::vec3& normal) const;
    void spawnPuff(const glm::vec3& position, const glm::vec3& velocity, const glm::vec3& color, float life,
                   float startSize, float endSize, float opacity);
    void updatePuffs(float dt);
    void drawPuffs(const glm::mat4& viewProj, const glm::mat4& invView);
    void drawDecals(const glm::mat4& viewProj);
    void drawProjected(const glm::vec3& center, const glm::vec3& normal, float size, float depth, const glm::vec3& color,
                       float opacity, float seed, int kind);
    bool inWindow(const glm::vec3& point) const;
    float fogDistance(const glm::vec3& eye) const;
    void drawSeeThrough(const glm::mat4& viewProj, const glm::mat4& invView, const glm::vec3& cameraPos, const glm::vec3& cameraForward);
    void drawTracers(const glm::mat4& viewProj, float alpha);
    void drawEffects(const glm::mat4& viewProj, const glm::mat4& invView);
    TornadoInstance tornadoOf(const InfernoBlast& b) const;
    void drawTornadoes(const glm::mat4& viewProj, const glm::vec3& cameraPos, const glm::vec3& cameraForward);
    void drawFlashAxes(const glm::mat4& viewProj, const glm::vec3& center, const glm::vec3& right,
                       const glm::vec3& up, float size, const glm::vec3& color, float intensity, float seed,
                       float spikes, float opacity);
    void drawFlash(const glm::mat4& viewProj, const glm::mat4& invView, const glm::vec3& center, float size,
                   const glm::vec3& color, float intensity, float seed, float spikes);
    void setFrameLights(const glm::vec3& muzzleWorld);
    void consumeEvents();
    bool wispLike(const Ghost& ghost) const
    {
        const GhostBehavior b = m_ghosts.def(ghost).behavior;
        return b == GhostBehavior::Wisp || b == GhostBehavior::Poltergeist;
    }
    const MechanismState& gunState() const;
    void drawRadial();
    void drawPlayersHud(const HudFrame& hud);
    void drawArenaHud(const HudFrame& hud);
    glm::vec3 absorbTarget(PlayerId player) const;
    void readLoadSelection(const Input& input, const PlayFrame& frame, PlayerCommand& pending);
    void updateDebris(float dt);
    void updateItem(float dt, const PlayFrame& frame);
    void trackChambers(float dt);
    glm::mat4 debrisModel(const Debris& d) const;

    ghost::game::Gameplay& m_playMut;
    const ghost::game::Gameplay& m_play;
    const engine::PhysicsWorld& m_physics;
    const AmmoData& m_ammo;
    const GhostWorld& m_ghosts;
    const ElementVolumes& m_volumes;
    const std::vector<WindVortex>& m_vortices;
    const FogField& m_fog;
    const std::vector<Breath>& m_breaths;
    const std::vector<RevealPulse>& m_reveals;
    const std::vector<RevealMark>& m_revealMarks;
    const std::vector<MaterialDrop>& m_materialDrops;
    const std::vector<DroppedRound>& m_dropped;
    const Ballistics& m_ballistics;
    const FogParams* m_fogParams = nullptr;
    ElementId m_fogElement = kPlainElement;
    engine::PostProcess* m_post = nullptr;
    OthersHooks m_others;
    const EventList* m_eventsIn = nullptr;
    std::span<const SeeThrough> m_extras;
    PlayFrame m_frame;
    ghost::game::PlayerState m_world;
    static constexpr float kRibbonLife = 0.35f;
    PlayerTuning m_playerTuning;
    PlayerId m_local = 0;
    bool m_client = false;
    glm::vec3 m_cameraPos{0.0f};
    glm::ivec2 m_viewport{1, 1};
    std::vector<Absorb> m_absorbs;
    std::vector<ArcMark> m_arcMarks;
    GameAudio m_audio{m_ammo};
    float m_ballArcBudget = 0.0f;
    float m_blastShake = 0.0f;
    std::vector<InfernoBlast> m_blasts;
    BlobBody m_blob;
    std::vector<ActiveBolt> m_bolts;
    std::vector<BreathFx> m_breathFx;
    std::vector<StormCloud> m_clouds;
    Comet m_comet;
    std::vector<HazeComet> m_comets;
    std::vector<ShardFlock> m_fallenFlocks;
    FireFx m_fire;
    std::unordered_map<std::uint32_t, ShardFlock> m_flocks;
    float m_fogArcBudget = 0.0f;
    FogStyle m_fogStyle;
    FogVolume m_fogVolume;
    float m_frameFlash = 0.0f;
    std::vector<FogLight> m_frameLights;
    float m_ghostFxBudget = 0.0f;
    std::vector<GunArc> m_gunArcs;
    HazeTuning m_hazeTuning;
    std::vector<std::pair<std::uint32_t, GhostState>> m_heardGhostStates;
    float m_hitFlash = 0.0f;
    glm::vec3 m_hitFlashColor{0.3f, 0.03f, 0.4f};
    HitMark m_hitMark;
    float m_hurtFlash = 0.0f;
    float m_hurtShown = 0.0f;
    std::vector<ImpactFlash> m_impactFlashes;
    InfernoTuning m_inferno;
    std::vector<ActiveJet> m_jets;
    std::vector<LightSpike> m_lightSpikes;
    Lightning m_lightning;
    LightningTuning m_lightningTuning;
    std::deque<ImpactMark> m_marks;
    MaterialOrb m_materialOrb;
    MimicFx m_mimicFx;
    std::unordered_map<std::uint32_t, MimicShown> m_mimics;
    std::uint32_t m_nextSmokeId = 1;
    std::vector<Puff> m_puffs;
    float m_revealDim = 0.0f;
    RevealFx m_revealFx;
    float m_revealFxBudget = 0.0f;
    std::vector<RevealShot> m_revealShots;
    std::vector<RibbonPiece> m_ribbons;
    std::vector<Ring> m_rings;
    std::vector<Ripple> m_ripples;
    unsigned m_sceneDepthCopy = 0;
    std::vector<Scorch> m_scorches;
    SelfFx m_selfFx;
    SelfFxTuning m_selfFxTuning;
    Shards m_shardsFx;
    bool m_showTracers = false;
    std::vector<SmokePuff> m_smoke;
    std::vector<Spark> m_sparks;
    std::vector<SteamBurst> m_steam;
    Strands m_strands;
    FireTornado m_tornado;
    std::unordered_map<std::uint32_t, glm::vec3> m_trailHeads;
    std::vector<TrailSegment> m_trails;
    engine::Mesh m_unitBox;
    Vortex m_vortex;
    std::vector<BulletWake> m_wakes;
    glm::vec3 m_whirlAxis{0.0f, 0.0f, -1.0f};
    DrawWindow m_window;
    glm::vec3 m_windowEye{0.0f};
    std::vector<WispCloud> m_wispClouds;
    WispFx m_wispFx;
    double m_lastShotTime = -10.0;
    glm::vec3 m_lightDir{0.4f, 0.8f, 0.45f};

    ghost::game::RevolverView m_revolver;
    ghost::game::Viewmodel m_viewmodel;
    ghost::game::Viewmodel m_itemViewmodel;
    std::unique_ptr<MaterialLibrary> m_surfaces;
    engine::Mesh m_cylinder = engine::makeCylinder(20);
    float m_itemLower = 1.0f;
    bool m_itemDriving = false;
    ghost::engine::Shader m_litShader;
    ghost::engine::Shader m_flashShader;
    ghost::engine::Shader m_decalShader;
    ghost::engine::Mesh m_quad;
    CameraRecoilTuning m_cameraRecoil;
    EffectsTuning m_effects;
    ghost::game::SpringVec3 m_cameraKick;
    ghost::game::SpringVec3 m_fovPunch;
    float m_shakeTime = 0.0f;
    float m_shakeSeed = 0.0f;
    float m_slideTilt = 0.0f;
    float m_flashTime = 0.0f;
    float m_flashSeed = 0.0f;
    bool m_flashElectric = false;
    float m_fxTime = 0.0f;
    float m_frameDt = 0.0f;
    float m_pendingPitch = 0.0f;
    glm::vec2 m_lookDelta{0.0f};
    float m_lastYaw = 0.0f;
    float m_lastPitch = 0.0f;
    bool m_lookValid = false;
    glm::mat4 m_gunToWorld{1.0f};
    glm::mat4 m_view{1.0f};
    float m_worldFovDeg = 75.0f;
    glm::vec3 m_aimOrigin{0.0f};
    glm::vec3 m_aimDirection{0.0f, 0.0f, -1.0f};
    glm::vec3 m_lastMuzzleWorld{0.0f};
    glm::vec3 m_lastGunForward{0.0f, 0.0f, -1.0f};
    bool m_gunVisible = false;
    bool m_triggerBlocked = false;
    float m_loadHold = -1.0f;
    bool m_radialOpen = false;
    glm::vec2 m_flick{0.0f};
    std::vector<ghost::game::ElementId> m_radialOptions;
    int m_radialChoice = -1;
    std::vector<Debris> m_debris;
    std::array<glm::mat4, ghost::game::kChamberCount> m_lastChamberTransforms{};
    std::array<glm::vec3, ghost::game::kChamberCount> m_chamberCenters{};
    std::array<glm::vec3, ghost::game::kChamberCount> m_chamberVelocities{};
    bool m_chambersTracked = false;
    std::minstd_rand m_fxRng{1234};
};

}
