#include "analysis/Complexity.h"

namespace codecomplex {

Complexity Complexity::polynomial(int power, int logPower) {
    Complexity c;
    c.power = power;
    c.logPower = logPower;
    return c;
}

Complexity Complexity::exponential(int base) {
    Complexity c;
    c.kind = Kind::Exponential;
    c.base = base;
    return c;
}

Complexity Complexity::factorial() {
    Complexity c;
    c.kind = Kind::Factorial;
    return c;
}

Complexity Complexity::unknown() {
    Complexity c;
    c.kind = Kind::Unknown;
    return c;
}

std::string Complexity::notation() const {
    switch (kind) {
    case Kind::Unknown:     return "Unknown";
    case Kind::Factorial:   return "O(n!)";
    case Kind::Exponential: return "O(" + std::to_string(base) + "^n)";
    case Kind::Polynomial:  break;
    }

    std::string inner;
    if (power == 1) inner = "n";
    if (power > 1) inner = "n^" + std::to_string(power);
    if (logPower > 0) {
        if (!inner.empty()) inner += ' ';
        inner += logPower == 1 ? "log n" : "log^" + std::to_string(logPower) + " n";
    }
    if (inner.empty()) inner = "1";
    return "O(" + inner + ")";
}

std::string Complexity::toString() const {
    std::string text = notation();
    if (upperBound) text += " (upper bound)";
    if (uncertain) text += " (uncertain)";
    return text;
}

namespace {

int kindRank(Complexity::Kind kind) {
    switch (kind) {
    case Complexity::Kind::Polynomial:  return 0;
    case Complexity::Kind::Exponential: return 1;
    case Complexity::Kind::Factorial:   return 2;
    case Complexity::Kind::Unknown:     return 3;
    }
    return 3;
}

int compareInts(int a, int b) { return a < b ? -1 : (a > b ? 1 : 0); }

}

int compareGrowth(const Complexity& a, const Complexity& b) {
    if (a.kind != b.kind) return compareInts(kindRank(a.kind), kindRank(b.kind));
    if (a.kind == Complexity::Kind::Exponential) return compareInts(a.base, b.base);
    if (a.kind == Complexity::Kind::Polynomial) {
        if (a.power != b.power) return compareInts(a.power, b.power);
        return compareInts(a.logPower, b.logPower);
    }
    return 0;
}

Complexity multiply(const Complexity& a, const Complexity& b) {
    if (a.isUnknown() || b.isUnknown()) return Complexity::unknown();

    Complexity result;
    if (a.isConstant()) {
        result = b;
    } else if (b.isConstant()) {
        result = a;
    } else if (a.kind == Complexity::Kind::Polynomial && b.kind == Complexity::Kind::Polynomial) {
        result = Complexity::polynomial(a.power + b.power, a.logPower + b.logPower);
    } else if (a.kind == Complexity::Kind::Exponential && b.kind == Complexity::Kind::Exponential) {
        result = Complexity::exponential(a.base * b.base);
    } else {
        result = compareGrowth(a, b) >= 0 ? a : b;
        result.uncertain = true;
    }
    result.upperBound = a.upperBound || b.upperBound;
    result.uncertain = result.uncertain || a.uncertain || b.uncertain;
    return result;
}

Complexity add(const Complexity& a, const Complexity& b) {
    if (a.isUnknown() || b.isUnknown()) return Complexity::unknown();

    const int order = compareGrowth(a, b);
    Complexity result = order >= 0 ? a : b;
    if (order == 0) result.upperBound = a.upperBound && b.upperBound;
    result.uncertain = a.uncertain || b.uncertain;
    return result;
}

bool operator==(const Complexity& a, const Complexity& b) {
    return a.kind == b.kind && a.power == b.power && a.logPower == b.logPower &&
           a.base == b.base && a.upperBound == b.upperBound && a.uncertain == b.uncertain;
}

}
