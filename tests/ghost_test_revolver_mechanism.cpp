#include "game/weapons/revolver_mechanism.h"

#include <doctest/doctest.h>

#include <variant>
#include <vector>

using namespace ghost::game;

namespace {
constexpr float kDt = 1.0f / 120.0f;

int run(RevolverMechanism& m, MechanismInput input, float seconds, EventList& events) {
    int shots = 0;
    for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
        const std::size_t before = events.size();
        m.tick(input, kDt, events);
        input.cock = false;
        for (std::size_t e = before; e < events.size(); ++e) {
            shots += std::holds_alternative<ShotFired>(events[e]) ? 1 : 0;
        }
    }
    return shots;
}

MechanismInput pull() {
    MechanismInput input;
    input.triggerHeld = true;
    return input;
}

MechanismInput cock() {
    MechanismInput input;
    input.cock = true;
    return input;
}

}

TEST_CASE("Double action: a full pull turns the cylinder, then fires the next chamber") {
    RevolverMechanism m;
    m.loadAll(Round{});
    EventList events;

    const float pullTime = m.tuning().doubleActionPullTime;
    CHECK(run(m, pull(), pullTime * 0.5f, events) == 0);
    CHECK(run(m, pull(), pullTime, events) == 1);

    CHECK(m.state().aligned == 1);
    CHECK(m.state().chambers[1].state == ChamberState::Spent);
    CHECK(m.state().chambers[0].state == ChamberState::Live);
    CHECK(m.state().phase == HammerPhase::Down);
}

TEST_CASE("Holding the trigger does not fire again until it is released") {
    RevolverMechanism m;
    m.loadAll(Round{});
    EventList events;

    CHECK(run(m, pull(), 2.0f, events) == 1);
    run(m, MechanismInput{}, 0.3f, events);
    CHECK(run(m, pull(), 1.0f, events) == 1);
}

TEST_CASE("Single action: cocking turns the cylinder, then a short pull fires") {
    RevolverMechanism m;
    m.loadAll(Round{});
    EventList events;

    run(m, cock(), m.tuning().cockTime + 0.05f, events);
    CHECK(m.state().phase == HammerPhase::Cocked);
    CHECK(m.state().aligned == 1);
    CHECK(m.state().nextToFire() == 1);

    CHECK(run(m, pull(), m.tuning().singleActionPullTime + 0.02f, events) == 1);
    CHECK(m.state().chambers[1].state == ChamberState::Spent);
}

TEST_CASE("Easing off a double-action pull before the break doesn't fire") {
    RevolverMechanism m;
    m.loadAll(Round{});
    EventList events;

    CHECK(run(m, pull(), m.tuning().doubleActionPullTime * 0.5f, events) == 0);
    run(m, MechanismInput{}, 0.3f, events);
    CHECK(m.state().hammer == doctest::Approx(0.0f));
    CHECK(m.state().trigger == doctest::Approx(0.0f));
}

TEST_CASE("Empty and spent chambers dry fire") {
    RevolverMechanism m;
    EventList events;
    run(m, pull(), 1.0f, events);
    REQUIRE(events.size() == 1);
    CHECK(std::holds_alternative<DryFired>(events[0]));
}

TEST_CASE("Six double-action shots empty all six chambers exactly once") {
    RevolverMechanism m;
    m.loadAll(Round{});
    EventList events;

    int shots = 0;
    for (int i = 0; i < 8; ++i) {
        shots += run(m, pull(), 0.5f, events);
        run(m, MechanismInput{}, 0.2f, events);
    }
    CHECK(shots == 6);
    for (const Chamber& chamber : m.state().chambers) {
        CHECK(chamber.state == ChamberState::Spent);
    }
}

namespace {
MechanismInput toggle() {
    MechanismInput input;
    input.toggleCylinder = true;
    return input;
}

MechanismInput loading() {
    MechanismInput input;
    input.load = true;
    input.roundToLoad = Round{7};
    return input;
}

void press(RevolverMechanism& m, const MechanismInput& edge, float seconds, EventList& events) {
    m.tick(edge, kDt, events);
    run(m, MechanismInput{}, seconds, events);
}

template <typename T>
int count(const EventList& events) {
    int n = 0;
    for (const GameEvent& e : events) {
        n += std::holds_alternative<T>(e) ? 1 : 0;
    }
    return n;
}

}

