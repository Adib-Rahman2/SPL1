#include "TestFramework.h"

#include <string>

namespace {

template <typename F>
int failuresRecordedBy(F body) {
    const int saved = testfw::currentFailures();
    testfw::currentFailures() = 0;
    testfw::silentFailures() = true;
    try {
        body();
    } catch (const testfw::RequireFailed&) {
    }
    testfw::silentFailures() = false;
    const int recorded = testfw::currentFailures();
    testfw::currentFailures() = saved;
    return recorded;
}

}

TEST_CASE("harness: passing checks record no failures") {
    const int recorded = failuresRecordedBy([] {
        CHECK(1 + 1 == 2);
        CHECK_EQ(std::string("abc"), std::string("abc"));
        REQUIRE(true);
    });
    CHECK_EQ(recorded, 0);
}

TEST_CASE("harness: failing CHECK is recorded and execution continues") {
    bool reachedEnd = false;
    const int recorded = failuresRecordedBy([&] {
        CHECK(false);
        CHECK_EQ(2, 3);
        reachedEnd = true;
    });
    CHECK_EQ(recorded, 2);
    CHECK(reachedEnd);
}

TEST_CASE("harness: failing REQUIRE stops the test") {
    bool reachedEnd = false;
    const int recorded = failuresRecordedBy([&] {
        REQUIRE(false);
        reachedEnd = true;
    });
    CHECK_EQ(recorded, 1);
    CHECK(!reachedEnd);
}
