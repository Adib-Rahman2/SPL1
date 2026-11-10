#include "TestFramework.h"

#include "analysis/RuleEngine.h"
#include "lexer/Lexer.h"
#include "structure/Parser.h"

#include <string>

using codecomplex::Complexity;
using codecomplex::Reason;
using codecomplex::RuleResult;

namespace {

RuleResult analyze(const std::string& source) {
    const codecomplex::LexResult lexed = codecomplex::tokenize(source);
    REQUIRE(lexed.ok());
    const codecomplex::ParseResult parsed = codecomplex::parse(lexed.tokens);
    REQUIRE(parsed.functions.size() == 1);
    return codecomplex::analyzeFunction(parsed.functions[0]);
}

bool hasReason(const RuleResult& result, int line, const std::string& text) {
    for (const Reason& reason : result.reasons) {
        if (reason.line == line && reason.text.find(text) != std::string::npos) return true;
    }
    return false;
}

}

TEST_CASE("rules: no loops is O(1)") {
    const RuleResult r = analyze("int add(int a, int b) {\n    return a + b;\n}\n");
    CHECK_EQ(r.complexity, Complexity::constant());
    CHECK_EQ(r.pattern, std::string("No Loops"));
    CHECK(hasReason(r, 1, "no loops"));
    CHECK(!r.hasRecursion);
}

TEST_CASE("rules: a single linear loop is O(n)") {
    const RuleResult r = analyze(
        "int sum(int a[], int n) {\n"
        "    int s = 0;\n"
        "    for (int i = 0; i < n; i++) s += a[i];\n"
        "    return s;\n"
        "}\n");
    CHECK_EQ(r.complexity, Complexity::linear());
    CHECK_EQ(r.pattern, std::string("Single Loop"));
    CHECK(hasReason(r, 3, "for loop on i: step i++ adds a constant, from 0 to n (input size)"));
}

TEST_CASE("rules: a loop over v.size() and a range-for are O(n)") {
    CHECK_EQ(analyze("int f(std::vector<int>& v) { int s = 0;\n"
                     "for (int i = 0; i < v.size(); i++) s += v[i]; return s; }")
                 .complexity,
             Complexity::linear());
    CHECK_EQ(analyze("int f(const std::vector<int>& v) { int s = 0;\n"
                     "for (int x : v) s += x; return s; }")
                 .complexity,
             Complexity::linear());
}

TEST_CASE("rules: a multiplying loop is O(log n)") {
    const RuleResult r = analyze(
        "int bits(int n) {\n"
        "    int c = 0;\n"
        "    for (int i = 1; i < n; i *= 2) c++;\n"
        "    return c;\n"
        "}\n");
    CHECK_EQ(r.complexity, Complexity::logarithmic());
    CHECK_EQ(r.pattern, std::string("Logarithmic Loop"));
    CHECK(hasReason(r, 3, "multiplies or divides by a constant"));
}

TEST_CASE("rules: a halving while loop is O(log n)") {
    const RuleResult r = analyze("int f(int n) {\n    while (n > 1) { n /= 2; }\n    return n;\n}\n");
    CHECK_EQ(r.complexity, Complexity::logarithmic());
    CHECK_EQ(r.pattern, std::string("Logarithmic Loop"));
}

TEST_CASE("rules: iterative binary search is O(log n)") {
    const RuleResult r = analyze(
        "int search(const int a[], int lo, int hi, int x) {\n"
        "    while (lo <= hi) {\n"
        "        int mid = (lo + hi) / 2;\n"
        "        if (a[mid] == x) return mid;\n"
        "        if (a[mid] < x) lo = mid + 1;\n"
        "        else hi = mid - 1;\n"
        "    }\n"
        "    return -1;\n"
        "}\n");
    CHECK_EQ(r.complexity, Complexity::logarithmic());
    CHECK(hasReason(r, 2, "halved around mid"));
}