TEST_CASE("The cylinder swings out and back in, taking time each way") {
    RevolverMechanism m;
    EventList events;
    press(m, toggle(), m.tuning().openTime * 0.5f, events);
    CHECK(m.state().cylinder == CylinderPhase::Opening);
    run(m, MechanismInput{}, m.tuning().openTime, events);
    CHECK(m.state().cylinder == CylinderPhase::Open);
    CHECK(count<CylinderOpened>(events) == 1);

    press(m, toggle(), m.tuning().closeTime + 0.05f, events);
    CHECK(m.state().isClosed());
    CHECK(count<CylinderClosed>(events) == 1);
}

TEST_CASE("Opening lowers a cocked hammer, and an open gun can't fire") {
    RevolverMechanism m;
    m.loadAll(Round{});
    EventList events;
    run(m, cock(), 0.4f, events);
    REQUIRE(m.state().phase == HammerPhase::Cocked);

    press(m, toggle(), 0.5f, events);
    CHECK(m.state().phase == HammerPhase::Down);
    CHECK(m.state().cylinder == CylinderPhase::Open);
    CHECK(run(m, pull(), 1.0f, events) == 0);
    CHECK(count<DryFired>(events) == 0);
}

TEST_CASE("Ejecting drops everything, spent and live") {
    RevolverMechanism m;
    m.loadAll(Round{});
    EventList events;
    run(m, pull(), 0.5f, events);
    run(m, MechanismInput{}, 0.2f, events);
    press(m, toggle(), 0.5f, events);

    MechanismInput eject;
    eject.eject = true;
    press(m, eject, m.tuning().ejectTime + 0.05f, events);

    REQUIRE(count<ChambersEjected>(events) == 1);
    for (const GameEvent& e : events) {
        if (const auto* ejected = std::get_if<ChambersEjected>(&e)) {
            int live = 0;
            int spent = 0;
            for (const Chamber& c : ejected->contents) {
                live += c.state == ChamberState::Live ? 1 : 0;
                spent += c.state == ChamberState::Spent ? 1 : 0;
            }
            CHECK(live == 5);
            CHECK(spent == 1);
        }
    }
    for (const Chamber& c : m.state().chambers) {
        CHECK(c.state == ChamberState::Empty);
    }
}

TEST_CASE("Loading puts one round in the selected chamber per insert time") {
    RevolverMechanism m;
    EventList events;
    press(m, toggle(), 0.5f, events);

    MechanismInput turn;
    turn.turn = 2;
    press(m, turn, 0.2f, events);
    CHECK(m.state().aligned == 2);

    m.tick(loading(), kDt, events);
    run(m, MechanismInput{}, m.tuning().insertTime + 0.05f, events);
    CHECK(count<RoundLoaded>(events) == 1);
    CHECK(m.state().chambers[3].state == ChamberState::Live);
    CHECK(m.state().chambers[3].round.element == 7);
    CHECK(m.state().chambers[4].state == ChamberState::Empty);
}

TEST_CASE("Holding load fills the empty chambers one after another") {
    RevolverMechanism m;
    EventList events;
    press(m, toggle(), 0.5f, events);
    run(m, loading(), m.tuning().insertTime * 6.0f + 0.2f, events);
    CHECK(count<RoundLoaded>(events) == 6);
    for (const Chamber& c : m.state().chambers) {
        CHECK(c.state == ChamberState::Live);
    }
}

TEST_CASE("No round in the pouch means nothing loads") {
    RevolverMechanism m;
    EventList events;
    press(m, toggle(), 0.5f, events);
    MechanismInput empty;
    empty.load = true;
    run(m, empty, 1.0f, events);
    CHECK(count<RoundLoaded>(events) == 0);
}

TEST_CASE("Closing while a round is going in seats it first, then shuts") {
    RevolverMechanism m;
    EventList events;
    press(m, toggle(), 0.5f, events);
    m.tick(loading(), kDt, events);
    run(m, MechanismInput{}, m.tuning().insertTime * 0.5f, events);
    m.tick(toggle(), kDt, events);
    CHECK_FALSE(m.state().isClosed());
    CHECK(m.state().cylinder == CylinderPhase::Open);
    run(m, MechanismInput{}, m.tuning().insertTime * 0.5f + m.tuning().closeTime + 0.1f, events);

    CHECK(m.state().isClosed());
    CHECK(count<RoundLoaded>(events) == 1);
    const std::size_t before = events.size();
    run(m, pull(), 0.5f, events);
    CHECK(count<ShotFired>(EventList(events.begin() + static_cast<long>(before), events.end())) == 1);
}

