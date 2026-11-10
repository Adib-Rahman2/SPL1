#pragma once

#include "analysis/Complexity.h"
#include "structure/Ast.h"

#include <map>
#include <string>
#include <vector>

namespace codecomplex {

enum class ContainerKind {
    Tree,
    Heap,
    Hash
};

struct LibraryCost {
    bool found = false;
    Complexity cost;
    std::string what;
    bool tracedToInput = false;
};

std::map<std::string, ContainerKind> findContainers(const Function& function);

bool isSortCall(const std::string& callee);

LibraryCost libraryCost(const Call& call, const std::map<std::string, ContainerKind>& containers,
                        const std::vector<std::string>& inputs);

}
