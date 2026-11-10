#include "TestFramework.h"

#include "analysis/Recurrence.h"
#include "analysis/RuleEngine.h"
#include "lexer/Lexer.h"
#include "structure/Parser.h"

#include <string>
#include <vector>

using codecomplex::Complexity;
using codecomplex::Function;
using codecomplex::Reason;
using codecomplex::Recurrence;
using codecomplex::RecursionResult;
using codecomplex::RuleResult;
using codecomplex::ShrinkKind;

namespace {

std::vector<Function> parseAll(const std::string& source) {
    const codecomplex::LexResult lexed = codecomplex::tokenize(source);
    REQUIRE(lexed.ok());
    return codecomplex::parse(lexed.tokens).functions;
}

Function parseOne(const std::string& source) {
    const std::vector<Function> functions = parseAll(source);
    REQUIRE(functions.size() == 1);
    return functions[0];
}

RuleResult analyze(const std::string& source) {
    return codecomplex::analyzeFunction(parseOne(source));
}

Recurrence recurrence(const std::string& source) {
    return codecomplex::extractRecurrence(parseOne(source));
}

bool hasReason(const std::vector<Reason>& reasons, const std::string& text) {
    for (const Reason& reason : reasons) {
        if (reason.text.find(text) != std::string::npos) return true;
    }
    return false;
}

bool hasReason(const RuleResult& result, int line, const std::string& text) {
    for (const Reason& reason : result.reasons) {
        if (reason.line == line && reason.text.find(text) != std::string::npos) return true;
    }
    return false;
}

Complexity upperBound(Complexity c) {
    c.upperBound = true;
    return c;
}

const char* const kFactorial =
    "int fact(int n) {\n"
    "    if (n <= 1) return 1;\n"
    "    return n * fact(n - 1);\n"
    "}\n";

const char* const kBinarySearch =
    "int search(int a[], int lo, int hi, int x) {\n"
    "    if (lo > hi) return -1;\n"
    "    int mid = (lo + hi) / 2;\n"
    "    if (a[mid] == x) return mid;\n"
    "    if (a[mid] < x) return search(a, mid + 1, hi, x);\n"
    "    return search(a, lo, mid - 1, x);\n"
    "}\n";

const char* const kMergeSort =
    "void mergeSort(int a[], int lo, int hi) {\n"
    "    if (lo >= hi) return;\n"
    "    int mid = lo + (hi - lo) / 2;\n"
    "    mergeSort(a, lo, mid);\n"
    "    mergeSort(a, mid + 1, hi);\n"
    "    int i = lo, j = mid + 1, k = 0;\n"
    "    for (int t = lo; t <= hi; t++) {\n"
    "        if (j > hi || (i <= mid && a[i] <= a[j])) tmp[k++] = a[i++];\n"
    "        else tmp[k++] = a[j++];\n"
    "    }\n"
    "    for (int t = lo; t <= hi; t++) a[t] = tmp[t - lo];\n"
    "}\n";

const char* const kFibonacci =
    "int fib(int n) {\n"
    "    if (n < 2) return n;\n"
    "    return fib(n - 1) + fib(n - 2);\n"
    "}\n";

const char* const kPermutations =
    "void permute(std::string& s, int k, int n) {\n"
    "    if (k == n) {\n"
    "        std::cout << s << '\\n';\n"
    "        return;\n"
    "    }\n"
    "    for (int i = k; i < n; i++) {\n"
    "        swap(s[k], s[i]);\n"
    "        permute(s, k + 1, n);\n"
    "        swap(s[k], s[i]);\n"
    "    }\n"
    "}\n";

const char* const kMemoFibonacci =
    "long long fib(int n, std::vector<long long>& memo) {\n"
    "    if (n < 2) return n;\n"
    "    if (memo[n] != -1) return memo[n];\n"
    "    return memo[n] = fib(n - 1, memo) + fib(n - 2, memo);\n"
    "}\n";

}

TEST_CASE("recurrence: factorial is T(n) = T(n - 1) + O(1) = O(n)") {
    const Recurrence rec = recurrence(kFactorial);
    CHECK(rec.unsupported.empty());
    CHECK_EQ(rec.calls, 1);
    CHECK(rec.shrink == ShrinkKind::Subtract);
    CHECK_EQ(rec.amount, 1);

    const RuleResult r = analyze(kFactorial);
    CHECK_EQ(r.complexity, Complexity::linear());
    CHECK_EQ(r.pattern, std::string("Linear Recursion"));
    CHECK(hasReason(r, 3, "fact(n - 1): n - 1 subtracts 1 from n"));
    CHECK(hasReason(r, 1, "T(n) = T(n - 1) + O(1)"));
}