TEST_CASE("With the gun closed and at rest, the wheel turns the cylinder") {
    RevolverMechanism m;
    EventList events;
    const int before = m.state().nextToFire();
    MechanismInput turn;
    turn.turn = 1;
    m.tick(turn, kDt, events);
    CHECK(m.state().isClosed());
    CHECK(m.state().nextToFire() == (before + 1) % kChamberCount);
    turn.turn = -1;
    m.tick(turn, kDt, events);
    CHECK(m.state().nextToFire() == before);

    MechanismInput pulling = pull();
    pulling.turn = 1;
    const int aligned = m.state().aligned;
    m.tick(pulling, kDt, events);
    CHECK(m.state().aligned == aligned);
}

TEST_CASE("Rounds fire in the order they were loaded") {
    RevolverMechanism m;
    EventList events;
    press(m, toggle(), 0.5f, events);
    MechanismInput turn;
    turn.turn = 3;
    press(m, turn, 0.2f, events);

    MechanismInput first = loading();
    first.roundToLoad = Round{1};
    m.tick(first, kDt, events);
    run(m, MechanismInput{}, m.tuning().insertTime + 0.05f, events);
    MechanismInput second = loading();
    second.roundToLoad = Round{2};
    m.tick(second, kDt, events);
    run(m, MechanismInput{}, m.tuning().insertTime + 0.05f, events);
    press(m, toggle(), 0.5f, events);
    REQUIRE(m.state().isClosed());

    std::vector<ElementId> fired;
    for (int i = 0; i < 3; ++i) {
        const std::size_t before = events.size();
        run(m, pull(), 0.5f, events);
        run(m, MechanismInput{}, 0.2f, events);
        for (std::size_t e = before; e < events.size(); ++e) {
            if (const auto* shot = std::get_if<ShotFired>(&events[e])) {
                fired.push_back(shot->round.element);
            }
        }
    }
    REQUIRE(fired.size() == 2);
    CHECK(fired[0] == 1);
    CHECK(fired[1] == 2);
}

TEST_CASE("A close request snaps the cylinder shut, reversing a swing-out and waiting out an eject") {
    MechanismInput close;
    close.closeCylinder = true;

    SUBCASE("while swinging out") {
        RevolverMechanism m;
        EventList events;
        press(m, toggle(), m.tuning().openTime * 0.5f, events);
        REQUIRE(m.state().cylinder == CylinderPhase::Opening);
        run(m, close, m.tuning().closeTime, events);
        CHECK(m.state().isClosed());
        CHECK(count<CylinderOpened>(events) == 0);
    }
    SUBCASE("mid-load: the round is seated first") {
        RevolverMechanism m;
        EventList events;
        press(m, toggle(), 0.5f, events);
        m.tick(loading(), kDt, events);
        run(m, close, m.tuning().insertTime + m.tuning().closeTime + 0.1f, events);
        CHECK(m.state().isClosed());
        CHECK(count<RoundLoaded>(events) == 1);
    }
    SUBCASE("mid-eject: finishes the stroke first") {
        RevolverMechanism m;
        m.loadAll(Round{});
        EventList events;
        press(m, toggle(), 0.5f, events);
        MechanismInput eject;
        eject.eject = true;
        m.tick(eject, kDt, events);
        run(m, close, m.tuning().ejectTime + m.tuning().closeTime + 0.05f, events);
        CHECK(count<ChambersEjected>(events) == 1);
        CHECK(m.state().isClosed());
    }
}

TEST_CASE("After each round the cylinder steps on, so the loading slot stays in the same place") {
    RevolverMechanism m;
    EventList events;
    press(m, toggle(), 0.5f, events);

    for (int i = 0; i < 3; ++i) {
        const int alignedBefore = m.state().aligned;
        const int slot = m.state().nextToFire();
        m.tick(loading(), kDt, events);
        run(m, MechanismInput{}, m.tuning().insertTime + 0.05f, events);

        CHECK(m.state().chambers[static_cast<std::size_t>(slot)].state == ChamberState::Live);
        CHECK(m.state().aligned == (alignedBefore + 1) % kChamberCount);

        CHECK(m.state().chambers[static_cast<std::size_t>(m.state().nextToFire())].state == ChamberState::Empty);
    }
}

