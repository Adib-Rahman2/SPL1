#include "TestFramework.h"

#include "lexer/Lexer.h"
#include "structure/Parser.h"

#include <cstddef>
#include <string>
#include <vector>

using codecomplex::Call;
using codecomplex::Function;
using codecomplex::LoopKind;
using codecomplex::Node;
using codecomplex::NodeKind;
using codecomplex::ParseResult;
using codecomplex::Token;

namespace {

ParseResult parseSource(const std::string& source) {
    const codecomplex::LexResult lexed = codecomplex::tokenize(source);
    REQUIRE(lexed.ok());
    return codecomplex::parse(lexed.tokens);
}

Function onlyFunction(const std::string& source) {
    const ParseResult result = parseSource(source);
    REQUIRE(result.functions.size() == 1);
    return result.functions[0];
}

std::string joined(const std::vector<Token>& tokens) {
    std::string out;
    for (const Token& token : tokens) {
        if (!out.empty()) out += ' ';
        out += token.text;
    }
    return out;
}

void collect(const Node& node, NodeKind kind, std::vector<const Node*>& out) {
    if (node.kind == kind) out.push_back(&node);
    for (const Node& child : node.children) collect(child, kind, out);
}

std::vector<const Node*> nodesOfKind(const Node& root, NodeKind kind) {
    std::vector<const Node*> out;
    collect(root, kind, out);
    return out;
}

void collectCalls(const Node& node, std::vector<Call>& out) {
    out.insert(out.end(), node.calls.begin(), node.calls.end());
    for (const Node& child : node.children) collectCalls(child, out);
}

int countCallsTo(const Node& root, const std::string& callee) {
    std::vector<Call> calls;
    collectCalls(root, calls);
    int count = 0;
    for (const Call& call : calls) {
        if (call.callee == callee) ++count;
    }
    return count;
}

int maxLoopDepth(const Node& node) {
    int deepest = 0;
    for (const Node& child : node.children) {
        const int depth = maxLoopDepth(child);
        if (depth > deepest) deepest = depth;
    }
    return node.kind == NodeKind::Loop ? deepest + 1 : deepest;
}

}

TEST_CASE("parser: empty input gives no functions") {
    const ParseResult result = parseSource("");
    CHECK(result.functions.empty());
    CHECK(result.unsupported.empty());
}

TEST_CASE("parser: a free function with name, parameters and lines") {
    const Function f = onlyFunction(
        "int sum(const std::vector<int>& v, int n) {\n"
        "    return n;\n"
        "}\n");
    CHECK_EQ(f.name, std::string("sum"));
    CHECK(f.className.empty());
    CHECK_EQ(f.line, 1);
    CHECK_EQ(f.endLine, 3);
    REQUIRE(f.params.size() == 2);
    CHECK_EQ(f.params[0].name, std::string("v"));
    CHECK_EQ(f.params[0].type, std::string("const std :: vector < int > &"));
    CHECK_EQ(f.params[1].name, std::string("n"));
    CHECK_EQ(f.params[1].type, std::string("int"));
    CHECK_EQ(f.body.kind, NodeKind::Block);
    REQUIRE(f.body.children.size() == 1);
    CHECK_EQ(joined(f.body.children[0].tokens), std::string("return n"));
}

TEST_CASE("parser: parameters with templates, defaults, arrays and no names") {
    const Function f = onlyFunction(
        "void g(std::map<int, int>& m, int k = 3, int a[], int, void* = nullptr) {}");
    REQUIRE(f.params.size() == 5);
    CHECK_EQ(f.params[0].name, std::string("m"));
    CHECK_EQ(f.params[1].name, std::string("k"));
    CHECK_EQ(f.params[2].name, std::string("a"));
    CHECK_EQ(f.params[3].name, std::string(""));
    CHECK_EQ(f.params[3].type, std::string("int"));
    CHECK_EQ(f.params[4].name, std::string(""));
}

TEST_CASE("parser: (void) and () mean no parameters") {
    const ParseResult result = parseSource("int a(void) { return 1; }\nint b() { return 2; }");
    REQUIRE(result.functions.size() == 2);
    CHECK(result.functions[0].params.empty());
    CHECK(result.functions[1].params.empty());
}

