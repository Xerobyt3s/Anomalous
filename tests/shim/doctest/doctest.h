#pragma once

#include "test.h"

#if defined(_MSC_VER)
#pragma warning(disable : 4702)
#endif

#include <algorithm>
#include <exception>
#include <cmath>
#include <limits>

namespace doctest {

struct Approx {
    explicit Approx(double v) : value(v) {}

    Approx& epsilon(double e)
    {
        tolerance = e;
        return *this;
    }

    double value;
    double tolerance = static_cast<double>(std::numeric_limits<float>::epsilon()) * 100.0;
};

inline bool operator==(double lhs, const Approx& rhs)
{
    return std::fabs(lhs - rhs.value) < rhs.tolerance * (1.0 + std::max(std::fabs(lhs), std::fabs(rhs.value)));
}

inline bool operator==(const Approx& lhs, double rhs) { return rhs == lhs; }
inline bool operator!=(double lhs, const Approx& rhs) { return !(lhs == rhs); }
inline bool operator!=(const Approx& lhs, double rhs) { return !(rhs == lhs); }

struct RequireAbort {
};

} // namespace doctest

#undef CHECK
#undef FAIL

#define DOCTEST_SHIM_FN(line) TEST_CAT(ghost_case_, line)
#define DOCTEST_SHIM_RUN(line) TEST_CAT(ghost_run_, line)
#define DOCTEST_SHIM_REG(line) TEST_CAT(ghost_registrar_, line)

#define TEST_CASE(description)                                                                  static void DOCTEST_SHIM_FN(__LINE__)();                                                    static void DOCTEST_SHIM_RUN(__LINE__)()                                                    {                                                                                               try {                                                                                           DOCTEST_SHIM_FN(__LINE__)();                                                            } catch (const ::doctest::RequireAbort&) {                                                  } catch (const std::exception& e) {                                                             ::test::report_failure(__FILE__, __LINE__, "unexpected exception", e.what());           }                                                                                       }                                                                                           static const ::test::Registrar DOCTEST_SHIM_REG(__LINE__){                                      ::test::Case{"ghost", description, &DOCTEST_SHIM_RUN(__LINE__), __FILE__, __LINE__}};     static void DOCTEST_SHIM_FN(__LINE__)()

#define CHECK(...)                                                                              do {                                                                                            if (!::test::as_bool(!!(__VA_ARGS__))) {                                                        ::test::report_failure(__FILE__, __LINE__, #__VA_ARGS__, nullptr);                      }                                                                                       } while (0)

#define REQUIRE(...)                                                                            do {                                                                                            if (!::test::as_bool(!!(__VA_ARGS__))) {                                                        ::test::report_failure(__FILE__, __LINE__, #__VA_ARGS__, nullptr);                          throw ::doctest::RequireAbort{};                                                        }                                                                                       } while (0)

#define FAIL(message)                                                                           do {                                                                                            ::test::report_failure(__FILE__, __LINE__, "FAIL", message);                                throw ::doctest::RequireAbort{};                                                        } while (0)

#define CHECK_FALSE(...) CHECK(!(__VA_ARGS__))
#define REQUIRE_FALSE(...) REQUIRE(!(__VA_ARGS__))
#define SUBCASE(name) if (true)
#define INFO(...) ((void)0)
#define CAPTURE(...) ((void)0)

#define CHECK_THROWS(...)                                                                       do {                                                                                            bool doctest_shim_thrown = false;                                                           try {                                                                                           (void)(__VA_ARGS__);                                                                    } catch (...) {                                                                                 doctest_shim_thrown = true;                                                             }                                                                                           CHECK(doctest_shim_thrown);                                                             } while (0)
