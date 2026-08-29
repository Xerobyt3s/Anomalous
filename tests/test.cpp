#include "test.h"

#include <cstdio>
#include <cstring>

namespace test {
namespace {

constexpr int kMaxCases = 1024;

struct Registry {
    Case cases[kMaxCases];
    int count = 0;
    int overflow = 0;
};

// Function-local static: guarantees the registry is constructed before the first
// Registrar touches it, whatever order the translation units initialise in.
Registry& registry()
{
    static Registry r;
    return r;
}

int g_current_failures = 0;

bool matches(const Case& c, const char* filter)
{
    if (!filter || !*filter) {
        return true;
    }
    return std::strstr(c.suite, filter) != nullptr || std::strstr(c.name, filter) != nullptr;
}

} // namespace

bool as_bool(bool value)
{
    return value;
}

void register_case(const Case& c)
{
    Registry& r = registry();
    if (r.count >= kMaxCases) {
        r.overflow++;
        return;
    }
    r.cases[r.count++] = c;
}

void report_failure(const char* file, int line, const char* expr, const char* detail)
{
    g_current_failures++;
    if (detail) {
        std::printf("    FAIL %s:%d\n      %s\n      %s\n", file, line, expr, detail);
    } else {
        std::printf("    FAIL %s:%d\n      %s\n", file, line, expr);
    }
}

int run_all(int argc, char** argv)
{
    const char* filter = nullptr;
    bool list_only = false;

    for (int i = 1; i < argc; i++) {
        if (std::strncmp(argv[i], "--filter=", 9) == 0) {
            filter = argv[i] + 9;
        } else if (std::strcmp(argv[i], "--list") == 0) {
            list_only = true;
        }
    }

    Registry& r = registry();
    if (r.overflow > 0) {
        std::printf("error: test registry overflow, %d case(s) dropped (kMaxCases=%d)\n",
                    r.overflow, kMaxCases);
        return 2;
    }

    if (list_only) {
        for (int i = 0; i < r.count; i++) {
            std::printf("%s.%s\n", r.cases[i].suite, r.cases[i].name);
        }
        return 0;
    }

    int ran = 0;
    int failed = 0;

    for (int i = 0; i < r.count; i++) {
        const Case& c = r.cases[i];
        if (!matches(c, filter)) {
            continue;
        }
        ran++;
        g_current_failures = 0;
        std::printf("  %s.%s\n", c.suite, c.name);
        c.fn();
        if (g_current_failures > 0) {
            failed++;
        }
    }

    std::printf("\n%d case(s) run, %d passed, %d failed\n", ran, ran - failed, failed);

    if (ran == 0 && filter) {
        std::printf("error: filter '%s' matched no cases\n", filter);
        return 2;
    }

    return failed == 0 ? 0 : 1;
}

} // namespace test

int main(int argc, char** argv)
{
    return test::run_all(argc, argv);
}