TEST_CASE("parser: declarations, variables and directives are skipped") {
    const ParseResult result = parseSource(
        "#include <vector>\n"
        "using namespace std;\n"
        "int helper(int x);\n"
        "const int N = 100;\n"
        "int table[N] = {1, 2, 3};\n"
        "int total = compute(5);\n"
        "struct Point;\n"
        "typedef long long ll;\n"
        "int main() { return 0; }\n");
    REQUIRE(result.functions.size() == 1);
    CHECK_EQ(result.functions[0].name, std::string("main"));
    CHECK(result.unsupported.empty());
}

TEST_CASE("parser: several functions in order") {
    const ParseResult result = parseSource(
        "void a() {}\n"
        "static inline long long b(int x) { return x; }\n"
        "int main() { a(); return b(1); }\n");
    REQUIRE(result.functions.size() == 3);
    CHECK_EQ(result.functions[0].name, std::string("a"));
    CHECK_EQ(result.functions[1].name, std::string("b"));
    CHECK_EQ(result.functions[2].name, std::string("main"));
}

TEST_CASE("parser: functions inside a namespace") {
    const ParseResult result = parseSource(
        "namespace util {\n"
        "int twice(int x) { return 2 * x; }\n"
        "namespace inner { void f() {} }\n"
        "}\n");
    REQUIRE(result.functions.size() == 2);
    CHECK_EQ(result.functions[0].name, std::string("twice"));
    CHECK_EQ(result.functions[1].name, std::string("f"));
}

TEST_CASE("parser: class members, constructors, destructors and operators") {
    const ParseResult result = parseSource(
        "class Stack {\n"
        "public:\n"
        "    Stack() : top_(0), data_{} {}\n"
        "    ~Stack() {}\n"
        "    void push(int x);\n"
        "    int size() const { return top_; }\n"
        "    bool operator<(const Stack& o) const { return top_ < o.top_; }\n"
        "    virtual void draw() = 0;\n"
        "    Stack(const Stack&) = delete;\n"
        "private:\n"
        "    int top_;\n"
        "    int data_[10];\n"
        "};\n"
        "void Stack::push(int x) { data_[top_++] = x; }\n");
    REQUIRE(result.functions.size() == 5);
    CHECK_EQ(result.functions[0].name, std::string("Stack"));
    CHECK_EQ(result.functions[0].className, std::string("Stack"));
    CHECK_EQ(result.functions[1].name, std::string("~Stack"));
    CHECK_EQ(result.functions[2].name, std::string("size"));
    CHECK_EQ(result.functions[3].name, std::string("operator<"));
    CHECK_EQ(result.functions[4].name, std::string("push"));
    CHECK_EQ(result.functions[4].className, std::string("Stack"));
    CHECK_EQ(result.functions[4].line, 14);
}

TEST_CASE("parser: struct members and a nested struct") {
    const ParseResult result = parseSource(
        "struct Graph {\n"
        "    struct Edge { int to; int w; int other() const { return to; } };\n"
        "    void addEdge(int a, int b) { }\n"
        "} g;\n"
        "int main() { return 0; }\n");
    REQUIRE(result.functions.size() == 3);
    CHECK_EQ(result.functions[0].name, std::string("other"));
    CHECK_EQ(result.functions[0].className, std::string("Edge"));
    CHECK_EQ(result.functions[1].name, std::string("addEdge"));
    CHECK_EQ(result.functions[1].className, std::string("Graph"));
    CHECK_EQ(result.functions[2].name, std::string("main"));
}

TEST_CASE("parser: an enum and a trailing return type") {
    const ParseResult result = parseSource(
        "enum class Color { Red, Green };\n"
        "auto area(int w, int h) -> long { return w * h; }\n");
    REQUIRE(result.functions.size() == 1);
    CHECK_EQ(result.functions[0].name, std::string("area"));
}

