#pragma once

#include "report/FunctionResult.h"

#include <ostream>
#include <string>
#include <vector>

namespace codecomplex {

std::string jsonString(const std::string& text);

void writeJson(std::ostream& out, const std::string& file, const std::vector<FunctionResult>& results);

bool writeJsonFile(const std::string& path, const std::string& file,
                   const std::vector<FunctionResult>& results, std::string& error);

}
