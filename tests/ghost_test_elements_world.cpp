#include "engine/assets/asset_path.h"
#include "game/ammo/ammo_data.h"
#include "game/ballistics/ballistics.h"
#include "game/world/element_volume.h"

#include <doctest/doctest.h>

#include <cmath>
#include <optional>
#include <variant>

using namespace ghost::game;
using ghost::engine::RayHit;

namespace {
constexpr float kDt = 1.0f / 120.0f;

const AmmoData& data() {
    static const AmmoData d = loadAmmoData(ghost::engine::assetPath("data"));
    return d;
}

RaycastFn wallAt(float z) {
    return [z](const glm::vec3& from, const glm::vec3& to) -> std::optional<RayHit> {
        if (from.z < z || to.z > z) {
            return std::nullopt;
        }
        const float t = (from.z - z) / (from.z - to.z);
        RayHit hit;
        hit.point = from + (to - from) * t;
        hit.normal = {0.0f, 0.0f, 1.0f};
        hit.userData = static_cast<std::uint64_t>(Surface::Concrete);
        return hit;
    };
}

template <typename T>
const T* find(const EventList& events) {
    for (const GameEvent& e : events) {
        if (const auto* t = std::get_if<T>(&e)) {
            return t;
        }
    }
    return nullptr;
}

}

TEST_CASE("segmentSphere finds the entry point") {
    const auto t = segmentSphere({0, 0, 0}, {0, 0, -10}, {0, 0, -5}, 1.0f);
    REQUIRE(t);
    CHECK(*t == doctest::Approx(0.4f));
    CHECK_FALSE(segmentSphere({0, 0, 0}, {0, 0, -10}, {0, 5, -5}, 1.0f));
    CHECK(segmentSphere({0, 0, -5}, {0, 0, -10}, {0, 0, -5}, 1.0f) == 0.0f);
}

