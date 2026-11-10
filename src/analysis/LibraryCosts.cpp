#include "analysis/LibraryCosts.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace codecomplex {

namespace {

bool isNameToken(const Token& token) { return token.kind == TokenKind::Identifier; }

bool contains(const std::set<std::string>& words, const std::string& word) {
    return words.count(word) > 0;
}

bool containerType(const std::string& word, ContainerKind& kind) {
    static const std::set<std::string> tree = {"map", "set", "multimap", "multiset",
                                               "TreeMap", "TreeSet"};
    static const std::set<std::string> heap = {"priority_queue", "PriorityQueue"};
    static const std::set<std::string> hash = {
        "unordered_map", "unordered_set", "unordered_multimap", "unordered_multiset",
        "HashMap", "HashSet", "LinkedHashMap", "LinkedHashSet", "Hashtable"};
    if (contains(tree, word)) kind = ContainerKind::Tree;
    else if (contains(heap, word)) kind = ContainerKind::Heap;
    else if (contains(hash, word)) kind = ContainerKind::Hash;
    else return false;
    return true;
}

std::size_t skipAngles(const std::vector<Token>& tokens, std::size_t i) {
    int depth = 0;
    for (; i < tokens.size(); ++i) {
        if (tokens[i].text == "<") ++depth;
        if (tokens[i].text == ">") --depth;
        if (tokens[i].text == ">>") depth -= 2;
        if (depth <= 0) return i + 1;
    }
    return i;
}

void declarationIn(const std::vector<Token>& tokens,
                   std::map<std::string, ContainerKind>& containers) {
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string& text = tokens[i].text;
        if (text == "=" || text == "(") break;
        ContainerKind kind;
        if (!containerType(text, kind)) continue;
        std::size_t j = i + 1;
        if (j < tokens.size() && tokens[j].text == "<") j = skipAngles(tokens, j);
        while (j < tokens.size() && (tokens[j].text == "&" || tokens[j].text == "*")) ++j;
        if (j < tokens.size() && isNameToken(tokens[j])) containers[tokens[j].text] = kind;
        return;
    }
    for (std::size_t i = 0; i + 2 < tokens.size(); ++i) {
        ContainerKind kind;
        if (tokens[i + 1].text == "=" && tokens[i + 2].text == "new" && isNameToken(tokens[i]) &&
            i + 3 < tokens.size() && containerType(tokens[i + 3].text, kind)) {
            containers[tokens[i].text] = kind;
            return;
        }
    }
}

void findIn(const Node& node, std::map<std::string, ContainerKind>& containers) {
    if (node.kind == NodeKind::Statement) declarationIn(node.tokens, containers);
    if (node.kind == NodeKind::Loop) declarationIn(node.init, containers);
    for (const Node& child : node.children) findIn(child, containers);
}

void splitCallee(const std::string& callee, std::string& qualifier, std::string& base) {
    const std::size_t cut = callee.find_last_of(".:>");
    if (cut == std::string::npos) {
        qualifier.clear();
        base = callee;
        return;
    }
    base = callee.substr(cut + 1);
    std::size_t end = cut;
    if (cut > 0 && ((callee[cut] == ':' && callee[cut - 1] == ':') ||
                    (callee[cut] == '>' && callee[cut - 1] == '-'))) {
        end = cut - 1;
    }
    qualifier = callee.substr(0, end);
}

bool mentionsInput(const Call& call, const std::vector<std::string>& inputs) {
    for (const std::vector<Token>& arg : call.args) {
        for (const Token& token : arg) {
            if (isNameToken(token) &&
                std::find(inputs.begin(), inputs.end(), token.text) != inputs.end()) {
                return true;
            }
        }
    }
    return false;
}

bool isConstantTimeCall(const std::string& base) {
    static const std::set<std::string> names = {
        "size", "length", "empty", "push_back", "emplace_back", "pop_back", "front", "back",
        "begin", "end", "swap", "abs", "min", "max", "printf", "scanf", "putchar", "getchar",
        "sqrt", "pow", "floor", "ceil",
        "println", "print", "nextInt", "nextLong", "nextDouble", "next", "nextLine", "hasNext",
        "charAt", "parseInt", "parseLong", "isEmpty", "close"};
    return contains(names, base);
}