TEST_CASE("parser: for loop header keeps init, condition and step tokens") {
    const Function f = onlyFunction(
        "void f(int n) {\n"
        "    for (int i = 0; i < n; i += 2) {\n"
        "        work(i);\n"
        "    }\n"
        "}\n");
    REQUIRE(f.body.children.size() == 1);
    const Node& loop = f.body.children[0];
    CHECK_EQ(loop.kind, NodeKind::Loop);
    CHECK_EQ(loop.loopKind, LoopKind::For);
    CHECK_EQ(loop.line, 2);
    CHECK_EQ(joined(loop.init), std::string("int i = 0"));
    CHECK_EQ(joined(loop.cond), std::string("i < n"));
    CHECK_EQ(joined(loop.step), std::string("i += 2"));
    REQUIRE(loop.children.size() == 1);
    CHECK_EQ(loop.children[0].kind, NodeKind::Block);
    CHECK_EQ(countCallsTo(loop, "work"), 1);
}

TEST_CASE("parser: for loop with empty header parts") {
    const Function f = onlyFunction("void f() { for (;;) { break; } }");
    const Node& loop = f.body.children[0];
    CHECK_EQ(loop.loopKind, LoopKind::For);
    CHECK(loop.init.empty());
    CHECK(loop.cond.empty());
    CHECK(loop.step.empty());
}

TEST_CASE("parser: range-for keeps the variable and the range") {
    const Function f = onlyFunction(
        "void f(const std::vector<int>& v) { for (const auto& [a, b] : pairs(v)) use(a); }");
    const Node& loop = f.body.children[0];
    CHECK_EQ(loop.loopKind, LoopKind::RangeFor);
    CHECK_EQ(joined(loop.init), std::string("const auto & [ a , b ]"));
    CHECK_EQ(joined(loop.cond), std::string("pairs ( v )"));
    CHECK(loop.step.empty());
    REQUIRE(loop.calls.size() == 1);
    CHECK_EQ(loop.calls[0].callee, std::string("pairs"));
}

TEST_CASE("parser: a ternary in a for condition is not a range-for") {
    const Function f = onlyFunction("void f(int n) { for (int i = 0; i < (n > 5 ? 5 : n); i++) {} }");
    CHECK_EQ(f.body.children[0].loopKind, LoopKind::For);
}

TEST_CASE("parser: while and do-while loops") {
    const Function f = onlyFunction(
        "void f(int n) {\n"
        "    while (n > 1) { n /= 2; }\n"
        "    do { n++; } while (n < 10);\n"
        "}\n");
    REQUIRE(f.body.children.size() == 2);
    const Node& w = f.body.children[0];
    CHECK_EQ(w.loopKind, LoopKind::While);
    CHECK_EQ(joined(w.cond), std::string("n > 1"));
    const Node& d = f.body.children[1];
    CHECK_EQ(d.kind, NodeKind::Loop);
    CHECK_EQ(d.loopKind, LoopKind::DoWhile);
    CHECK_EQ(joined(d.cond), std::string("n < 10"));
    REQUIRE(d.children.size() == 1);
    CHECK_EQ(d.children[0].kind, NodeKind::Block);
}

TEST_CASE("parser: sequential loops are siblings, nested loops are children") {
    const Function seq = onlyFunction(
        "void f(int n) { for (int i = 0; i < n; i++) {} for (int j = 0; j < n; j++) {} }");
    CHECK_EQ(seq.body.children.size(), std::size_t{2});
    CHECK_EQ(maxLoopDepth(seq.body), 1);

    const Function nested = onlyFunction(
        "void f(int n) {\n"
        "  for (int i = 0; i < n; i++)\n"
        "    for (int j = 0; j < n; j++)\n"
        "      for (int k = 0; k < n; k++) { x++; }\n"
        "}\n");
    CHECK_EQ(nested.body.children.size(), std::size_t{1});
    CHECK_EQ(maxLoopDepth(nested.body), 3);
    CHECK_EQ(nodesOfKind(nested.body, NodeKind::Loop).size(), std::size_t{3});
}

