#pragma once

#include <cstddef>
#include <string>

namespace codecomplex {

constexpr std::size_t kMaxSourceBytes = 5 * 1024 * 1024;

struct SourceFile {
    std::string path;
    std::string text;
};

struct SourceLoadResult {
    bool ok = false;
    SourceFile file;
    std::string error;
};

SourceLoadResult loadSourceFile(const std::string& path,
                                std::size_t maxBytes = kMaxSourceBytes);

std::string checkSourceText(const std::string& text);

}
