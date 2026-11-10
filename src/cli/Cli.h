#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace codecomplex {

constexpr int kExitOk = 0;
constexpr int kExitUsage = 1;
constexpr int kExitInput = 2;

enum class Command { Help, Version, Analyze, Evaluate };

struct CliOptions {
    Command command = Command::Help;
    std::string file;
    std::string functionName;
    bool explain = false;
    bool useKnn = true;
    bool recommend = true;
    bool timing = false;
    int k = 5;
    std::string dataDir = "data";
    double maxDistance = -1;
    double minConfidence = 0;
    std::string csvPath;
    std::string jsonPath;
};

struct CliParseResult {
    bool ok = false;
    CliOptions options;
    std::string error;
};

CliParseResult parseArgs(const std::vector<std::string>& args);

void printUsage(std::ostream& out);

int runCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}
