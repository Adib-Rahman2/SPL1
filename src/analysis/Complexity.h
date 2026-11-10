#pragma once

#include <ostream>
#include <string>

namespace codecomplex {

struct Complexity {
    enum class Kind { Polynomial, Exponential, Factorial, Unknown };

    Kind kind = Kind::Polynomial;
    int power = 0;
    int logPower = 0;
    int base = 2;
    bool upperBound = false;
    bool uncertain = false;

    static Complexity constant() { return Complexity(); }
    static Complexity logarithmic() { return polynomial(0, 1); }
    static Complexity linear() { return polynomial(1, 0); }
    static Complexity linearithmic() { return polynomial(1, 1); }
    static Complexity polynomial(int power, int logPower = 0);
    static Complexity exponential(int base = 2);
    static Complexity factorial();
    static Complexity unknown();

    bool isConstant() const { return kind == Kind::Polynomial && power == 0 && logPower == 0; }
    bool isUnknown() const { return kind == Kind::Unknown; }

    std::string notation() const;
    std::string toString() const;
};

int compareGrowth(const Complexity& a, const Complexity& b);

Complexity multiply(const Complexity& a, const Complexity& b);

Complexity add(const Complexity& a, const Complexity& b);

bool operator==(const Complexity& a, const Complexity& b);
inline bool operator!=(const Complexity& a, const Complexity& b) { return !(a == b); }

inline std::ostream& operator<<(std::ostream& out, const Complexity& c) { return out << c.toString(); }

}