TEST_CASE("parser: braceless bodies take exactly one statement") {
    const Function f = onlyFunction(
        "void f(int n) {\n"
        "    for (int i = 0; i < n; i++)\n"
        "        a(i);\n"
        "    b();\n"
        "    while (n--) ;\n"
        "}\n");
    REQUIRE(f.body.children.size() == 3);
    const Node& loop = f.body.children[0];
    REQUIRE(loop.children.size() == 1);
    CHECK_EQ(loop.children[0].kind, NodeKind::Statement);
    CHECK_EQ(countCallsTo(loop, "a"), 1);
    CHECK_EQ(countCallsTo(loop, "b"), 0);
    CHECK_EQ(f.body.children[1].kind, NodeKind::Statement);
    const Node& empty = f.body.children[2];
    CHECK_EQ(empty.loopKind, LoopKind::While);
    REQUIRE(empty.children.size() == 1);
    CHECK(empty.children[0].tokens.empty());
}

TEST_CASE("parser: if / else if / else becomes one If with three branches") {
    const Function f = onlyFunction(
        "int sign(int x) {\n"
        "    if (x > 0) return 1;\n"
        "    else if (x < 0) { return -1; }\n"
        "    else return 0;\n"
        "}\n");
    REQUIRE(f.body.children.size() == 1);
    const Node& branchNode = f.body.children[0];
    CHECK_EQ(branchNode.kind, NodeKind::If);
    REQUIRE(branchNode.children.size() == 3);
    CHECK_EQ(branchNode.children[0].kind, NodeKind::Branch);
    CHECK_EQ(joined(branchNode.children[0].cond), std::string("x > 0"));
    CHECK_EQ(joined(branchNode.children[1].cond), std::string("x < 0"));
    CHECK_EQ(branchNode.children[1].line, 3);
    CHECK(branchNode.children[2].cond.empty());
    CHECK_EQ(branchNode.children[2].children[0].kind, NodeKind::Statement);
}

TEST_CASE("parser: dangling else belongs to the nearest if") {
    const Function f = onlyFunction("void f(int a, int b) { if (a) if (b) x(); else y(); }");
    const Node& outer = f.body.children[0];
    REQUIRE(outer.children.size() == 1);
    const Node& inner = outer.children[0].children[0];
    CHECK_EQ(inner.kind, NodeKind::If);
    CHECK_EQ(inner.children.size(), std::size_t{2});
}

TEST_CASE("parser: switch with case labels") {
    const Function f = onlyFunction(
        "void f(int op) {\n"
        "    switch (op) {\n"
        "    case 1: a(); break;\n"
        "    case Kind::B: { b(); break; }\n"
        "    default: c();\n"
        "    }\n"
        "}\n");
    const Node& sw = f.body.children[0];
    CHECK_EQ(sw.kind, NodeKind::Switch);
    CHECK_EQ(joined(sw.cond), std::string("op"));
    REQUIRE(sw.children.size() == 1);
    const Node& body = sw.children[0];
    REQUIRE(body.children.size() == 7);
    CHECK_EQ(joined(body.children[0].tokens), std::string("case 1 :"));
    CHECK_EQ(joined(body.children[3].tokens), std::string("case Kind :: B :"));
    CHECK_EQ(body.children[4].kind, NodeKind::Block);
    CHECK_EQ(joined(body.children[5].tokens), std::string("default :"));
    CHECK_EQ(countCallsTo(sw, "c"), 1);
}

TEST_CASE("parser: loops inside branches inside loops") {
    const Function f = onlyFunction(
        "void f(int n) {\n"
        "  for (int i = 0; i < n; i++) {\n"
        "    if (i % 2 == 0) { for (int j = 0; j < n; j++) g(); }\n"
        "    else while (n) n--;\n"
        "  }\n"
        "}\n");
    CHECK_EQ(maxLoopDepth(f.body), 2);
    CHECK_EQ(nodesOfKind(f.body, NodeKind::Loop).size(), std::size_t{3});
    CHECK_EQ(nodesOfKind(f.body, NodeKind::Branch).size(), std::size_t{2});
}