TEST_CASE("Closing a partly loaded cylinder brings the first loaded round up next") {
    RevolverMechanism m;
    EventList events;
    press(m, toggle(), 0.5f, events);
    for (std::uint8_t type : {std::uint8_t{1}, std::uint8_t{2}}) {
        MechanismInput in = loading();
        in.roundToLoad = Round{type};
        m.tick(in, kDt, events);
        run(m, MechanismInput{}, m.tuning().insertTime + 0.05f, events);
    }
    press(m, toggle(), 0.6f, events);
    REQUIRE(m.state().isClosed());

    events.clear();
    CHECK(run(m, pull(), 0.5f, events) == 1);
    CHECK(count<DryFired>(events) == 0);
    REQUIRE(count<ShotFired>(events) == 1);
    for (const GameEvent& e : events) {
        if (const auto* shot = std::get_if<ShotFired>(&e)) {
            CHECK(shot->round.element == 1);
        }
    }
}

namespace {
MechanismInput speedload(std::initializer_list<int> elements) {
    SpeedloaderSlots slots{};
    std::size_t i = 0;
    for (const int e : elements) {
        if (e >= 0) {
            slots[i] = Round{static_cast<ElementId>(e)};
        }
        ++i;
    }
    MechanismInput input;
    input.speedload = slots;
    return input;
}

std::vector<ElementId> fireAll(RevolverMechanism& m, EventList& events, int pulls) {
    std::vector<ElementId> fired;
    for (int i = 0; i < pulls; ++i) {
        const std::size_t before = events.size();
        run(m, pull(), 0.5f, events);
        run(m, MechanismInput{}, 0.2f, events);
        for (std::size_t e = before; e < events.size(); ++e) {
            if (const auto* shot = std::get_if<ShotFired>(&events[e])) {
                fired.push_back(shot->round.element);
            }
        }
    }
    return fired;
}

}

TEST_CASE("A speedloader drops six rounds into an empty open cylinder, and they fire in its order") {
    RevolverMechanism m;
    EventList events;
    press(m, toggle(), 0.5f, events);
    m.tick(speedload({1, 2, 3, 4, 5, 6}), kDt, events);
    CHECK(m.state().speedloadProgress >= 0.0f);
    run(m, MechanismInput{}, m.tuning().speedloadTime * 0.5f, events);
    CHECK(count<SpeedloaderUsed>(events) == 0);
    run(m, MechanismInput{}, m.tuning().speedloadTime * 0.5f + 0.05f, events);
    REQUIRE(count<SpeedloaderUsed>(events) == 1);
    for (const Chamber& c : m.state().chambers) {
        CHECK(c.state == ChamberState::Live);
    }
    press(m, toggle(), 0.5f, events);
    REQUIRE(m.state().isClosed());
    const std::vector<ElementId> fired = fireAll(m, events, 6);
    REQUIRE(fired.size() == 6);
    for (int i = 0; i < 6; ++i) {
        CHECK(fired[static_cast<std::size_t>(i)] == static_cast<ElementId>(i + 1));
    }
}

TEST_CASE("A speedloader is refused unless the cylinder is open and every chamber is empty") {
    EventList events;

    RevolverMechanism closed;
    closed.tick(speedload({1, 1, 1, 1, 1, 1}), kDt, events);
    run(closed, MechanismInput{}, 1.5f, events);
    CHECK(count<SpeedloaderUsed>(events) == 0);

    RevolverMechanism partly;
    press(partly, toggle(), 0.5f, events);
    partly.tick(loading(), kDt, events);
    run(partly, MechanismInput{}, partly.tuning().insertTime + 0.1f, events);
    partly.tick(speedload({1, 1, 1, 1, 1, 1}), kDt, events);
    CHECK(partly.state().speedloadProgress < 0.0f);
    run(partly, MechanismInput{}, 1.5f, events);
    CHECK(count<SpeedloaderUsed>(events) == 0);

    RevolverMechanism empty;
    press(empty, toggle(), 0.5f, events);
    empty.tick(speedload({-1, -1, -1, -1, -1, -1}), kDt, events);
    CHECK(empty.state().speedloadProgress < 0.0f);
}

