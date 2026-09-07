#pragma once
// Micro test framework (header-only, no deps).
#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace rcai::test {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

struct Failure {
    std::string what;
};

struct Context {
    std::vector<std::string> failures;
};

inline int runAll(const std::string& filter = "") {
    int failed = 0;
    for (const auto& c : registry()) {
        if (!filter.empty() && c.name.find(filter) == std::string::npos) continue;
        try {
            c.fn();
        } catch (const Failure& f) {
            ++failed;
            std::printf("FAIL  %s\n      %s\n", c.name.c_str(), f.what.c_str());
            continue;
        } catch (const std::exception& e) {
            ++failed;
            std::printf("FAIL  %s\n      exception: %s\n", c.name.c_str(), e.what());
            continue;
        }
        std::printf("ok    %s\n", c.name.c_str());
    }
    return failed;
}

} // namespace rcai::test

#define RCAI_TEST(name)                                                         \
    static void rcai_test_##name();                                             \
    static ::rcai::test::Registrar rcai_reg_##name(#name, rcai_test_##name);    \
    static void rcai_test_##name()

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond))                                                            \
            throw ::rcai::test::Failure{__FILE__ ":" + std::to_string(__LINE__) +\
                                         "  CHECK(" #cond ") failed"};          \
    } while (0)

#define CHECK_EQ(a, b)                                                          \
    do {                                                                        \
        const auto va = (a);                                                    \
        const auto vb = (b);                                                    \
        if (!(va == vb)) {                                                      \
            std::ostringstream oss;                                             \
            oss << __FILE__ << ":" << __LINE__ << "  CHECK_EQ(" #a ", " #b     \
                << ") failed: " << va << " != " << vb;                          \
            throw ::rcai::test::Failure{oss.str()};                             \
        }                                                                       \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                   \
    do {                                                                        \
        const double va = (a);                                                  \
        const double vb = (b);                                                  \
        if (std::fabs(va - vb) > (eps)) {                                       \
            std::ostringstream oss;                                             \
            oss << __FILE__ << ":" << __LINE__ << "  CHECK_NEAR(" #a ", " #b    \
                << ", " #eps ") failed: " << va << " vs " << vb;                \
            throw ::rcai::test::Failure{oss.str()};                             \
        }                                                                       \
    } while (0)
