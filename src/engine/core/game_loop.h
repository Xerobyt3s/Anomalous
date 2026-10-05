#pragma once

namespace ghost::engine {
class FixedStepClock {
public:
    explicit FixedStepClock(double tickRate = 120.0, double maxFrameTime = 0.25);

    int advance(double frameSeconds);

    double tickDt() const { return m_tickDt; }

    double alpha() const { return m_accumulator / m_tickDt; }

private:
    double m_tickDt;
    double m_maxFrameTime;
    double m_accumulator = 0.0;
};

}