TEST_CASE("A wind round flying through a fire patch becomes an inferno round") {
    ElementVolumes volumes;
    volumes.spawn(data().element("fire"), {0.0f, 1.0f, -10.0f}, {0, 1, 0}, 0.6f, 5.0f);

    Ballistics b;
    EventList events;
    const ElementDef& wind = data().elements[data().element("wind")];
    b.fire({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, Round{data().element("wind")}, wind.velocityScale, wind.dragScale);
    const VolumeReactFn react = [&](const glm::vec3& from, const glm::vec3& to, ElementId element, glm::vec3* where) {
        return volumes.reactAlong(from, to, element, data().reactions, where);
    };
    for (int i = 0; i < 120 && !find<ProjectileImpact>(events); ++i) {
        b.tick(kDt, wallAt(-30.0f), events, react);
    }

    const auto* transformed = find<ProjectileTransformed>(events);
    REQUIRE(transformed);
    CHECK(transformed->element == data().element("inferno"));
    CHECK(transformed->point.z == doctest::Approx(-9.4f).epsilon(0.02));
    const auto* impact = find<ProjectileImpact>(events);
    REQUIRE(impact);
    CHECK(impact->element == data().element("inferno"));
}

TEST_CASE("A plain round passes through fire unchanged") {
    ElementVolumes volumes;
    volumes.spawn(data().element("fire"), {0.0f, 1.0f, -10.0f}, {0, 1, 0}, 0.6f, 5.0f);
    CHECK_FALSE(volumes.reactAlong({0, 1, 0}, {0, 1, -20}, kPlainElement, data().reactions));
}

TEST_CASE("Wind rounds fly faster than plain ones") {
    Ballistics b;
    const ElementDef& wind = data().elements[data().element("wind")];
    b.fire({0, 0, 0}, {0, 0, -1}, Round{}, 1.0f, 1.0f);
    b.fire({0, 0, 0}, {0, 0, -1}, Round{data().element("wind")}, wind.velocityScale, wind.dragScale);
    CHECK(glm::length(b.projectiles()[1].velocity) > glm::length(b.projectiles()[0].velocity));
}

TEST_CASE("A gust over a fire patch flares it into an inferno patch") {
    ElementVolumes volumes;
    EventList events;
    volumes.spawn(data().element("fire"), {0, 0, 0}, {0, 1, 0}, 0.5f, 6.0f);
    volumes.spawn(data().element("wind"), {1.0f, 0, 0}, {0, 1, 0}, 2.5f, 0.3f);
    volumes.tick(kDt, data(), events);

    REQUIRE(volumes.all().size() == 1);
    CHECK(volumes.all()[0].element == data().element("inferno"));
    CHECK(volumes.all()[0].radius >= data().elements[data().element("inferno")].volumeRadius);
    CHECK(find<VolumeReacted>(events));
}

TEST_CASE("Volumes expire") {
    ElementVolumes volumes;
    EventList events;
    volumes.spawn(data().element("wind"), {0, 0, 0}, {0, 1, 0}, 2.5f, 0.3f);
    for (int i = 0; i < 60; ++i) {
        volumes.tick(kDt, data(), events);
    }
    CHECK(volumes.all().empty());
}

TEST_CASE("A lightning shot through a wind gust becomes a thunderstrike") {
    ElementVolumes volumes;
    volumes.spawn(data().element("wind"), {0.0f, 1.0f, -10.0f}, {0, 1, 0}, 2.5f, 0.35f);
    const auto changed = volumes.reactAlong({0, 1, 0}, {0, 1, -40}, data().element("lightning"), data().reactions);
    REQUIRE(changed);
    CHECK(*changed == data().element("thunderstrike"));
}

TEST_CASE("A fire round flying into a fog cloud turns to steam there; a plain round does not") {
    ElementVolumes volumes;
    volumes.spawn(data().element("fog"), {0.0f, 0.0f, -12.0f}, {0, 1, 0}, 6.0f, 15.0f);
    glm::vec3 where{0.0f};
    const auto changed = volumes.reactAlong({0, 1, 0}, {0, 1, -40}, data().element("fire"), data().reactions, &where);
    REQUIRE(changed);
    CHECK(*changed == data().element("steam"));
    CHECK(where.z == doctest::Approx(-6.08f).epsilon(0.02));
    CHECK_FALSE(volumes.reactAlong({0, 1, 0}, {0, 1, -40}, kPlainElement, data().reactions));

    Ballistics b;
    EventList events;
    b.fire({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, Round{data().element("fire")});
    const VolumeReactFn react = [&](const glm::vec3& from, const glm::vec3& to, ElementId element, glm::vec3* at) {
        return volumes.reactAlong(from, to, element, data().reactions, at);
    };
    for (int i = 0; i < 30 && !find<ProjectileTransformed>(events); ++i) {
        b.tick(kDt, wallAt(-60.0f), events, react);
    }
    const auto* transformed = find<ProjectileTransformed>(events);
    REQUIRE(transformed);
    CHECK(transformed->element == data().element("steam"));
    REQUIRE(b.projectiles().size() == 1);
    b.remove(transformed->projectile);
    CHECK(b.projectiles().empty());
}

TEST_CASE("A fire patch under a fog cloud reacts to steam, and the steam can be cleared") {
    ElementVolumes volumes;
    EventList events;
    volumes.spawn(data().element("fire"), {0, 0, 0}, {0, 1, 0}, 0.5f, 6.0f);
    volumes.spawn(data().element("fog"), {2.0f, 0, 0}, {0, 1, 0}, 6.0f, 15.0f);
    volumes.tick(kDt, data(), events);
    const auto* reacted = find<VolumeReacted>(events);
    REQUIRE(reacted);
    CHECK(reacted->element == data().element("steam"));
    volumes.removeElement(data().element("steam"));
    CHECK(volumes.all().empty());
}

TEST_CASE("A volume that is blown along travels, and feeding it gives it its time back") {
    ElementVolumes volumes;
    EventList events;
    const std::uint32_t id = volumes.spawn(data().element("inferno"), {0, 0, 0}, {0, 1, 0}, 1.3f, 10.0f).id;
    for (int i = 0; i < 600; ++i) {
        volumes.tick(kDt, data(), events);
    }
    CHECK(volumes.all()[0].center.x == doctest::Approx(0.0f));
    CHECK(volumes.all()[0].age == doctest::Approx(5.0f).epsilon(0.01));

    volumes.push(id, {2.0f, 0.0f, 0.0f});
    volumes.refresh(id, 0.8f);
    CHECK(volumes.all()[0].age == doctest::Approx(0.8f));
    for (int i = 0; i < 240; ++i) {
        volumes.tick(kDt, data(), events);
    }
    CHECK(volumes.all()[0].center.x == doctest::Approx(4.0f).epsilon(0.01));

    volumes.push(id, glm::vec3(0.0f));
    volumes.tick(kDt, data(), events);
    CHECK(volumes.all()[0].center.x == doctest::Approx(4.0f).epsilon(0.01));
    CHECK(data().elements[data().element("inferno")].driftSpeed > 0.0f);
}

TEST_CASE("A settling volume keeps to the ground: it drops off a wall, steps up a little rise, falls off a ledge") {
    constexpr float dt = 1.0f / 120.0f;

    auto ground = [](const glm::vec3& at) -> std::optional<float> {
        const float top = at.y + ElementVolumes::kStepUp;
        float g = at.z > 5.0f ? -3.0f : (at.x > 2.0f ? 0.3f : 0.0f);
        if (g > top) {
            return std::nullopt;
        }
        return g;
    };
    const ElementId inferno = 1;
    const ElementId fire = 2;
    auto settles = [&](ElementId e) { return e == inferno; };
    auto run = [&](ElementVolumes& volumes, float seconds) {
        for (int i = 0; i < static_cast<int>(seconds / dt); ++i) {
            volumes.settle(dt, settles, ground);
            for (const ElementVolume& v : volumes.all()) {
                REQUIRE(std::isfinite(v.center.y));
            }

            const auto all = volumes.all();
            for (const ElementVolume& v : all) {
                volumes.adjust(v.id, v.velocity * dt, 0.0f);
            }
        }
    };
    ElementVolumes volumes;
    const std::uint32_t hot = volumes.spawn(inferno, {0.0f, 1.8f, 0.0f}, {1.0f, 0.0f, 0.0f}, 1.0f, 100.0f).id;
    const std::uint32_t warm = volumes.spawn(fire, {0.0f, 1.8f, 1.0f}, {1.0f, 0.0f, 0.0f}, 1.0f, 100.0f).id;
    auto find = [&](std::uint32_t id) {
        for (const ElementVolume& v : volumes.all()) {
            if (v.id == id) {
                return v;
            }
        }
        FAIL("gone");
        return ElementVolume{};
    };
    run(volumes, 1.5f);
    CHECK(find(hot).center.y == doctest::Approx(ElementVolumes::kRestHeight).epsilon(0.01));
    CHECK(find(hot).normal.y == doctest::Approx(1.0f));
    CHECK(find(warm).center.y == doctest::Approx(1.8f));

    volumes.push(hot, {2.0f, 0.0f, 0.0f});
    run(volumes, 1.5f);
    CHECK(find(hot).center.x > 2.5f);
    CHECK(find(hot).center.y == doctest::Approx(0.3f + ElementVolumes::kRestHeight).epsilon(0.01));

    volumes.push(hot, {0.0f, 0.0f, 4.0f});
    run(volumes, 3.0f);
    CHECK(find(hot).center.z > 5.0f);
    CHECK(find(hot).center.y == doctest::Approx(-3.0f + ElementVolumes::kRestHeight).epsilon(0.01));
}

TEST_CASE("A settling volume with no ground under it falls without blowing up") {
    ElementVolumes volumes;
    volumes.spawn(1, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 1.0f, 100.0f);
    for (int i = 0; i < 600; ++i) {
        volumes.settle(1.0f / 120.0f, [](ElementId) { return true; }, [](const glm::vec3&) { return std::optional<float>{}; });
    }
    CHECK(volumes.all().front().velocity.y < -5.0f);
    CHECK(std::isfinite(volumes.all().front().velocity.y));
}
