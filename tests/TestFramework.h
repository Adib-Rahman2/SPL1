#pragma once

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace testfw {

struct TestCase {
    std::string name;
    std::function<void()> body;
};

struct RequireFailed {};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& currentFailures() {
    static int failures = 0;
    return failures;
}

inline bool& silentFailures() {
    static bool silent = false;
    return silent;
}

inline void reportFailure(const char* file, int line, const std::string& message) {
    ++currentFailures();
    if (silentFailures()) return;
    std::cerr << "  FAILED " << file << ':' << line << ": " << message << '\n';
}

struct Registrar {
    Registrar(const char* name, std::function<void()> body) {
        registry().push_back({name, std::move(body)});
    }
};

template <typename A, typename B>
std::string describeMismatch(const char* actualExpr, const char* expectedExpr,
                             const A& actual, const B& expected) {
    std::ostringstream out;
    out << actualExpr << " == " << expectedExpr
        << "  (got: " << actual << ", expected: " << expected << ')';
    return out.str();
}

inline int runAllTests() {
    int failedTests = 0;
    for (const TestCase& test : registry()) {
        currentFailures() = 0;
        try {
            test.body();
        } catch (const RequireFailed&) {
        } catch (const std::exception& e) {
            reportFailure(__FILE__, __LINE__, std::string("unexpected exception: ") + e.what());
        } catch (...) {
            reportFailure(__FILE__, __LINE__, "unexpected non-standard exception");
        }

        if (currentFailures() == 0) {
            std::cout << "[PASS] " << test.name << '\n';
        } else {
            std::cout << "[FAIL] " << test.name << '\n';
            ++failedTests;
        }
    }
    std::cout << '\n' << (registry().size() - failedTests) << '/' << registry().size()
              << " tests passed\n";
    return failedTests;
}

}

#define TESTFW_CONCAT_INNER(a, b) a##b
#define TESTFW_CONCAT(a, b) TESTFW_CONCAT_INNER(a, b)
#define TESTFW_UNIQUE(prefix) TESTFW_CONCAT(prefix, __LINE__)

#define TEST_CASE(name)                                                        \
    static void TESTFW_UNIQUE(testfw_body_)();                                 \
    static const testfw::Registrar TESTFW_UNIQUE(testfw_reg_)(                 \
        name, &TESTFW_UNIQUE(testfw_body_));                                   \
    static void TESTFW_UNIQUE(testfw_body_)()

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) testfw::reportFailure(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_EQ(actual, expected)                                             \
    do {                                                                       \
        const auto& testfw_a = (actual);                                       \
        const auto& testfw_e = (expected);                                     \
        if (!(testfw_a == testfw_e))                                           \
            testfw::reportFailure(__FILE__, __LINE__,                          \
                testfw::describeMismatch(#actual, #expected, testfw_a, testfw_e)); \
    } while (0)

#define REQUIRE(cond)                                                          \
    do {                                                                       \
        if (!(cond)) {                                                         \
            testfw::reportFailure(__FILE__, __LINE__, "REQUIRE(" #cond ")");   \
            throw testfw::RequireFailed{};                                     \
        }                                                                      \
    } while (0)