TEST_CASE("rules: binary search on local bounds is O(log n), uncertain") {
    const RuleResult r = analyze(
        "int search(const std::vector<int>& a, int x) {\n"
        "    int lo = 0, hi = a.size() - 1;\n"
        "    while (lo <= hi) {\n"
        "        int mid = lo + (hi - lo) / 2;\n"
        "        if (a[mid] < x) lo = mid + 1; else hi = mid - 1;\n"
        "    }\n"
        "    return lo;\n"
        "}\n");
    CHECK_EQ(r.complexity.notation(), std::string("O(log n)"));
    CHECK(r.complexity.uncertain);
}

TEST_CASE("rules: sequential loops are O(n), not O(n^2)") {
    const RuleResult r = analyze(
        "void f(int a[], int n) {\n"
        "    for (int i = 0; i < n; i++) a[i] = 0;\n"
        "    for (int j = 0; j < n; j++) a[j] += j;\n"
        "}\n");
    CHECK_EQ(r.complexity, Complexity::linear());
    CHECK_EQ(r.pattern, std::string("Sequential Loops"));
    CHECK(hasReason(r, 1, "in sequence: O(n) + O(n) = O(n)"));
}

TEST_CASE("rules: nested loops are O(n^2)") {
    const RuleResult r = analyze(
        "int pairs(int a[], int n) {\n"
        "    int c = 0;\n"
        "    for (int i = 0; i < n; i++)\n"
        "        for (int j = 0; j < n; j++)\n"
        "            if (a[i] == a[j]) c++;\n"
        "    return c;\n"
        "}\n");
    CHECK_EQ(r.complexity, Complexity::polynomial(2));
    CHECK_EQ(r.pattern, std::string("Nested Loops"));
    CHECK(hasReason(r, 3, "nested: O(n) iterations x O(n) per iteration = O(n^2)"));
}

TEST_CASE("rules: a triangular loop (j < i) is O(n^2)") {
    const RuleResult r = analyze(
        "void sort(int a[], int n) {\n"
        "    for (int i = 0; i < n; i++)\n"
        "        for (int j = 0; j < i; j++)\n"
        "            if (a[j] > a[i]) { int t = a[i]; a[i] = a[j]; a[j] = t; }\n"
        "}\n");
    CHECK_EQ(r.complexity, Complexity::polynomial(2));
}

TEST_CASE("rules: three nested loops are O(n^3)") {
    const RuleResult r = analyze(
        "void mul(int n) {\n"
        "    for (int i = 0; i < n; i++)\n"
        "        for (int j = 0; j < n; j++)\n"
        "            for (int k = 0; k < n; k++) c[i][j] += a[i][k] * b[k][j];\n"
        "}\n");
    CHECK_EQ(r.complexity, Complexity::polynomial(3));
    CHECK_EQ(r.pattern, std::string("Nested Loops"));
}

TEST_CASE("rules: a nested loop with a constant bound stays O(n)") {
    const RuleResult r = analyze(
        "void count(const std::string& s) {\n"
        "    for (int i = 0; i < s.size(); i++)\n"
        "        for (int c = 0; c < 26; c++) freq[c]++;\n"
        "}\n");
    CHECK_EQ(r.complexity, Complexity::linear());
    CHECK_EQ(r.pattern, std::string("Single Loop"));
    CHECK(hasReason(r, 3, "runs a constant number of times"));
}

TEST_CASE("rules: only constant loops is O(1)") {
    const RuleResult r = analyze("void f() {\n    for (int i = 0; i < 10; i++) x += i;\n}\n");
    CHECK_EQ(r.complexity, Complexity::constant());
    CHECK_EQ(r.pattern, std::string("Constant-Bound Loops"));
}

