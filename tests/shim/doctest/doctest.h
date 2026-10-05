#pragma once

#include "test.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace doctest {

struct Approx {
    explicit Approx(double v) : value(v) {}

    double value;
    double epsilon = static_cast<double>(std::numeric_limits<float>::epsilon()) * 100.0;
};

inline bool operator==(double lhs, const Approx& rhs)
{
    return std::fabs(lhs - rhs.value) < rhs.epsilon * (1.0 + std::max(std::fabs(lhs), std::fabs(rhs.value)));
}

inline bool operator==(const Approx& lhs, double rhs) { return rhs == lhs; }
inline bool operator!=(double lhs, const Approx& rhs) { return !(lhs == rhs); }
inline bool operator!=(const Approx& lhs, double rhs) { return !(rhs == lhs); }

} // namespace doctest

#define DOCTEST_SHIM_FN(line) TEST_CAT(ghost_case_, line)
#define DOCTEST_SHIM_REG(line) TEST_CAT(ghost_registrar_, line)

#define TEST_CASE(description)                                                              \
    static void DOCTEST_SHIM_FN(__LINE__)();                                                \
    static const ::test::Registrar DOCTEST_SHIM_REG(__LINE__){                              \
        ::test::Case{"ghost", description, &DOCTEST_SHIM_FN(__LINE__), __FILE__, __LINE__}}; \
    static void DOCTEST_SHIM_FN(__LINE__)()

#define REQUIRE(expr) CHECK(expr)
#define CHECK_FALSE(expr) CHECK(!(expr))
#define REQUIRE_FALSE(expr) CHECK(!(expr))
