#include "TestFramework.h"

#include "analysis/LibraryCosts.h"
#include "analysis/RuleEngine.h"
#include "lexer/Lexer.h"
#include "structure/Parser.h"

#include <string>

using codecomplex::Complexity;
using codecomplex::ContainerKind;
using codecomplex::Language;
using codecomplex::Reason;
using codecomplex::RuleResult;

namespace {

codecomplex::ParseResult parseIn(const std::string& source, Language language) {
    const codecomplex::LexResult lexed = codecomplex::tokenize(source, language);
    REQUIRE(lexed.ok());
    return codecomplex::parse(lexed.tokens, language);
}

RuleResult analyzeNamed(const std::string& source, const std::string& name,
                        Language language = Language::Cpp) {
    const codecomplex::ParseResult parsed = parseIn(source, language);
    for (const codecomplex::Function& function : parsed.functions) {
        if (function.name == name) return codecomplex::analyzeFunction(function, parsed.functions);
    }
    REQUIRE(false);
    return RuleResult();
}

bool hasReason(const RuleResult& result, int line, const std::string& text) {
    for (const Reason& reason : result.reasons) {
        if (reason.line == line && reason.text.find(text) != std::string::npos) return true;
    }
    return false;
}

Complexity uncertain(Complexity c) {
    c.uncertain = true;
    return c;
}

}

TEST_CASE("library: std::sort on the input is O(n log n)") {
    const RuleResult r = analyzeNamed(
        "void f(std::vector<int>& v) {\n"
        "    std::sort(v.begin(), v.end());\n"
        "}\n", "f");
    CHECK_EQ(r.complexity, Complexity::linearithmic());
    CHECK(hasReason(r, 2, "std::sort() is sorting -> O(n log n)"));
}

TEST_CASE("library: Arrays.sort in Java, and sorting inside a loop") {
    const RuleResult r = analyzeNamed(
        "class A { static void f(int[] a, int n) {\n"
        "    Arrays.sort(a);\n"
        "    for (int i = 0; i < n; i++) Arrays.sort(a);\n"
        "} }\n", "f", Language::Java);
    CHECK_EQ(r.complexity, Complexity::polynomial(2, 1));
}

TEST_CASE("library: a sort of a range not traced to the input is uncertain") {
    const RuleResult r = analyzeNamed(
        "void f() {\n"
        "    std::vector<int> local = read();\n"
        "    sort(local.begin(), local.end());\n"
        "}\n", "f");
    CHECK_EQ(r.complexity, uncertain(Complexity::linearithmic()));
    CHECK(hasReason(r, 3, "not traced to the input size"));
}

TEST_CASE("library: a user's own sort() is not std::sort") {
    const RuleResult r = analyzeNamed(
        "void sort(int a[], int n) { }\n"
        "void f(int a[], int n) {\n"
        "    sort(a, n);\n"
        "}\n", "f");
    CHECK_EQ(r.complexity, Complexity::constant());
    CHECK(hasReason(r, 3, "call to sort() costs O(1)"));
    CHECK(!hasReason(r, 3, "library-cost table"));
}

TEST_CASE("library: linear algorithms and binary search") {
    CHECK_EQ(analyzeNamed("int f(std::vector<int>& v, int x) {\n"
                          "    return std::count(v.begin(), v.end(), x);\n}\n", "f").complexity,
             Complexity::linear());
    CHECK_EQ(analyzeNamed("bool f(std::vector<int>& v, int x) {\n"
                          "    return std::binary_search(v.begin(), v.end(), x);\n}\n", "f")
                 .complexity,
             Complexity::logarithmic());
    CHECK_EQ(analyzeNamed("int f(std::string s) {\n    return s.find('x');\n}\n", "f").complexity,
             uncertain(Complexity::constant()));
}

TEST_CASE("library: map and priority_queue operations are O(log n), hash ones O(1)") {
    const RuleResult tree = analyzeNamed(
        "void f(std::vector<int>& v) {\n"
        "    std::map<int, int> seen;\n"
        "    for (int x : v) seen.insert({x, 1});\n"
        "}\n", "f");
    CHECK_EQ(tree.complexity, uncertain(Complexity::linearithmic()));

    const RuleResult heap = analyzeNamed(
        "class A { void f(int n) {\n"
        "    PriorityQueue<Integer> pq = new PriorityQueue<>();\n"
        "    for (int i = 0; i < n; i++) pq.add(i);\n"
        "} }\n", "f", Language::Java);
    CHECK_EQ(heap.complexity, uncertain(Complexity::linearithmic()));

    const RuleResult hash = analyzeNamed(
        "class A { void f(int n) {\n"
        "    Map<Integer, Integer> m = new HashMap<>();\n"
        "    for (int i = 0; i < n; i++) m.put(i, i);\n"
        "} }\n", "f", Language::Java);
    CHECK_EQ(hash.complexity, Complexity::linear());
    CHECK(hasReason(hash, 3, "hash-table operation"));
}

TEST_CASE("library: containers are found in parameters and declarations") {
    const codecomplex::ParseResult parsed = parseIn(
        "void f(const std::set<int>& s, std::unordered_map<int, int> h) {\n"
        "    std::priority_queue<int, std::vector<int>, std::greater<int>> pq;\n"
        "    std::vector<int> v;\n"
        "}\n", Language::Cpp);
    REQUIRE(parsed.functions.size() == 1);
    const auto containers = codecomplex::findContainers(parsed.functions[0]);
    CHECK_EQ(containers.size(), std::size_t(3));
    CHECK(containers.at("s") == ContainerKind::Tree);
    CHECK(containers.at("h") == ContainerKind::Hash);
    CHECK(containers.at("pq") == ContainerKind::Heap);
}
