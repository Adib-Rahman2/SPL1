#include "TestFramework.h"

#include "features/FeatureExtractor.h"
#include "lexer/Lexer.h"
#include "structure/Parser.h"

#include <string>
#include <vector>

using codecomplex::BoundKind;
using codecomplex::FeatureVector;
using codecomplex::Function;
using codecomplex::LoopFacts;
using codecomplex::Node;
using codecomplex::NodeKind;
using codecomplex::StepKind;
using codecomplex::Token;

namespace {

struct Parsed {
    std::vector<Token> tokens;
    Function function;
};

Parsed parseOne(const std::string& source) {
    const codecomplex::LexResult lexed = codecomplex::tokenize(source);
    REQUIRE(lexed.ok());
    const codecomplex::ParseResult parsed = codecomplex::parse(lexed.tokens);
    REQUIRE(parsed.functions.size() == 1);
    return {lexed.tokens, parsed.functions[0]};
}

const Node* firstLoop(const Node& node) {
    if (node.kind == NodeKind::Loop) return &node;
    for (const Node& child : node.children) {
        if (const Node* loop = firstLoop(child)) return loop;
    }
    return nullptr;
}

LoopFacts loopFacts(const std::string& params, const std::string& body) {
    const Parsed p = parseOne("void f(" + params + ") {\n" + body + "\n}\n");
    const Node* loop = firstLoop(p.function.body);
    REQUIRE(loop != nullptr);
    return codecomplex::analyzeLoop(*loop, codecomplex::inputNames(p.function));
}

FeatureVector features(const std::string& source) {
    const Parsed p = parseOne(source);
    return codecomplex::extractFeatures(p.function, p.tokens);
}

}

TEST_CASE("loop facts: for loop with i++ up to a parameter") {
    const LoopFacts f = loopFacts("int n", "for (int i = 0; i < n; i++) {}");
    CHECK_EQ(f.variable, std::string("i"));
    CHECK_EQ(f.step, StepKind::Additive);
    CHECK_EQ(f.stepText, std::string("i++"));
    CHECK_EQ(f.bound, BoundKind::Input);
    CHECK_EQ(f.start, std::string("0"));
    CHECK_EQ(f.limit, std::string("n"));
    CHECK_EQ(f.line, 2);
    CHECK(!f.modifiedInBody);
}

TEST_CASE("loop facts: additive step forms") {
    CHECK_EQ(loopFacts("int n", "for (int i = 0; i < n; ++i) {}").step, StepKind::Additive);
    CHECK_EQ(loopFacts("int n", "for (int i = n; i > 0; i--) {}").step, StepKind::Additive);
    CHECK_EQ(loopFacts("int n", "for (int i = 0; i < n; i += 2) {}").step, StepKind::Additive);
    CHECK_EQ(loopFacts("int n", "for (int i = n; i >= 0; i -= 3) {}").step, StepKind::Additive);
    CHECK_EQ(loopFacts("int n", "for (int i = 0; i < n; i = i + 1) {}").step, StepKind::Additive);
    CHECK_EQ(loopFacts("int n", "for (int i = 0; i < n; i = 1 + i) {}").step, StepKind::Additive);
}

TEST_CASE("loop facts: multiplicative step forms") {
    CHECK_EQ(loopFacts("int n", "for (int i = 1; i < n; i *= 2) {}").step, StepKind::Multiplicative);
    CHECK_EQ(loopFacts("int n", "for (int i = n; i > 1; i /= 2) {}").step, StepKind::Multiplicative);
    CHECK_EQ(loopFacts("int n", "for (int i = 1; i < n; i <<= 1) {}").step, StepKind::Multiplicative);
    CHECK_EQ(loopFacts("int n", "for (int i = n; i > 0; i >>= 1) {}").step, StepKind::Multiplicative);
    CHECK_EQ(loopFacts("int n", "for (int i = 1; i < n; i = i * 3) {}").step, StepKind::Multiplicative);
    CHECK_EQ(loopFacts("int n", "for (int i = 1; i < n; i = 2 * i) {}").step, StepKind::Multiplicative);
}

TEST_CASE("loop facts: steps that are not a constant add or multiply") {
    CHECK_EQ(loopFacts("int n, int k", "for (int i = 0; i < n; i += k) {}").step, StepKind::Other);
    CHECK_EQ(loopFacts("int n", "for (int i = 1; i < n; i *= 1) {}").step, StepKind::Other);
    CHECK_EQ(loopFacts("int n", "for (int i = 0; i < n; i = next(i)) {}").step, StepKind::Other);
    CHECK_EQ(loopFacts("int n", "for (int i = 0; i < n; i += 0x2) {}").step, StepKind::Other);
}

