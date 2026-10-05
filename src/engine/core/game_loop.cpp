#include "engine/core/game_loop.h"

#include <algorithm>

namespace ghost::engine {
FixedStepClock::FixedStepClock(double tickRate, double maxFrameTime)
    : m_tickDt(1.0 / tickRate), m_maxFrameTime(maxFrameTime) {}

int FixedStepClock::advance(double frameSeconds) {
    m_accumulator += std::clamp(frameSeconds, 0.0, m_maxFrameTime);
    int ticks = 0;
    while (m_accumulator >= m_tickDt) {
        m_accumulator -= m_tickDt;
        ++ticks;
    }
    return ticks;
}

}