TEST_CASE("recurrence: binary search has a = 1 and is O(log n)") {
    const Recurrence rec = recurrence(kBinarySearch);
    CHECK(rec.unsupported.empty());
    CHECK_EQ(rec.calls, 1);
    CHECK(rec.shrink == ShrinkKind::Divide);
    CHECK_EQ(rec.amount, 2);
    CHECK(hasReason(rec.reasons, "counted once"));

    const RuleResult r = analyze(kBinarySearch);
    CHECK_EQ(r.complexity, Complexity::logarithmic());
    CHECK_EQ(r.pattern, std::string("Divide and Conquer"));
    CHECK(hasReason(r, 5, "uses the midpoint mid"));
    CHECK(hasReason(r, 1, "Master Theorem case 2"));
}

TEST_CASE("recurrence: binary search with if / else and with ?: also has a = 1") {
    CHECK_EQ(recurrence("int s(int a[], int lo, int hi, int x) {\n"
                        "    if (lo > hi) return -1;\n"
                        "    int mid = (lo + hi) / 2;\n"
                        "    if (a[mid] == x) return mid;\n"
                        "    else if (a[mid] < x) return s(a, mid + 1, hi, x);\n"
                        "    else return s(a, lo, mid - 1, x);\n"
                        "}\n")
                 .calls,
             1);
    const char* ternary = "int s(int a[], int lo, int hi, int x) {\n"
                          "    if (lo > hi) return -1;\n"
                          "    int mid = (lo + hi) / 2;\n"
                          "    if (a[mid] == x) return mid;\n"
                          "    return a[mid] < x ? s(a, mid + 1, hi, x) : s(a, lo, mid - 1, x);\n"
                          "}\n";
    CHECK_EQ(recurrence(ternary).calls, 1);
    CHECK_EQ(analyze(ternary).complexity, Complexity::logarithmic());
}

TEST_CASE("recurrence: merge sort is 2T(n / 2) + O(n) = O(n log n)") {
    const Recurrence rec = recurrence(kMergeSort);
    CHECK(rec.unsupported.empty());
    CHECK_EQ(rec.calls, 2);
    CHECK(rec.shrink == ShrinkKind::Divide);
    CHECK_EQ(rec.amount, 2);

    const RuleResult r = analyze(kMergeSort);
    CHECK_EQ(r.nonRecursiveWork, Complexity::linear());
    CHECK_EQ(r.complexity, Complexity::linearithmic());
    CHECK_EQ(r.pattern, std::string("Divide and Conquer"));
    CHECK(hasReason(r, 1, "T(n) = 2*T(n / 2) + O(n)"));
}

TEST_CASE("recurrence: naive Fibonacci is O(2^n) as an upper bound") {
    const Recurrence rec = recurrence(kFibonacci);
    CHECK_EQ(rec.calls, 2);
    CHECK(rec.shrink == ShrinkKind::Subtract);
    CHECK_EQ(rec.amount, 1);

    const RuleResult r = analyze(kFibonacci);
    CHECK_EQ(r.complexity, upperBound(Complexity::exponential(2)));
    CHECK(r.complexity.upperBound);
    CHECK_EQ(r.pattern, std::string("Exponential Recursion"));
    CHECK(hasReason(r, 1, "<= 2*T(n - 1)"));
}

TEST_CASE("recurrence: Fibonacci shows its real recurrence before the bound") {
    const Recurrence rec = recurrence(kFibonacci);
    CHECK_EQ(rec.amounts.size(), std::size_t(2));
    const RuleResult r = analyze(kFibonacci);
    CHECK(hasReason(r, 1, "T(n) = T(n - 1) + T(n - 2) + O(1) <= 2*T(n - 1) + O(1)"));
    CHECK(hasReason(r, 1, "up to 2^n calls -> O(2^n)"));
    CHECK_EQ(r.complexity, upperBound(Complexity::exponential(2)));

    const RuleResult same = analyze("int g(int n) {\n"
                                    "    if (n == 0) return 1;\n"
                                    "    return g(n - 1) + g(n - 1);\n"
                                    "}\n");
    CHECK(hasReason(same, 1, "T(n) = 2*T(n - 1) + O(1): each call makes 2 more"));
    CHECK(!hasReason(same, 1, "<="));

    const RuleResult three = analyze("int t(int n) {\n"
                                     "    if (n < 3) return 1;\n"
                                     "    return t(n - 1) + t(n - 2) + t(n - 3);\n"
                                     "}\n");
    CHECK(hasReason(three, 1, "T(n - 1) + T(n - 2) + T(n - 3) + O(1) <= 3*T(n - 1) + O(1)"));
}

