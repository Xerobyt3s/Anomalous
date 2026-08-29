#include "platform/clock.h"

#include <windows.h>

namespace anom {
namespace {

struct Timebase {
    i64 frequency;
    i64 origin;

    Timebase()
    {
        LARGE_INTEGER f;
        LARGE_INTEGER t;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&t);
        frequency = f.QuadPart;
        origin = t.QuadPart;
    }
};

const Timebase& timebase()
{
    static Timebase base;
    return base;
}

} // namespace

f64 time_seconds()
{
    const Timebase& base = timebase();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<f64>(now.QuadPart - base.origin) / static_cast<f64>(base.frequency);
}

} // namespace anom