TEST_CASE("rules: a linear loop around a log loop is O(n log n)") {
    const RuleResult r = analyze(
        "void f(int n) {\n"
        "    for (int i = 0; i < n; i++)\n"
        "        for (int j = 1; j < n; j *= 2) x++;\n"
        "}\n");
    CHECK_EQ(r.complexity, Complexity::linearithmic());
    CHECK_EQ(r.pattern, std::string("Nested Loops with a Logarithmic Loop"));
}

TEST_CASE("rules: two nested log loops are O(log^2 n)") {
    const RuleResult r = analyze(
        "void f(int n) {\n"
        "    for (int i = 1; i < n; i *= 2)\n"
        "        for (int j = n; j > 0; j /= 2) x++;\n"
        "}\n");
    CHECK_EQ(r.complexity, Complexity::polynomial(0, 2));
    CHECK_EQ(r.pattern, std::string("Nested Logarithmic Loops"));
}

TEST_CASE("rules: an unknown step gives Unknown") {
    const RuleResult r = analyze(
        "void walk(int n) {\n"
        "    for (int i = 0; i < n; i = next[i]) visit(i);\n"
        "}\n");
    CHECK(r.complexity.isUnknown());
    CHECK_EQ(r.pattern, std::string("Unknown"));
    CHECK(hasReason(r, 2, "the number of iterations is unknown"));
}

TEST_CASE("rules: a square-root bound gives Unknown") {
    const RuleResult r = analyze(
        "bool prime(int n) {\n"
        "    for (int i = 2; i * i <= n; i++) if (n % i == 0) return false;\n"
        "    return true;\n"
        "}\n");
    CHECK(r.complexity.isUnknown());
    CHECK(hasReason(r, 2, "i * i <= n"));
}

TEST_CASE("rules: an infinite loop gives Unknown") {
    CHECK(analyze("void f() { while (true) { if (done()) break; } }").complexity.isUnknown());
    CHECK(analyze("void f() { for (;;) { if (done()) break; } }").complexity.isUnknown());
}

TEST_CASE("rules: Unknown inside a sequence makes everything Unknown") {
    const RuleResult r = analyze(
        "void f(int n) {\n"
        "    for (int i = 0; i < n; i++) x++;\n"
        "    for (int i = 0; i < n; i += step) x++;\n"
        "}\n");
    CHECK(r.complexity.isUnknown());
}

TEST_CASE("rules: a bound not traced to the input is uncertain") {
    const RuleResult r = analyze(
        "void f() {\n"
        "    std::vector<int> items = load();\n"
        "    for (int x : items) total += x;\n"
        "}\n");
    CHECK_EQ(r.complexity.notation(), std::string("O(n)"));
    CHECK(r.complexity.uncertain);
    CHECK(hasReason(r, 3, "not traced to the input size"));
}

TEST_CASE("rules: a loop variable changed in the body is uncertain") {
    const RuleResult r = analyze(
        "void f(int n) {\n"
        "    for (int i = 0; i < n; i++) { if (skip(i)) i++; }\n"
        "}\n");
    CHECK_EQ(r.complexity.notation(), std::string("O(n)"));
    CHECK(r.complexity.uncertain);
    CHECK(hasReason(r, 2, "also changed inside the loop body"));
}

TEST_CASE("rules: unknown calls are assumed O(1) and make the result uncertain") {
    const RuleResult r = analyze(
        "void f(std::vector<int>& v, int n) {\n"
        "    for (int i = 0; i < n; i++) {\n"
        "        v.push_back(i);\n"
        "        helper(v);\n"
        "        helper(v);\n"
        "    }\n"
        "}\n");
    CHECK_EQ(r.complexity.notation(), std::string("O(n)"));
    CHECK(r.complexity.uncertain);
    CHECK(hasReason(r, 4, "call to helper() is assumed O(1)"));
    int helperReasons = 0;
    for (const Reason& reason : r.reasons) {
        if (reason.text.find("helper") != std::string::npos) ++helperReasons;
        CHECK(reason.text.find("push_back") == std::string::npos);
    }
    CHECK_EQ(helperReasons, 1);
}