TEST_CASE("recurrence: permutations are n*T(n - 1) = O(n!)") {
    const Recurrence rec = recurrence(kPermutations);
    CHECK(rec.unsupported.empty());
    CHECK(rec.inLoop);
    CHECK_EQ(rec.calls, 1);

    const RuleResult r = analyze(kPermutations);
    CHECK_EQ(r.complexity, Complexity::factorial());
    CHECK_EQ(r.pattern, std::string("Permutation Recursion"));
    CHECK(hasReason(r, 8, "k + 1 moves k 1 towards the end"));
    CHECK(hasReason(r, 6, "one recursive call per iteration"));
}

TEST_CASE("recurrence: memoized Fibonacci is flagged uncertain") {
    const Recurrence rec = recurrence(kMemoFibonacci);
    CHECK(rec.memoized);
    CHECK(!recurrence(kFibonacci).memoized);

    const RuleResult r = analyze(kMemoFibonacci);
    CHECK(r.complexity.uncertain);
    CHECK(r.complexity.kind == Complexity::Kind::Exponential);
    CHECK(hasReason(r, 3, "memoized: the condition memo[n]"));
    CHECK(hasReason(r, 1, "memoized recursion"));
}

TEST_CASE("recurrence: a map lookup with count() also counts as memoization") {
    CHECK(recurrence("long long f(int n) {\n"
                     "    if (n < 2) return 1;\n"
                     "    if (seen.count(n)) return seen[n];\n"
                     "    seen[n] = f(n - 1) + f(n - 2);\n"
                     "    return seen[n];\n"
                     "}\n")
              .memoized);
}

TEST_CASE("recurrence: T(n - 1) + O(n) is O(n^2)") {
    const RuleResult r = analyze("void sortDown(int a[], int n) {\n"
                                 "    if (n <= 1) return;\n"
                                 "    for (int i = 0; i < n - 1; i++) if (a[i] > a[i + 1]) swap(a[i], a[i + 1]);\n"
                                 "    sortDown(a, n - 1);\n"
                                 "}\n");
    CHECK_EQ(r.complexity, Complexity::polynomial(2));
    CHECK_EQ(r.pattern, std::string("Linear Recursion"));
}

TEST_CASE("recurrence: halving with n / 2 or n >> 1 is O(log n)") {
    CHECK_EQ(analyze("int f(int n) { if (n <= 1) return 0; return 1 + f(n / 2); }").complexity,
             Complexity::logarithmic());
    CHECK_EQ(analyze("int f(int n) { if (n <= 1) return 0; return 1 + f(n >> 1); }").complexity,
             Complexity::logarithmic());
}

TEST_CASE("recurrence: Master Theorem case 1 and case 3") {
    CHECK_EQ(analyze("int f(int n) { if (n <= 1) return 1; return f(n / 2) + f(n / 2); }").complexity,
             Complexity::linear());
    CHECK_EQ(analyze("int f(int n) { if (n <= 1) return 1;\n"
                     "return f(n / 2) + f(n / 2) + f(n / 2) + f(n / 2); }")
                 .complexity,
             Complexity::polynomial(2));
    const RuleResult r = analyze("int f(int n) { if (n <= 1) return 1; int s = 0;\n"
                                 "for (int i = 0; i < n; i++) s++; return s + f(n / 2); }");
    CHECK_EQ(r.complexity, Complexity::linear());
    CHECK(hasReason(r, 1, "case 3"));
}

TEST_CASE("recurrence: a non-integer Master Theorem exponent is rounded up as an upper bound") {
    const RuleResult r = analyze("int f(int n) { if (n <= 1) return 1; int s = 0;\n"
                                 "for (int i = 0; i < n; i++) s++;\n"
                                 "return f(n / 2) + f(n / 2) + f(n / 2) + s; }");
    CHECK_EQ(r.complexity, upperBound(Complexity::polynomial(2)));
    CHECK(hasReason(r, 1, "n^1.58"));
}

TEST_CASE("recurrence: an uncertain f(n) makes the result uncertain") {
    const RuleResult r = analyze("int f(int n) { if (n <= 1) return 1; helper(n); return f(n - 1); }");
    CHECK(r.complexity.uncertain);
    CHECK(r.complexity.kind == Complexity::Kind::Polynomial);
    CHECK_EQ(r.complexity.power, 1);
}

TEST_CASE("recurrence: this-> and Class:: self-calls are recognised") {
    CHECK_EQ(analyze("struct T { int go(int n) { return n == 0 ? 0 : this->go(n - 1); } };").complexity,
             Complexity::linear());
    CHECK_EQ(analyze("int T::go(int n) { return n == 0 ? 0 : T::go(n - 1); }").complexity,
             Complexity::linear());
}