TEST_CASE("loop facts: bound kinds") {
    CHECK_EQ(loopFacts("", "for (int i = 0; i < 100; i++) {}").bound, BoundKind::Constant);
    CHECK_EQ(loopFacts("", "for (int i = 0; i < n; i++) {}").bound, BoundKind::Input);
    CHECK_EQ(loopFacts("std::vector<int>& v", "for (int i = 0; i < v.size(); i++) {}").bound,
             BoundKind::Input);
    CHECK_EQ(loopFacts("const std::string& s", "for (int i = 0; i < s.length(); i++) {}").bound,
             BoundKind::Input);
    CHECK_EQ(loopFacts("const char* s", "for (int i = 0; i < strlen(t); i++) {}").bound,
             BoundKind::Input);
    CHECK_EQ(loopFacts("int len", "for (int i = len; i > 0; i--) {}").bound, BoundKind::Input);
    CHECK_EQ(loopFacts("", "int m = 5; for (int i = 0; i < m; i++) {}").bound, BoundKind::Variable);
    CHECK_EQ(loopFacts("int n", "for (int j = i; j < i + 5; j++) {}").bound, BoundKind::Variable);
    CHECK_EQ(loopFacts("int n", "for (int i = 1; i * i <= n; i++) {}").bound, BoundKind::Unknown);
    CHECK_EQ(loopFacts("int n", "for (int i = 0; i < n || i < 5; i++) {}").bound, BoundKind::Unknown);
    CHECK_EQ(loopFacts("int n", "for (int i = 0; ; i++) {}").bound, BoundKind::Unknown);
}

TEST_CASE("loop facts: && keeps the comparison with the loop variable") {
    const LoopFacts f = loopFacts("int n, bool ok", "for (int i = 0; ok && i < n; i++) {}");
    CHECK_EQ(f.bound, BoundKind::Input);
    CHECK_EQ(f.limit, std::string("n"));
}

TEST_CASE("loop facts: for loop variable changed in the body") {
    CHECK(loopFacts("int n", "for (int i = 0; i < n; i++) { if (i == 3) i = 7; }").modifiedInBody);
}

TEST_CASE("loop facts: while loops take the step from the body") {
    LoopFacts f = loopFacts("int n", "while (n > 1) { n /= 2; }");
    CHECK_EQ(f.variable, std::string("n"));
    CHECK_EQ(f.step, StepKind::Multiplicative);
    CHECK_EQ(f.stepText, std::string("n /= 2"));
    CHECK_EQ(f.bound, BoundKind::Input);

    f = loopFacts("int n", "int i = 0;\nwhile (i < n) { i++; }");
    CHECK_EQ(f.step, StepKind::Additive);
    CHECK_EQ(f.bound, BoundKind::Input);

    f = loopFacts("int n", "int i = 0;\ndo { i += 2; } while (i < n);");
    CHECK_EQ(f.step, StepKind::Additive);

    f = loopFacts("int n", "while (n--) {}");
    CHECK_EQ(f.step, StepKind::Additive);
    CHECK_EQ(f.bound, BoundKind::Input);
}

TEST_CASE("loop facts: while loops the rules can't follow") {
    LoopFacts f = loopFacts("int n", "int i = 0;\nwhile (i < n) { if (ok()) i++; }");
    CHECK_EQ(f.step, StepKind::Other);
    f = loopFacts("int n", "while (n > 0) { work(); }");
    CHECK_EQ(f.step, StepKind::Other);
    CHECK(!f.note.empty());
    f = loopFacts("", "while (true) { if (done()) break; }");
    CHECK_EQ(f.step, StepKind::Other);
}

TEST_CASE("loop facts: binary-search halving") {
    const LoopFacts f = loopFacts(
        "const int a[], int lo, int hi, int x",
        "while (lo <= hi) {\n"
        "    int mid = lo + (hi - lo) / 2;\n"
        "    if (a[mid] == x) return mid;\n"
        "    if (a[mid] < x) lo = mid + 1; else hi = mid - 1;\n"
        "}");
    CHECK_EQ(f.step, StepKind::Multiplicative);
    CHECK_EQ(f.variable, std::string("lo, hi"));
    CHECK_EQ(f.bound, BoundKind::Input);
}

TEST_CASE("loop facts: range-for") {
    LoopFacts f = loopFacts("const std::vector<int>& v", "for (const auto& x : v) {}");
    CHECK_EQ(f.variable, std::string("x"));
    CHECK_EQ(f.step, StepKind::Additive);
    CHECK_EQ(f.bound, BoundKind::Input);
    CHECK_EQ(f.limit, std::string("v"));

    CHECK_EQ(loopFacts("", "for (int x : {1, 2, 3}) {}").bound, BoundKind::Constant);
    CHECK_EQ(loopFacts("", "std::vector<int> w; for (int x : w) {}").bound, BoundKind::Variable);
}

TEST_CASE("features: a function with no loops") {
    const FeatureVector f = features("int add(int a, int b) {\n    return a + b;\n}\n");
    CHECK_EQ(f.loopCount, 0);
    CHECK_EQ(f.maxNestingDepth, 0);
    CHECK(!f.hasRecursion);
    CHECK(!f.hasLogPattern);
    CHECK_EQ(f.functionCallCount, 0);
    CHECK_EQ(f.linesOfCode, 3);
}