TEST_CASE("rules: known constant-time calls keep the result certain") {
    const RuleResult r = analyze(
        "int f(const std::vector<int>& v) {\n"
        "    int best = 0;\n"
        "    for (int i = 0; i < v.size(); i++) best = std::max(best, v[i]);\n"
        "    return best;\n"
        "}\n");
    CHECK_EQ(r.complexity, Complexity::linear());
}

TEST_CASE("rules: if/else takes the most expensive branch") {
    const RuleResult r = analyze(
        "void f(int n, bool all) {\n"
        "    if (all) {\n"
        "        for (int i = 0; i < n; i++)\n"
        "            for (int j = 0; j < n; j++) x++;\n"
        "    } else {\n"
        "        for (int i = 0; i < n; i++) x++;\n"
        "    }\n"
        "}\n");
    CHECK_EQ(r.complexity, Complexity::polynomial(2));
    CHECK(hasReason(r, 2, "the most expensive branch"));
}

TEST_CASE("rules: recursion is handed to the recurrence solver") {
    const RuleResult r = analyze(
        "int fib(int n) {\n"
        "    if (n < 2) return n;\n"
        "    return fib(n - 1) + fib(n - 2);\n"
        "}\n");
    Complexity expected = Complexity::exponential(2);
    expected.upperBound = true;
    CHECK(r.hasRecursion);
    CHECK_EQ(r.complexity, expected);
    CHECK_EQ(r.pattern, std::string("Exponential Recursion"));
    CHECK_EQ(r.nonRecursiveWork, Complexity::constant());
    CHECK(hasReason(r, 3, "recursive call to fib()"));
    CHECK(hasReason(r, 1, "f(n) is O(1)"));
}

TEST_CASE("rules: recursion keeps the cost of the rest of the body") {
    const RuleResult r = analyze(
        "void sortRange(int a[], int lo, int hi) {\n"
        "    if (lo >= hi) return;\n"
        "    int mid = (lo + hi) / 2;\n"
        "    sortRange(a, lo, mid);\n"
        "    sortRange(a, mid + 1, hi);\n"
        "    for (int i = lo; i <= hi; i++) tmp[i] = a[i];\n"
        "}\n");
    CHECK(r.hasRecursion);
    CHECK_EQ(r.complexity, Complexity::linearithmic());
    CHECK_EQ(r.nonRecursiveWork, Complexity::linear());
}

TEST_CASE("rules: unsupported code gives Unknown with its line") {
    const RuleResult r = analyze(
        "void f(int n) {\n"
        "    for (int i = 0; i < n; i++) x++;\n"
        "    auto g = [](int a) { return a; };\n"
        "}\n");
    CHECK(r.complexity.isUnknown());
    CHECK_EQ(r.pattern, std::string("Unknown"));
    CHECK(hasReason(r, 3, "unsupported code"));
}

TEST_CASE("rules: every reason has a line number") {
    const RuleResult r = analyze(
        "void f(int n) {\n"
        "    for (int i = 0; i < n; i++)\n"
        "        for (int j = 1; j < n; j *= 2) g(i, j);\n"
        "    while (n > 0) n--;\n"
        "}\n");
    CHECK(!r.reasons.empty());
    for (const Reason& reason : r.reasons) {
        CHECK(reason.line >= 1);
        CHECK(reason.line <= 5);
        CHECK(!reason.text.empty());
    }
}

