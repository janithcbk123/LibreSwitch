// =====================================================================
//  test/host/test_framework.h — tiny zero-dependency unit-test harness
// ---------------------------------------------------------------------
//  No external deps (works in any g++/CI). Enough for Phase 10 L1.
//  Usage: TEST(name){ ... CHECK(cond); EQ(a,b); } then RUN_ALL() in main.
// =====================================================================
#pragma once

#include <cstdio>
#include <vector>
#include <string>
#include <functional>

namespace tf {

struct Case { std::string name; std::function<void(bool&)> fn; };

inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }

struct Reg { Reg(const char* n, std::function<void(bool&)> f) { registry().push_back({n, f}); } };

inline int run_all() {
    int passed = 0, failed = 0;
    for (auto& c : registry()) {
        bool ok = true;
        c.fn(ok);
        if (ok) { ++passed; printf("  PASS  %s\n", c.name.c_str()); }
        else    { ++failed; printf("  FAIL  %s\n", c.name.c_str()); }
    }
    printf("\n%d passed, %d failed, %zu total\n", passed, failed, registry().size());
    return failed == 0 ? 0 : 1;
}

}  // namespace tf

#define TEST(NAME)                                                          \
    static void NAME(bool& _ok);                                            \
    static tf::Reg _reg_##NAME(#NAME, NAME);                                \
    static void NAME(bool& _ok)

#define CHECK(COND)                                                         \
    do { if (!(COND)) { _ok = false;                                        \
        printf("    CHECK failed: %s  (%s:%d)\n", #COND, __FILE__, __LINE__); } } while (0)

#define EQ(A, B)                                                            \
    do { if (!((A) == (B))) { _ok = false;                                  \
        printf("    EQ failed: %s == %s  (%s:%d)\n", #A, #B, __FILE__, __LINE__); } } while (0)