TEST_CASE("features: loops, nesting depth and loop kinds") {
    const FeatureVector f = features(
        "void f(int n) {\n"
        "    for (int i = 0; i < n; i++)\n"
        "        for (int j = 1; j < n; j *= 2)\n"
        "            for (int k = 0; k < n; k = g(k)) {}\n"
        "    while (n > 0) { n--; }\n"
        "    if (n) { for (int x : {1, 2}) {} }\n"
        "}\n");
    CHECK_EQ(f.loopCount, 5);
    CHECK_EQ(f.maxNestingDepth, 3);
    CHECK_EQ(f.linearLoopCount, 3);
    CHECK_EQ(f.logLoopCount, 1);
    CHECK(f.hasLogPattern);
    CHECK_EQ(f.functionCallCount, 1);
}

TEST_CASE("features: if blocks don't add nesting depth") {
    const FeatureVector f = features(
        "void f(int n) {\n"
        "    if (n > 0) { if (n > 1) { for (int i = 0; i < n; i++) {} } }\n"
        "}\n");
    CHECK_EQ(f.maxNestingDepth, 1);
}

TEST_CASE("features: function calls are all counted") {
    const FeatureVector f = features(
        "void f(std::vector<int>& v) {\n"
        "    for (int i = 0; i < v.size(); i++) print(max(v[i], 0));\n"
        "    std::sort(v.begin(), v.end());\n"
        "}\n");
    CHECK_EQ(f.functionCallCount, 6);
}

TEST_CASE("features: recursion through name, this-> and Class::") {
    CHECK(features("int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }").hasRecursion);
    CHECK(features("struct T { int go(int n) { return n ? this->go(n - 1) : 0; } };").hasRecursion);
    CHECK(features("int T::go(int n) { return n ? T::go(n - 1) : 0; }").hasRecursion);
    CHECK(!features("int f(int n) { return g(n - 1); }").hasRecursion);
    CHECK(!features("int f(int n) { return other.f(n); }").hasRecursion);
}

TEST_CASE("features: log pattern from halving recursion and midpoints") {
    CHECK(features("int f(int n) { if (n <= 1) return 0; return f(n / 2) + 1; }").hasLogPattern);
    CHECK(features("int f(int n) { if (n <= 1) return 0; return f(n >> 1); }").hasLogPattern);
    CHECK(features(
        "int bs(int a[], int lo, int hi, int x) {\n"
        "    if (lo > hi) return -1;\n"
        "    int mid = (lo + hi) / 2;\n"
        "    if (a[mid] == x) return mid;\n"
        "    return a[mid] < x ? bs(a, mid + 1, hi, x) : bs(a, lo, mid - 1, x);\n"
        "}\n").hasLogPattern);
    CHECK(!features("int f(int n) { if (n <= 1) return 0; return f(n - 1); }").hasLogPattern);
    CHECK(!features("int avg(int a, int b) { int s = a * b / 2; return s; }").hasLogPattern);
}

TEST_CASE("features: lines of code skip blank and comment lines") {
    const FeatureVector f = features(
        "// header comment\n"
        "int f(int n) {\n"
        "\n"
        "    // a comment\n"
        "    /* a block\n"
        "       comment */\n"
        "    int s = 0;\n"
        "    return s;\n"
        "}\n");
    CHECK_EQ(f.linesOfCode, 4);
}

TEST_CASE("features: names and values line up") {
    FeatureVector f;
    f.loopCount = 2;
    f.maxNestingDepth = 1;
    f.hasRecursion = true;
    f.functionCallCount = 4;
    f.linesOfCode = 10;
    f.logLoopCount = 1;
    f.sortCallCount = 3;
    const std::vector<std::string> names = FeatureVector::names();
    const std::vector<double> values = f.values();
    REQUIRE(names.size() == values.size());
    CHECK_EQ(names.size(), std::size_t(9));
    CHECK_EQ(names[0], std::string("loop_count"));
    CHECK_EQ(values[0], 2.0);
    CHECK_EQ(values[2], 1.0);
    CHECK_EQ(values[3], 0.0);
    CHECK_EQ(values[4], 4.0);
    CHECK_EQ(values[5], 10.0);
    CHECK_EQ(values[7], 1.0);
    CHECK_EQ(names[8], std::string("sort_calls"));
    CHECK_EQ(values[8], 3.0);
}

TEST_CASE("features: tokens back to text") {
    const codecomplex::LexResult lexed = codecomplex::tokenize("i < v.size() && a[i] != f(x, ++k)");
    std::vector<Token> tokens = lexed.tokens;
    tokens.pop_back();
    CHECK_EQ(codecomplex::tokensToText(tokens), std::string("i < v.size() && a[i] != f(x, ++k)"));
}