TEST_CASE("parser: calls with arguments, members and qualified names") {
    const std::vector<Call> calls =
        codecomplex::findCalls(codecomplex::tokenize("std::sort(v.begin(), v.end()); a[i].size(); this->go(f(1, g(2)), 3)").tokens);
    REQUIRE(calls.size() == 7);
    CHECK_EQ(calls[0].callee, std::string("std::sort"));
    REQUIRE(calls[0].args.size() == 2);
    CHECK_EQ(joined(calls[0].args[0]), std::string("v . begin ( )"));
    CHECK_EQ(calls[1].callee, std::string("v.begin"));
    CHECK(calls[1].args.empty());
    CHECK_EQ(calls[2].callee, std::string("v.end"));
    CHECK_EQ(calls[3].callee, std::string(".size"));
    CHECK_EQ(calls[4].callee, std::string("this->go"));
    CHECK_EQ(calls[4].args.size(), std::size_t{2});
    CHECK_EQ(calls[5].callee, std::string("f"));
    CHECK_EQ(calls[6].callee, std::string("g"));
}

TEST_CASE("parser: keywords, casts and variable declarations are not calls") {
    const std::vector<Call> calls = codecomplex::findCalls(codecomplex::tokenize(
        "int x(5); std::vector<int> v(n); sizeof(x); static_cast<int>(y); "
        "return h(x); if (k(1)) {} a > m(2); new Node(3)").tokens);
    REQUIRE(calls.size() == 3);
    CHECK_EQ(calls[0].callee, std::string("h"));
    CHECK_EQ(calls[1].callee, std::string("k"));
    CHECK_EQ(calls[2].callee, std::string("m"));
}

TEST_CASE("parser: recursion is visible as calls to the function's own name") {
    const Function fib = onlyFunction(
        "long long fib(int n) {\n"
        "    if (n < 2) return n;\n"
        "    return fib(n - 1) + fib(n - 2);\n"
        "}\n");
    CHECK_EQ(countCallsTo(fib.body, fib.name), 2);

    const Function search = onlyFunction(
        "int bs(const std::vector<int>& a, int lo, int hi, int x) {\n"
        "    if (lo > hi) return -1;\n"
        "    int mid = (lo + hi) / 2;\n"
        "    if (a[mid] == x) return mid;\n"
        "    else if (a[mid] < x) return bs(a, mid + 1, hi, x);\n"
        "    else return bs(a, lo, mid - 1, x);\n"
        "}\n");
    CHECK_EQ(countCallsTo(search.body, "bs"), 2);
    const std::vector<const Node*> ifs = nodesOfKind(search.body, NodeKind::If);
    REQUIRE(ifs.size() == 2);
    REQUIRE(ifs[1]->children.size() == 3);
    CHECK_EQ(countCallsTo(ifs[1]->children[1], "bs"), 1);
    CHECK_EQ(countCallsTo(ifs[1]->children[2], "bs"), 1);
}

TEST_CASE("parser: calls in loop headers and conditions are recorded") {
    const Function f = onlyFunction(
        "void f(std::vector<int>& v) {\n"
        "    for (size_t i = 0; i < v.size(); i++) {}\n"
        "    while (more()) {}\n"
        "    if (check(v)) {}\n"
        "}\n");
    CHECK_EQ(f.body.children[0].calls.size(), std::size_t{1});
    CHECK_EQ(f.body.children[0].calls[0].callee, std::string("v.size"));
    CHECK_EQ(f.body.children[0].calls[0].line, 2);
    CHECK_EQ(f.body.children[1].calls[0].callee, std::string("more"));
    CHECK_EQ(f.body.children[2].children[0].calls[0].callee, std::string("check"));
}

TEST_CASE("parser: a template function is recorded as unsupported") {
    const ParseResult result = parseSource(
        "template <typename T>\n"
        "T biggest(const std::vector<T>& v) { return v[0]; }\n"
        "int main() { return 0; }\n");
    REQUIRE(result.functions.size() == 1);
    CHECK_EQ(result.functions[0].name, std::string("main"));
    REQUIRE(result.unsupported.size() == 1);
    CHECK_EQ(result.unsupported[0].kind, NodeKind::Unsupported);
    CHECK_EQ(result.unsupported[0].line, 1);
    CHECK(result.unsupported[0].reason.find("template") != std::string::npos);
}

