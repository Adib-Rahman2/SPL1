#include "TestFramework.h"

#include "analysis/Complexity.h"

#include <string>

using codecomplex::Complexity;

TEST_CASE("complexity: notation of every class") {
    CHECK_EQ(Complexity::constant().notation(), std::string("O(1)"));
    CHECK_EQ(Complexity::logarithmic().notation(), std::string("O(log n)"));
    CHECK_EQ(Complexity::linear().notation(), std::string("O(n)"));
    CHECK_EQ(Complexity::linearithmic().notation(), std::string("O(n log n)"));
    CHECK_EQ(Complexity::polynomial(2).notation(), std::string("O(n^2)"));
    CHECK_EQ(Complexity::polynomial(3).notation(), std::string("O(n^3)"));
    CHECK_EQ(Complexity::polynomial(2, 1).notation(), std::string("O(n^2 log n)"));
    CHECK_EQ(Complexity::polynomial(0, 2).notation(), std::string("O(log^2 n)"));
    CHECK_EQ(Complexity::exponential().notation(), std::string("O(2^n)"));
    CHECK_EQ(Complexity::exponential(3).notation(), std::string("O(3^n)"));
    CHECK_EQ(Complexity::factorial().notation(), std::string("O(n!)"));
    CHECK_EQ(Complexity::unknown().notation(), std::string("Unknown"));
}

TEST_CASE("complexity: toString adds the flags") {
    Complexity c = Complexity::exponential();
    c.upperBound = true;
    CHECK_EQ(c.toString(), std::string("O(2^n) (upper bound)"));
    c.uncertain = true;
    CHECK_EQ(c.toString(), std::string("O(2^n) (upper bound) (uncertain)"));
    CHECK_EQ(Complexity::linear().toString(), std::string("O(n)"));
}

TEST_CASE("complexity: growth order from O(1) to O(n!)") {
    const Complexity order[] = {
        Complexity::constant(), Complexity::logarithmic(), Complexity::polynomial(0, 2),
        Complexity::linear(), Complexity::linearithmic(), Complexity::polynomial(2),
        Complexity::polynomial(3), Complexity::exponential(2), Complexity::exponential(3),
        Complexity::factorial()};
    const int count = sizeof(order) / sizeof(order[0]);
    for (int i = 0; i + 1 < count; ++i) {
        CHECK_EQ(codecomplex::compareGrowth(order[i], order[i + 1]), -1);
        CHECK_EQ(codecomplex::compareGrowth(order[i + 1], order[i]), 1);
        CHECK_EQ(codecomplex::compareGrowth(order[i], order[i]), 0);
    }
}

TEST_CASE("complexity: multiply polynomials (nesting)") {
    using codecomplex::multiply;
    CHECK_EQ(multiply(Complexity::linear(), Complexity::linear()), Complexity::polynomial(2));
    CHECK_EQ(multiply(Complexity::polynomial(2), Complexity::linear()), Complexity::polynomial(3));
    CHECK_EQ(multiply(Complexity::linear(), Complexity::logarithmic()), Complexity::linearithmic());
    CHECK_EQ(multiply(Complexity::logarithmic(), Complexity::logarithmic()),
             Complexity::polynomial(0, 2));
    CHECK_EQ(multiply(Complexity::constant(), Complexity::linear()), Complexity::linear());
    CHECK_EQ(multiply(Complexity::factorial(), Complexity::constant()), Complexity::factorial());
}

TEST_CASE("complexity: multiply exponentials and factorials") {
    using codecomplex::multiply;
    CHECK_EQ(multiply(Complexity::exponential(2), Complexity::exponential(3)),
             Complexity::exponential(6));
    const Complexity c = multiply(Complexity::linear(), Complexity::exponential());
    CHECK_EQ(c.notation(), std::string("O(2^n)"));
    CHECK(c.uncertain);
    const Complexity f = multiply(Complexity::factorial(), Complexity::exponential());
    CHECK_EQ(f.notation(), std::string("O(n!)"));
    CHECK(f.uncertain);
}

TEST_CASE("complexity: Unknown absorbs multiply and add") {
    CHECK(codecomplex::multiply(Complexity::unknown(), Complexity::constant()).isUnknown());
    CHECK(codecomplex::multiply(Complexity::linear(), Complexity::unknown()).isUnknown());
    CHECK(codecomplex::add(Complexity::polynomial(2), Complexity::unknown()).isUnknown());
    CHECK(codecomplex::add(Complexity::unknown(), Complexity::constant()).isUnknown());
}

TEST_CASE("complexity: add keeps the largest term (sequence)") {
    using codecomplex::add;
    CHECK_EQ(add(Complexity::linear(), Complexity::linear()), Complexity::linear());
    CHECK_EQ(add(Complexity::linear(), Complexity::polynomial(2)), Complexity::polynomial(2));
    CHECK_EQ(add(Complexity::linearithmic(), Complexity::linear()), Complexity::linearithmic());
    CHECK_EQ(add(Complexity::constant(), Complexity::logarithmic()), Complexity::logarithmic());
    CHECK_EQ(add(Complexity::factorial(), Complexity::exponential()), Complexity::factorial());
}

TEST_CASE("complexity: flags through multiply and add") {
    using codecomplex::add;
    using codecomplex::multiply;
    Complexity bound = Complexity::exponential();
    bound.upperBound = true;
    Complexity unsure = Complexity::linear();
    unsure.uncertain = true;

    CHECK(multiply(bound, Complexity::constant()).upperBound);
    CHECK(multiply(unsure, Complexity::linear()).uncertain);

    CHECK(add(bound, Complexity::linear()).upperBound);
    Complexity smallBound = Complexity::linear();
    smallBound.upperBound = true;
    CHECK(!add(smallBound, Complexity::polynomial(2)).upperBound);
    CHECK(!add(smallBound, Complexity::linear()).upperBound);

    CHECK(add(Complexity::polynomial(2), unsure).uncertain);
}

TEST_CASE("complexity: equality includes the flags") {
    Complexity a = Complexity::linear();
    Complexity b = Complexity::linear();
    CHECK(a == b);
    b.uncertain = true;
    CHECK(a != b);
    CHECK(Complexity::linear() != Complexity::logarithmic());
}