TEST_CASE("rules: an inner step of the outer variable is a harmonic sum, O(n log n)") {
    const RuleResult r = analyze(
        "long long harmonic(int n) {\n"
        "    long long s = 0;\n"
        "    for (int i = 1; i <= n; i++) {\n"
        "        for (int j = i; j <= n; j += i) {\n"
        "            s += (i + j) & 1;\n"
        "        }\n"
        "    }\n"
        "    return s;\n"
        "}\n");
    CHECK_EQ(r.complexity, Complexity::linearithmic());
    CHECK(!r.complexity.uncertain);
    CHECK(hasReason(r, 4, "harmonic"));
    CHECK_EQ(r.pattern, std::string("Harmonic Nested Loops"));
    CHECK_EQ(analyze("int f(int n) { int s = 0;\n"
                     "for (int i = 1; i < n; i++) for (int j = 0; j < n; j = j + i) s++;\n"
                     "return s; }")
                 .complexity,
             Complexity::linearithmic());
}

TEST_CASE("rules: a harmonic-looking step that is not one stays Unknown") {
    CHECK(analyze("int f(int n) { int s = 0;\n"
                  "for (int i = 0; i < n; i++) for (int j = i; j < n; j += i) s++;\n"
                  "return s; }")
              .complexity.isUnknown());
    CHECK(analyze("int f(int n, int k) { int s = 0;\n"
                  "for (int i = 1; i < n; i++) for (int j = 0; j < n; j += k) s++;\n"
                  "return s; }")
              .complexity.isUnknown());
    CHECK(analyze("int f(int n) { int s = 0;\n"
                  "for (int i = 1; i < n; i *= 2) for (int j = 0; j < n; j += i) s++;\n"
                  "return s; }")
              .complexity.isUnknown());
}

TEST_CASE("rules: a loop up to a doubling variable is a geometric sum") {
    const RuleResult r = analyze(
        "long long three(int n) {\n"
        "    long long s = 0;\n"
        "    for (int i = 1; i <= n; i++) {\n"
        "        for (int j = 1; j <= n; j *= 2) {\n"
        "            for (int k = 0; k < j; k++) {\n"
        "                s += (i + j + k) & 1;\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    return s;\n"
        "}\n");
    CHECK_EQ(r.complexity, Complexity::polynomial(2));
    CHECK(!r.complexity.uncertain);
    CHECK(hasReason(r, 4, "geometric"));
    CHECK_EQ(analyze("int f(int n) { int s = 0;\n"
                     "for (int j = 1; j < n; j *= 2) for (int k = 0; k < j; k++) s++;\n"
                     "return s; }")
                 .complexity,
             Complexity::linear());
    CHECK_EQ(analyze("int f(int n) { int s = 0;\n"
                     "for (int j = n; j > 0; j /= 2) for (int k = 0; k < j; k++) s++;\n"
                     "return s; }")
                 .complexity,
             Complexity::linear());
}

TEST_CASE("rules: no geometric sum when other work grows with the input") {
    CHECK_EQ(analyze("int f(int n) { int s = 0;\n"
                     "for (int j = 1; j < n; j *= 2) {\n"
                     "  for (int k = 0; k < j; k++) s++;\n"
                     "  for (int m = 0; m < n; m++) s++; }\n"
                     "return s; }")
                 .complexity.notation(),
             std::string("O(n log n)"));
    CHECK_EQ(analyze("int f(int n) { int s = 0;\n"
                     "for (int j = 1; j < n; j *= 2) for (int k = j; k < n; k++) s++;\n"
                     "return s; }")
                 .complexity,
             Complexity::linearithmic());
    CHECK(analyze("int f(int n) { int s = 0;\n"
                  "for (int j = 1; j < n; j *= 2) { g(j); for (int k = 0; k < j; k++) s++; }\n"
                  "return s; }")
              .complexity.notation() != std::string("O(n)"));
}