TEST_CASE("parser: a lambda inside a function is unsupported but the rest is parsed") {
    const Function f = onlyFunction(
        "void f(std::vector<int>& v) {\n"
        "    std::sort(v.begin(), v.end(), [](int a, int b) { return a > b; });\n"
        "    for (int x : v) use(x);\n"
        "}\n");
    REQUIRE(f.body.children.size() == 2);
    const Node& lambda = f.body.children[0];
    CHECK_EQ(lambda.kind, NodeKind::Unsupported);
    CHECK_EQ(lambda.line, 2);
    CHECK(lambda.reason.find("lambda") != std::string::npos);
    CHECK_EQ(f.body.children[1].loopKind, LoopKind::RangeFor);
}

TEST_CASE("parser: a macro used as a loop is unsupported") {
    const Function f = onlyFunction(
        "void f(int n) {\n"
        "    FOR(i, 0, n) { work(i); }\n"
        "    rep(j, n) work(j);\n"
        "    done();\n"
        "}\n");
    REQUIRE(f.body.children.size() == 3);
    CHECK_EQ(f.body.children[0].kind, NodeKind::Unsupported);
    CHECK(f.body.children[0].reason.find("FOR") != std::string::npos);
    CHECK_EQ(f.body.children[1].kind, NodeKind::Unsupported);
    CHECK_EQ(f.body.children[1].line, 3);
    CHECK_EQ(f.body.children[2].kind, NodeKind::Statement);
}

TEST_CASE("parser: try/catch is a block of the try body and its handlers") {
    const Function f = onlyFunction(
        "void f() {\n"
        "    try { a(); } catch (const std::exception& e) { b(); } catch (...) {}\n"
        "    c();\n"
        "}\n");
    REQUIRE(f.body.children.size() == 2);
    const Node& tryBlock = f.body.children[0];
    CHECK_EQ(tryBlock.kind, NodeKind::Block);
    REQUIRE(tryBlock.children.size() == 3);
    REQUIRE(tryBlock.children[0].children.size() == 1);
    CHECK_EQ(tryBlock.children[0].children[0].calls[0].callee, std::string("a"));
    CHECK_EQ(tryBlock.children[1].children[0].calls[0].callee, std::string("b"));
    CHECK(tryBlock.children[2].children.empty());
    CHECK_EQ(f.body.children[1].kind, NodeKind::Statement);
}

TEST_CASE("parser: local types are unsupported") {
    const Function f = onlyFunction(
        "void f() {\n"
        "    struct Local { int x; };\n"
        "    c();\n"
        "}\n");
    REQUIRE(f.body.children.size() == 2);
    CHECK_EQ(f.body.children[0].kind, NodeKind::Unsupported);
    CHECK_EQ(f.body.children[0].line, 2);
    CHECK_EQ(f.body.children[1].kind, NodeKind::Statement);
}

TEST_CASE("parser: a lambda at file scope is unsupported") {
    const ParseResult result = parseSource("auto add = [](int a, int b) { return a + b; };\nvoid g() {}");
    REQUIRE(result.functions.size() == 1);
    REQUIRE(result.unsupported.size() == 1);
    CHECK(result.unsupported[0].reason.find("lambda") != std::string::npos);
}

TEST_CASE("parser: a missing closing brace is reported, not a crash") {
    const ParseResult result = parseSource("void f(int n) {\n  for (int i = 0; i < n; i++) {\n    x++;\n");
    REQUIRE(result.functions.size() == 1);
    const std::vector<const Node*> problems =
        nodesOfKind(result.functions[0].body, NodeKind::Unsupported);
    REQUIRE(!problems.empty());
    CHECK(problems[0]->reason.find("'}'") != std::string::npos);
}

