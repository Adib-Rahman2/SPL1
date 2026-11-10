#include "source/SourceFile.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace codecomplex {

namespace {

SourceLoadResult failure(const std::string& message) {
    SourceLoadResult result;
    result.error = message;
    return result;
}

bool isWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

}

std::string checkSourceText(const std::string& text) {
    if (text.find('\0') != std::string::npos) {
        return "file contains NUL bytes (binary data?)";
    }
    for (char c : text) {
        if (!isWhitespace(c)) return "";
    }
    return text.empty() ? "file is empty" : "file contains only whitespace";
}

SourceLoadResult loadSourceFile(const std::string& path, std::size_t maxBytes) {
    namespace fs = std::filesystem;
    std::error_code ec;

    if (!fs::exists(path, ec)) {
        return failure("file not found: " + path);
    }
    if (!fs::is_regular_file(path, ec)) {
        return failure("not a regular file: " + path);
    }
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        return failure("cannot read file size: " + path);
    }
    if (size > maxBytes) {
        return failure("file is too large: " + path + " (" + std::to_string(size) +
                       " bytes, limit " + std::to_string(maxBytes) + " bytes)");
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return failure("cannot open file: " + path);
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        return failure("error while reading file: " + path);
    }

    SourceLoadResult result;
    result.file.path = path;
    result.file.text = buffer.str();
    const std::string problem = checkSourceText(result.file.text);
    if (!problem.empty()) {
        return failure(problem + ": " + path);
    }
    result.ok = true;
    return result;
}

}