namespace {

RuleResult analyzeIn(const std::string& source, const std::string& name) {
    const codecomplex::LexResult lexed = codecomplex::tokenize(source);
    REQUIRE(lexed.ok());
    const codecomplex::ParseResult parsed = codecomplex::parse(lexed.tokens);
    for (const codecomplex::Function& f : parsed.functions) {
        if (f.name == name) return codecomplex::analyzeFunction(f, parsed.functions);
    }
    REQUIRE(false);
    return RuleResult();
}

const char* const kHelpers =
    "int sum(int a[], int n) { int s = 0; for (int i = 0; i < n; i++) s += a[i]; return s; }\n"
    "int twice(int x) { return 2 * x; }\n"
    "int once(int a[], int n) { return sum(a, n); }\n"
    "int many(int a[], int n) { int t = 0;\n"
    "  for (int i = 0; i < n; i++) t += sum(a, n);\n"
    "  return t; }\n"
    "int cheap(int n) { return twice(n) + 1; }\n";

}

TEST_CASE("rules: a call to a function in the file costs what that function costs") {
    const RuleResult once = analyzeIn(kHelpers, "once");
    CHECK_EQ(once.complexity.notation(), std::string("O(n)"));
    CHECK(once.complexity.uncertain);
    CHECK(hasReason(once, 3, "call to sum() costs O(n)"));

    const RuleResult many = analyzeIn(kHelpers, "many");
    CHECK_EQ(many.complexity.notation(), std::string("O(n^2)"));
    CHECK(hasReason(many, 5, "nested: O(n) iterations x O(n) per iteration = O(n^2)"));

    const RuleResult cheap = analyzeIn(kHelpers, "cheap");
    CHECK_EQ(cheap.complexity, Complexity::constant());
    CHECK(hasReason(cheap, 7, "call to twice() costs O(1)"));
}

TEST_CASE("rules: calls the rules cannot match to one function stay assumed O(1)") {
    const RuleResult overload = analyzeIn(
        "int f(int n) { int s = 0; for (int i = 0; i < n; i++) s++; return s; }\n"
        "int f(int a, int b) { return a + b; }\n"
        "int g(int n) { return f(n); }\n", "g");
    CHECK(hasReason(overload, 3, "call to f() is assumed O(1)"));
    CHECK_EQ(overload.complexity.notation(), std::string("O(1)"));

    const RuleResult method = analyzeIn(
        "int size(int n) { int s = 0; for (int i = 0; i < n; i++) s++; return s; }\n"
        "int g(Box b) { return b.size(); }\n", "g");
    CHECK_EQ(method.complexity.notation(), std::string("O(1)"));
}

TEST_CASE("rules: calls between functions that recurse into each other stay Unknown") {
    const char* source =
        "bool isEven(int n);\n"
        "bool isOdd(int n) { return n == 0 ? false : isEven(n - 1); }\n"
        "bool isEven(int n) { return n == 0 ? true : isOdd(n - 1); }\n"
        "int main() { return isEven(10); }\n";
    const RuleResult odd = analyzeIn(source, "isOdd");
    CHECK(odd.complexity.isUnknown());
    CHECK(hasReason(odd, 2, "call to isEven() is assumed O(1)"));
    const RuleResult m = analyzeIn(source, "main");
    CHECK(m.complexity.isUnknown());
    CHECK(hasReason(m, 4, "call to isEven() has an Unknown cost"));
}

TEST_CASE("rules: main takes the cost of what it calls") {
    const RuleResult m = analyzeIn(
        "long long quad(int n) { long long s = 0;\n"
        "  for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) s++; return s; }\n"
        "long long fib(int n) { if (n <= 1) return n; return fib(n - 1) + fib(n - 2); }\n"
        "int main() { int n; std::cin >> n;\n"
        "  std::cout << quad(n) << fib(n);\n"
        "  return 0; }\n", "main");
    CHECK_EQ(m.complexity.notation(), std::string("O(2^n)"));
    CHECK(m.complexity.upperBound);
    CHECK(m.complexity.uncertain);
    CHECK(hasReason(m, 5, "call to quad() costs O(n^2)"));
    CHECK(hasReason(m, 5, "call to fib() costs O(2^n)"));
    CHECK_EQ(m.pattern, std::string("Cost of Called Functions"));
    CHECK(hasReason(m, 4, "no loops: the cost comes from the calls"));
}
