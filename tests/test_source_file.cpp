#include "TestFramework.h"

#include "source/SourceFile.h"

#include <filesystem>
#include <fstream>
#include <string>

using codecomplex::checkSourceText;
using codecomplex::loadSourceFile;

namespace {

namespace fs = std::filesystem;

fs::path tempDir() {
    const fs::path dir = fs::temp_directory_path() / "codecomplex_tests";
    fs::create_directories(dir);
    return dir;
}

std::string writeTempFile(const std::string& name, const std::string& content) {
    const fs::path path = tempDir() / name;
    std::ofstream out(path, std::ios::binary);
    out << content;
    return path.string();
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

}

TEST_CASE("source: loads a valid file unchanged") {
    const std::string content = "int main() {\r\n    return 0;\n}\n";
    const std::string path = writeTempFile("valid.cpp", content);
    const auto result = loadSourceFile(path);
    REQUIRE(result.ok);
    CHECK_EQ(result.file.text, content);
    CHECK_EQ(result.file.path, path);
    CHECK(result.error.empty());
}

TEST_CASE("source: missing file is an error") {
    const std::string path = (tempDir() / "does_not_exist.cpp").string();
    const auto result = loadSourceFile(path);
    CHECK(!result.ok);
    CHECK(contains(result.error, "file not found"));
}

TEST_CASE("source: a directory is not a regular file") {
    const auto result = loadSourceFile(tempDir().string());
    CHECK(!result.ok);
    CHECK(contains(result.error, "not a regular file"));
}

TEST_CASE("source: empty file is an error") {
    const auto result = loadSourceFile(writeTempFile("empty.cpp", ""));
    CHECK(!result.ok);
    CHECK(contains(result.error, "file is empty"));
}

TEST_CASE("source: whitespace-only file is an error") {
    const auto result = loadSourceFile(writeTempFile("blank.cpp", "  \n\t\r\n  "));
    CHECK(!result.ok);
    CHECK(contains(result.error, "only whitespace"));
}

TEST_CASE("source: file with NUL bytes is rejected as binary") {
    const auto result = loadSourceFile(writeTempFile("binary.cpp", std::string("int\0x;", 6)));
    CHECK(!result.ok);
    CHECK(contains(result.error, "NUL"));
}

TEST_CASE("source: file over the size limit is rejected") {
    const std::string path = writeTempFile("big.cpp", "int x = 1234567890;");
    const auto tooBig = loadSourceFile(path, 18);
    CHECK(!tooBig.ok);
    CHECK(contains(tooBig.error, "too large"));

    const auto atLimit = loadSourceFile(path, 19);
    CHECK(atLimit.ok);
}

TEST_CASE("source: checkSourceText accepts code and rejects bad text") {
    CHECK(checkSourceText("int x;").empty());
    CHECK(!checkSourceText("").empty());
    CHECK(!checkSourceText(" \n ").empty());
    CHECK(!checkSourceText(std::string("a\0b", 3)).empty());
}
