#pragma once

// Minimal test harness.
//
// Deliberately dependency-free: the tests link anomalous_core and nothing else, so the
// suite stays runnable headless (no window, no GL context) and in CI. There are no
// exceptions, so a failed CHECK records the failure and returns from the test body --
// which is why every CHECK must sit directly in the test function, not in a helper that
// returns void and swallows the early exit.

#include <cmath>

namespace test {

using TestFn = void (*)();

struct Case {
    const char* suite;
    const char* name;
    TestFn fn;
    const char* file;
    int line;
};

void register_case(const Case& c);
void report_failure(const char* file, int line, const char* expr, const char* detail);
int run_all(int argc, char** argv);

struct Registrar {
    explicit Registrar(const Case& c) { register_case(c); }
};

// Deliberately not constexpr, and deliberately not inline-folded away at the source
// level: routing every CHECK condition through a call keeps the `if` from being a
// constant expression, which is what /W4 flags as C4127. The alternative is disabling
// C4127 for the whole project, and it is worth more than that in real code.
bool as_bool(bool value);

inline bool nearly_equal(double a, double b, double eps)
{
    const double diff = std::fabs(a - b);
    if (diff <= eps) {
        return true;
    }
    // Relative comparison for large magnitudes, so tolerances stay meaningful when the
    // physics baselines start producing world-space coordinates in the hundreds.
    const double scale = std::fmax(std::fabs(a), std::fabs(b));
    return diff <= eps * scale;
}

} // namespace test

#define TEST_CAT_(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT_(a, b)

#define TEST(suite_, name_)                                                     \
    static void TEST_CAT(suite_, TEST_CAT(_, name_))();                         \
    static const ::test::Registrar TEST_CAT(test_registrar_, __LINE__){         \
        ::test::Case{#suite_, #name_, &TEST_CAT(suite_, TEST_CAT(_, name_)),    \
                     __FILE__, __LINE__}};                                      \
    static void TEST_CAT(suite_, TEST_CAT(_, name_))()

#define CHECK(expr)                                                             \
    do {                                                                        \
        if (!::test::as_bool(!!(expr))) {                                       \
            ::test::report_failure(__FILE__, __LINE__, #expr, nullptr);          \
            return;                                                             \
        }                                                                       \
    } while (0)

#define CHECK_MSG(expr, detail)                                                 \
    do {                                                                        \
        if (!::test::as_bool(!!(expr))) {                                       \
            ::test::report_failure(__FILE__, __LINE__, #expr, (detail));         \
            return;                                                             \
        }                                                                       \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                   \
    do {                                                                        \
        if (!::test::nearly_equal((a), (b), (eps))) {                           \
            ::test::report_failure(__FILE__, __LINE__, #a " ~= " #b, nullptr);    \
            return;                                                             \
        }                                                                       \
    } while (0)

#define FAIL(detail)                                                            \
    do {                                                                        \
        ::test::report_failure(__FILE__, __LINE__, "FAIL", (detail));            \
        return;                                                                 \
    } while (0)