TEST_CASE("parser: broken input never crashes and reaches the end") {
    const std::vector<std::string> inputs = {
        "}", ")", "void f(", "void f() { for (", "void f() { for (int i = 0; i < n",
        "void f() { if", "void f() { do x++;", "void f() { switch (x) { case 1",
        "class A {", "namespace {", "else", "int f() { else x; }", "template <",
        "void f() { x = (1; }", "void f() { case }", "struct S { void g() { }",
    };
    for (const std::string& input : inputs) {
        const codecomplex::LexResult lexed = codecomplex::tokenize(input);
        REQUIRE(lexed.ok());
        codecomplex::parse(lexed.tokens);
    }
    CHECK(true);
}

TEST_CASE("parser: an else without an if is unsupported") {
    const Function f = onlyFunction("int f() { else x = 1; return 0; }");
    REQUIRE(!f.body.children.empty());
    CHECK_EQ(f.body.children[0].kind, NodeKind::Unsupported);
    CHECK(f.body.children[0].reason.find("else") != std::string::npos);
}

namespace {

ParseResult parseJava(const std::string& source) {
    const codecomplex::LexResult lexed = codecomplex::tokenize(source, codecomplex::Language::Java);
    REQUIRE(lexed.ok());
    return codecomplex::parse(lexed.tokens, codecomplex::Language::Java);
}

}

TEST_CASE("parser: Java class with modifiers, throws and no ';' after the class") {
    const ParseResult result = parseJava(
        "import java.util.*;\n"
        "public class Main {\n"
        "    static final int N = 3;\n"
        "    public static void main(String[] args) throws IOException, Exception {\n"
        "        for (int i = 0; i < args.length; i++) work(i);\n"
        "    }\n"
        "    private static <T> int count(List<T> xs) { return xs.size(); }\n"
        "}\n"
        "interface Shape { double area(); }\n"
        "class Other { void g() {} }\n");
    REQUIRE(result.functions.size() == 3);
    CHECK_EQ(result.functions[0].name, std::string("main"));
    CHECK_EQ(result.functions[0].className, std::string("Main"));
    REQUIRE(result.functions[0].body.children.size() == 1);
    CHECK_EQ(result.functions[0].body.children[0].kind, NodeKind::Loop);
    CHECK_EQ(result.functions[1].name, std::string("count"));
    CHECK_EQ(result.functions[2].name, std::string("g"));
    CHECK_EQ(result.functions[2].className, std::string("Other"));
    CHECK(result.unsupported.empty());
}

TEST_CASE("parser: Java enum body needs no ';'") {
    const ParseResult result = parseJava("enum Color { RED, GREEN }\nclass A { void f() {} }\n");
    REQUIRE(result.functions.size() == 1);
    CHECK_EQ(result.functions[0].name, std::string("f"));
}

TEST_CASE("parser: Java lambdas and anonymous classes are unsupported") {
    const Function f = parseJava(
        "class A { void f(List<Integer> v) {\n"
        "    v.sort((a, b) -> a - b);\n"
        "    Collections.sort(v, new Comparator<Integer>() {\n"
        "        public int compare(Integer a, Integer b) { return a - b; }\n"
        "    });\n"
        "    done();\n"
        "} }\n").functions.at(0);
    REQUIRE(f.body.children.size() == 3);
    CHECK_EQ(f.body.children[0].kind, NodeKind::Unsupported);
    CHECK(f.body.children[0].reason.find("lambda") != std::string::npos);
    CHECK_EQ(f.body.children[1].kind, NodeKind::Unsupported);
    CHECK(f.body.children[1].reason.find("anonymous") != std::string::npos);
    CHECK_EQ(f.body.children[2].kind, NodeKind::Statement);
}

TEST_CASE("parser: Java try-with-resources and finally") {
    const Function f = parseJava(
        "class A { void f() {\n"
        "    try (Reader r = open()) { a(); } catch (IOException e) { b(); } finally { c(); }\n"
        "} }\n").functions.at(0);
    REQUIRE(f.body.children.size() == 1);
    CHECK_EQ(f.body.children[0].kind, NodeKind::Block);
    CHECK_EQ(f.body.children[0].children.size(), std::size_t(3));
}
