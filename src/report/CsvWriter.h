#pragma once

#include "report/FunctionResult.h"

#include <ostream>
#include <string>
#include <vector>

namespace codecomplex {

std::string csvField(const std::string& text);

std::vector<std::string> csvHeader();
std::vector<std::string> csvRow(const FunctionResult& result);

void writeCsv(std::ostream& out, const std::vector<FunctionResult>& results);

bool writeCsvFile(const std::string& path, const std::vector<FunctionResult>& results,
                  std::string& error);

}