TEST_CASE("Empty slots in a speedloader leave those chambers empty, and closing waits for it to seat") {
    RevolverMechanism m;
    EventList events;
    press(m, toggle(), 0.5f, events);
    m.tick(speedload({1, -1, 3, -1, -1, -1}), kDt, events);
    m.tick(toggle(), kDt, events);
    CHECK_FALSE(m.state().isClosed());
    run(m, MechanismInput{}, m.tuning().speedloadTime + m.tuning().closeTime + 0.2f, events);
    REQUIRE(m.state().isClosed());
    int live = 0;
    for (const Chamber& c : m.state().chambers) {
        live += c.state == ChamberState::Live ? 1 : 0;
    }
    CHECK(live == 2);
    const std::vector<ElementId> fired = fireAll(m, events, 3);
    REQUIRE(fired.size() == 2);
    CHECK(fired[0] == 1);
    CHECK(fired[1] == 3);
}

TEST_CASE("The rack hands out its first filled speedloader and empties it when used") {
    SpeedloaderRack rack;
    CHECK(rack.nextFilled() == -1);
    rack.loaders[1].slots[0] = Round{2};
    CHECK(rack.nextFilled() == 1);
    rack.loaders[0].slots[3] = Round{};
    CHECK(rack.nextFilled() == 0);
    CHECK(rack.filled() == 2);
    rack.useNext();
    CHECK(rack.loaders[0].empty());
    CHECK(rack.nextFilled() == 1);
    rack.useNext();
    rack.useNext();
    CHECK(rack.nextFilled() == -1);
}

TEST_CASE("A quick load thumbs the rounds in fast, one after another, the same way as a normal load") {
    RevolverMechanism m;
    EventList events;
    press(m, toggle(), 0.5f, events);
    MechanismInput quick = loading();
    quick.quick = true;
    run(m, quick, m.tuning().quickInsertTime * 6.0f + 0.1f, events);
    CHECK(count<RoundLoaded>(events) == 6);
    CHECK(m.tuning().quickInsertTime < m.tuning().insertTime * 0.5f);
    for (const Chamber& c : m.state().chambers) {
        CHECK(c.state == ChamberState::Live);
    }
}

TEST_CASE("Opening a cylinder of only spent cases dumps them by itself; a live round or an empty cylinder does not") {
    {
        RevolverMechanism m;
        m.loadAll(Round{});
        EventList events;
        for (int shot = 0; shot < kChamberCount; ++shot) {
            run(m, pull(), 0.5f, events);
            run(m, MechanismInput{}, 0.2f, events);
        }
        press(m, toggle(), m.tuning().openTime + m.tuning().ejectTime + 0.1f, events);
        CHECK(count<ChambersEjected>(events) == 1);
        for (const Chamber& c : m.state().chambers) {
            CHECK(c.state == ChamberState::Empty);
        }
    }
    {
        RevolverMechanism m;
        m.loadAll(Round{});
        EventList events;
        for (int shot = 0; shot < kChamberCount - 1; ++shot) {
            run(m, pull(), 0.5f, events);
            run(m, MechanismInput{}, 0.2f, events);
        }
        press(m, toggle(), m.tuning().openTime + m.tuning().ejectTime + 0.1f, events);
        CHECK(count<ChambersEjected>(events) == 0);
    }
    {
        RevolverMechanism m;
        EventList events;
        press(m, toggle(), m.tuning().openTime + m.tuning().ejectTime + 0.1f, events);
        CHECK(count<ChambersEjected>(events) == 0);
    }
}

TEST_CASE("Only the shot that empties the gun is marked as the last") {
    RevolverMechanism m;
    m.loadAll(Round{});
    EventList events;
    for (int shot = 0; shot < kChamberCount; ++shot) {
        run(m, pull(), m.tuning().doubleActionPullTime * 1.5f, events);
        run(m, MechanismInput{}, 0.3f, events);
    }
    std::vector<bool> last;
    for (const GameEvent& e : events) {
        if (const auto* fired = std::get_if<ShotFired>(&e)) {
            last.push_back(fired->last);
        }
    }
    REQUIRE(last.size() == static_cast<std::size_t>(kChamberCount));
    for (std::size_t i = 0; i + 1 < last.size(); ++i) {
        CHECK_FALSE(last[i]);
    }
    CHECK(last.back());
}