TEST_CASE("recurrence: an uneven split is Unknown") {
    const RuleResult r = analyze("int f(int n) { if (n <= 1) return 1; return f(n / 2) + f(n / 3); }");
    CHECK(r.complexity.isUnknown());
    CHECK_EQ(r.pattern, std::string("Recursion"));
    CHECK(hasReason(r, 1, "uneven split"));
}

TEST_CASE("recurrence: an argument that does not shrink in a known way is Unknown") {
    const RuleResult r = analyze("int f(int n) { if (n <= 1) return 1; return f(2 * n / 3); }");
    CHECK(r.complexity.isUnknown());
    CHECK(hasReason(r, 1, "no argument shrinks in a recognised way"));
    CHECK(analyze("void f(int n) { if (n == 0) return; f(n); }").complexity.isUnknown());
}

TEST_CASE("recurrence: mixing n - c and n / b is Unknown") {
    const RuleResult r = analyze("int f(int n) { if (n <= 1) return 1; return f(n - 1) + f(n / 2); }");
    CHECK(r.complexity.isUnknown());
    CHECK(hasReason(r, 1, "mix"));
}

TEST_CASE("recurrence: other calls inside loops are Unknown") {
    const RuleResult grid = analyze("int f(int n) { if (n == 0) return 1; int s = 0;\n"
                                    "for (int d = 0; d < 4; d++) s += f(n - 1); return s; }");
    CHECK(grid.complexity.isUnknown());
    CHECK(hasReason(grid, 1, "does not run over the remaining items"));
    const RuleResult two = analyze("int f(int k, int n) { if (k == n) return 1; int s = 0;\n"
                                   "for (int i = k; i < n; i++) s += f(k + 1, n) + f(k + 1, n);\n"
                                   "return s; }");
    CHECK(two.complexity.isUnknown());
    CHECK(hasReason(two, 1, "2 recursive calls per iteration"));
    CHECK(analyze("int f(int k, int n) { if (k == n) return 1; int s = f(k + 1, n);\n"
                  "for (int i = k; i < n; i++) s += f(k + 1, n); return s; }")
              .complexity.isUnknown());
    CHECK(analyze("int f(int k, int n) { if (k >= n) return 1; int s = 0;\n"
                  "for (int i = k; i < n; i++) s += f(k + 2, n); return s; }")
              .complexity.isUnknown());
}

TEST_CASE("recurrence: mutual recursion is Unknown") {
    const std::vector<Function> program = parseAll(
        "bool isOdd(int n);\n"
        "bool isEven(int n) { if (n == 0) return true; return isOdd(n - 1); }\n"
        "bool isOdd(int n) { if (n == 0) return false; return isEven(n - 1); }\n"
        "int square(int n) { return n * n; }\n");
    REQUIRE(program.size() == 3);
    const std::vector<std::string> cycle = codecomplex::mutualRecursionCycle(program[0], program);
    REQUIRE(cycle.size() == 3);
    CHECK_EQ(cycle[0], std::string("isEven"));
    CHECK_EQ(cycle[1], std::string("isOdd"));
    CHECK_EQ(cycle[2], std::string("isEven"));
    CHECK(codecomplex::mutualRecursionCycle(program[2], program).empty());

    const RuleResult r = codecomplex::analyzeFunction(program[0], program);
    CHECK(r.complexity.isUnknown());
    CHECK(r.hasRecursion);
    CHECK_EQ(r.pattern, std::string("Mutual Recursion"));
    CHECK(hasReason(r, 2, "isEven() -> isOdd() -> isEven()"));
    const std::vector<Function> self = parseAll(std::string(kFactorial) + "int g(int n) { return fact(n); }\n");
    CHECK_EQ(codecomplex::analyzeFunction(self[0], self).complexity, Complexity::linear());
}

TEST_CASE("recurrence: an Unknown or non-polynomial f(n) is Unknown") {
    const RuleResult r = analyze("int f(int n, int k) { if (n <= 1) return 1; int s = 0;\n"
                                 "for (int i = 0; i < n; i += k) s++; return s + f(n - 1, k); }");
    CHECK(r.complexity.isUnknown());
    CHECK(hasReason(r, 1, "f(n) is Unknown"));

    Recurrence rec;
    rec.calls = 1;
    rec.shrink = ShrinkKind::Subtract;
    rec.amount = 1;
    const RecursionResult solved = codecomplex::solveRecurrence(rec, Complexity::exponential(2), 1);
    CHECK(solved.complexity.isUnknown());
    CHECK(hasReason(solved.reasons, "not polynomial"));
}
