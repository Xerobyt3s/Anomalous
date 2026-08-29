#pragma once

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

bool as_bool(bool value);

struct Registrar {
    explicit Registrar(const Case& c) { register_case(c); }
};

inline bool nearly_equal(double a, double b, double eps)
{
    const double diff = std::fabs(a - b);
    if (diff <= eps) {
        return true;
    }
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
        if (!::test::as_bool(::test::nearly_equal((a), (b), (eps)))) {          \
            ::test::report_failure(__FILE__, __LINE__, #a " ~= " #b, nullptr);    \
            return;                                                             \
        }                                                                       \
    } while (0)

#define FAIL(detail)                                                            \
    do {                                                                        \
        ::test::report_failure(__FILE__, __LINE__, "FAIL", (detail));            \
        return;                                                                 \
    } while (0)
