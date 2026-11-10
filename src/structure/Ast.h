#pragma once

#include "lexer/Token.h"

#include <ostream>
#include <string>
#include <vector>

namespace codecomplex {

enum class NodeKind {
    Block,
    Statement,
    Loop,
    If,
    Branch,
    Switch,
    Unsupported
};

enum class LoopKind {
    For,
    RangeFor,
    While,
    DoWhile
};

inline const char* nodeKindName(NodeKind kind) {
    switch (kind) {
    case NodeKind::Block:       return "Block";
    case NodeKind::Statement:   return "Statement";
    case NodeKind::Loop:        return "Loop";
    case NodeKind::If:          return "If";
    case NodeKind::Branch:      return "Branch";
    case NodeKind::Switch:      return "Switch";
    case NodeKind::Unsupported: return "Unsupported";
    }
    return "?";
}

inline const char* loopKindName(LoopKind kind) {
    switch (kind) {
    case LoopKind::For:      return "For";
    case LoopKind::RangeFor: return "RangeFor";
    case LoopKind::While:    return "While";
    case LoopKind::DoWhile:  return "DoWhile";
    }
    return "?";
}

inline std::ostream& operator<<(std::ostream& out, NodeKind kind) { return out << nodeKindName(kind); }
inline std::ostream& operator<<(std::ostream& out, LoopKind kind) { return out << loopKindName(kind); }

struct Call {
    std::string callee;
    std::vector<std::vector<Token>> args;
    int line = 0;
};

struct Node {
    NodeKind kind = NodeKind::Statement;
    int line = 0;

    std::vector<Token> tokens;

    LoopKind loopKind = LoopKind::For;
    std::vector<Token> init, cond, step;

    std::vector<Call> calls;
    std::string reason;

    std::vector<Node> children;
};

struct Param {
    std::string type;
    std::string name;
};

struct Function {
    std::string name;
    std::string className;
    std::vector<Param> params;
    Node body;
    int line = 0;
    int endLine = 0;
};

struct ParseResult {
    std::vector<Function> functions;
    std::vector<Node> unsupported;
};

}
