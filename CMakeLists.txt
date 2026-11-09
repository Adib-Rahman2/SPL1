cmake_minimum_required(VERSION 3.16)
project(CodeComplex VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(codecomplex_core STATIC
    src/source/SourceFile.cpp
    src/lexer/Lexer.cpp
    src/structure/Parser.cpp
    src/analysis/Complexity.cpp
    src/features/FeatureExtractor.cpp
    src/analysis/LibraryCosts.cpp
    src/analysis/RuleEngine.cpp
    src/analysis/Recurrence.cpp
    src/analysis/Program.cpp
    src/ml/Dataset.cpp
    src/ml/Knn.cpp
    src/ml/Evaluation.cpp
    src/advice/PatternDetector.cpp
    src/advice/Recommender.cpp
    src/report/TextReport.cpp
    src/report/CsvWriter.cpp
    src/report/JsonWriter.cpp
    src/cli/Cli.cpp)
target_include_directories(codecomplex_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(codecomplex_core PUBLIC
    CODECOMPLEX_VERSION="${PROJECT_VERSION}")

add_executable(codecomplex src/main.cpp)
target_link_libraries(codecomplex PRIVATE codecomplex_core)

enable_testing()
add_executable(codecomplex_tests
    tests/test_main.cpp
    tests/test_framework_selftest.cpp
    tests/test_source_file.cpp
    tests/test_lexer.cpp
    tests/test_parser.cpp
    tests/test_complexity.cpp
    tests/test_features.cpp
    tests/test_library_costs.cpp
    tests/test_rule_engine.cpp
    tests/test_recurrence.cpp
    tests/test_knn.cpp
    tests/test_dataset.cpp
    tests/test_advice.cpp
    tests/test_export.cpp
    tests/test_report.cpp
    tests/test_cli.cpp)
target_link_libraries(codecomplex_tests PRIVATE codecomplex_core)
target_include_directories(codecomplex_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(codecomplex_tests PRIVATE
    CODECOMPLEX_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data"
    CODECOMPLEX_TEST_OUTPUT_DIR="${CMAKE_BINARY_DIR}")
add_test(NAME unit_tests COMMAND codecomplex_tests)

add_test(NAME cli_analyze_file
    COMMAND codecomplex analyze ${CMAKE_SOURCE_DIR}/tests/data/loops.cpp)
set_tests_properties(cli_analyze_file PROPERTIES
    PASS_REGULAR_EXPRESSION "Detected Pattern  Nested Loops")
add_test(NAME cli_usage_error COMMAND codecomplex analyze)
set_tests_properties(cli_usage_error PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli_missing_file
    COMMAND codecomplex analyze ${CMAKE_SOURCE_DIR}/tests/data/does_not_exist.cpp)
set_tests_properties(cli_missing_file PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli_recommendation
    COMMAND codecomplex analyze --no-knn ${CMAKE_SOURCE_DIR}/tests/data/advice.cpp)
set_tests_properties(cli_recommendation PROPERTIES
    PASS_REGULAR_EXPRESSION "Overlapping recursion without memoization")