LibraryCost found(const Complexity& cost, const std::string& what, bool traced) {
    LibraryCost result;
    result.found = true;
    result.cost = cost;
    result.what = what;
    result.tracedToInput = traced;
    return result;
}

}

bool isSortCall(const std::string& callee) {
    std::string qualifier, base;
    splitCallee(callee, qualifier, base);
    return base == "sort" || base == "stable_sort";
}

std::map<std::string, ContainerKind> findContainers(const Function& function) {
    std::map<std::string, ContainerKind> containers;
    for (const Param& param : function.params) {
        std::string word;
        for (std::size_t i = 0; i <= param.type.size(); ++i) {
            const char c = i < param.type.size() ? param.type[i] : ' ';
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                word += c;
                continue;
            }
            ContainerKind kind;
            if (!word.empty() && !param.name.empty() && containerType(word, kind)) {
                containers[param.name] = kind;
                break;
            }
            word.clear();
        }
    }
    findIn(function.body, containers);
    return containers;
}

LibraryCost libraryCost(const Call& call, const std::map<std::string, ContainerKind>& containers,
                        const std::vector<std::string>& inputs) {
    std::string qualifier, base;
    splitCallee(call.callee, qualifier, base);
    const bool traced = mentionsInput(call, inputs);

    const auto container = containers.find(qualifier);
    if (container != containers.end()) {
        static const std::set<std::string> treeOps = {
            "insert", "emplace", "erase", "find", "count", "lower_bound", "upper_bound", "put",
            "get", "getOrDefault", "putIfAbsent", "containsKey", "contains", "add", "remove",
            "floorKey", "ceilingKey", "higherKey", "lowerKey", "floor", "ceiling", "higher",
            "lower", "firstKey", "lastKey", "pollFirst", "pollLast", "merge", "at"};
        static const std::set<std::string> heapOps = {"push", "pop", "emplace", "add",
                                                      "offer", "poll", "remove"};
        const bool sizeIsInput =
            std::find(inputs.begin(), inputs.end(), qualifier) != inputs.end();
        switch (container->second) {
        case ContainerKind::Tree:
            if (contains(treeOps, base)) {
                return found(Complexity::logarithmic(), "a balanced-tree (map/set) operation",
                             sizeIsInput);
            }
            break;
        case ContainerKind::Heap:
            if (contains(heapOps, base)) {
                return found(Complexity::logarithmic(), "a heap (priority queue) operation",
                             sizeIsInput);
            }
            break;
        case ContainerKind::Hash:
            if (contains(treeOps, base)) {
                return found(Complexity::constant(), "a hash-table operation (average case)", true);
            }
            break;
        }
    }

    const bool stdOrFree = qualifier.empty() || qualifier == "std";
    if (isSortCall(call.callee)) {
        return found(Complexity::linearithmic(), "sorting", traced);
    }
    static const std::set<std::string> linearFree = {
        "find", "count", "accumulate", "reverse", "fill", "copy", "min_element",
        "max_element", "memset", "memcpy", "strlen", "iota"};
    static const std::set<std::string> linearJava = {
        "Arrays.fill", "Arrays.copyOf", "Arrays.copyOfRange", "Arrays.toString",
        "System.arraycopy", "Collections.reverse", "Collections.max", "Collections.min"};
    if ((stdOrFree && contains(linearFree, base)) || contains(linearJava, call.callee)) {
        return found(Complexity::linear(), "a pass over a whole range", traced);
    }
    static const std::set<std::string> searchFree = {"lower_bound", "upper_bound",
                                                     "binary_search", "equal_range"};
    if ((stdOrFree && contains(searchFree, base)) || call.callee == "Arrays.binarySearch" ||
        call.callee == "Collections.binarySearch") {
        return found(Complexity::logarithmic(), "binary search", traced);
    }
    if (isConstantTimeCall(base)) return found(Complexity::constant(), "constant time", true);
    return LibraryCost();
}

}
