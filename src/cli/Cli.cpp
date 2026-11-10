#include "cli/Cli.h"

#include "advice/PatternDetector.h"
#include "advice/Recommender.h"
#include "analysis/RuleEngine.h"
#include "features/FeatureExtractor.h"
#include "lexer/Lexer.h"
#include "ml/Dataset.h"
#include "ml/Evaluation.h"
#include "ml/Knn.h"
#include "report/CsvWriter.h"
#include "report/JsonWriter.h"
#include "report/TextReport.h"
#include "source/Language.h"
#include "source/SourceFile.h"
#include "structure/Parser.h"

#include <cctype>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace codecomplex {

namespace {

CliParseResult fail(const std::string& error) {
    CliParseResult result;
    result.error = error;
    return result;
}

bool isOption(const std::string& arg) { return arg.size() > 1 && arg[0] == '-'; }

bool parseK(const std::string& text, int& k, std::string& error) {
    if (text.empty() || text.size() > 6) {
        error = "--k needs a whole number from 1 to 999999, got '" + text + "'";
        return false;
    }
    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            error = "--k needs a whole number from 1 to 999999, got '" + text + "'";
            return false;
        }
    }
    const int value = std::stoi(text);
    if (value < 1 || value % 2 == 0) {
        error = "--k must be odd and at least 1 (so a vote cannot tie), got " + text;
        return false;
    }
    k = value;
    return true;
}

bool parseNumber(const std::string& option, const std::string& text, double low, double high,
                 double& value, std::string& error) {
    int dots = 0;
    int digits = 0;
    for (char c : text) {
        if (c == '.') {
            ++dots;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            ++digits;
        } else {
            dots = 99;
        }
    }
    if (digits == 0 || dots > 1 || text.size() > 12) {
        error = option + " needs a number, got '" + text + "'";
        return false;
    }
    value = std::stod(text);
    if (value < low || value > high) {
        std::ostringstream message;
        message << option << " must be from " << low << " to " << high << ", got " << text;
        error = message.str();
        return false;
    }
    return true;
}

CliParseResult parseCommand(const std::vector<std::string>& args, Command command) {
    CliParseResult result;
    CliOptions& options = result.options;
    options.command = command;
    const bool analyze = command == Command::Analyze;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const bool analyzeOnly = arg == "--explain" || arg == "--no-knn" || arg == "--timing" ||
                                 arg == "--no-recommend" || arg == "--function" ||
                                 arg == "--csv" || arg == "--json";
        if (analyzeOnly && !analyze) return fail("evaluate does not take " + arg);
        if (arg == "--explain") {
            options.explain = true;
        } else if (arg == "--no-knn") {
            options.useKnn = false;
        } else if (arg == "--no-recommend") {
            options.recommend = false;
        } else if (arg == "--timing") {
            options.timing = true;
        } else if (arg == "--function" || arg == "--k" || arg == "--csv" || arg == "--json" ||
                   arg == "--data" || arg == "--max-distance" || arg == "--min-confidence") {
            if (i + 1 >= args.size() || isOption(args[i + 1]) || args[i + 1].empty()) {
                return fail(arg + " needs a value");
            }
            const std::string& value = args[++i];
            std::string error;
            if (arg == "--function") {
                options.functionName = value;
            } else if (arg == "--csv") {
                options.csvPath = value;
            } else if (arg == "--json") {
                options.jsonPath = value;
            } else if (arg == "--data") {
                options.dataDir = value;
            } else if (arg == "--max-distance") {
                if (!parseNumber(arg, value, 0, 100, options.maxDistance, error)) return fail(error);
            } else if (arg == "--min-confidence") {
                if (!parseNumber(arg, value, 0, 1, options.minConfidence, error)) return fail(error);
            } else if (!parseK(value, options.k, error)) {
                return fail(error);
            }
        } else if (isOption(arg)) {
            return fail("unknown option '" + arg + "'");
        } else if (!analyze) {
            return fail("evaluate takes no file, got '" + arg + "'");
        } else if (!options.file.empty()) {
            return fail("analyze takes one file, got '" + options.file + "' and '" + arg + "'");
        } else {
            options.file = arg;
        }
    }
    if (analyze && options.file.empty()) return fail("analyze needs a file");
    result.ok = true;
    return result;
}

KnnOptions knnOptions(const CliOptions& options) {
    KnnOptions knn;
    knn.k = options.k;
    if (options.maxDistance >= 0) knn.maxDistance = options.maxDistance;
    knn.minConfidence = options.minConfidence;
    return knn;
}

Dataset loadKnnDataset(const CliOptions& options, std::string& why) {
    const std::vector<std::string> files = defaultLabelFiles(options.dataDir);
    if (files.empty()) {
        why = "no dataset found in '" + options.dataDir + "' (corcod/labels.csv, cpp/labels.csv)";
        return Dataset();
    }
    Dataset dataset = loadDataset(files);
    if (dataset.examples.empty()) why = "no usable labelled examples in '" + options.dataDir + "'";
    return dataset;
}

bool matchesName(const Function& function, const std::string& wanted) {
    if (function.name == wanted) return true;
    return !function.className.empty() && function.className + "::" + function.name == wanted;
}

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

int runAnalyze(const CliOptions& options, std::ostream& out, std::ostream& err) {
    const Clock::time_point started = Clock::now();
    const SourceLoadResult loaded = loadSourceFile(options.file);
    if (!loaded.ok) {
        err << "error: " << loaded.error << '\n';
        return kExitInput;
    }

    const Language language = languageForPath(options.file);
    const LexResult lexed = tokenize(loaded.file.text, language);
    for (const LexError& error : lexed.errors) {
        err << options.file << ':' << error.line << ':' << error.column
            << ": error: " << error.message << '\n';
    }
    if (!lexed.ok()) return kExitInput;

    const ParseResult parsed = parse(lexed.tokens, language);
    const double parseMs = millisecondsSince(started);
    const Clock::time_point knnStarted = Clock::now();

    std::vector<const Function*> chosen;
    for (const Function& function : parsed.functions) {
        if (options.functionName.empty() || matchesName(function, options.functionName)) {
            chosen.push_back(&function);
        }
    }
    if (!options.functionName.empty() && chosen.empty()) {
        err << "error: no function named '" << options.functionName << "' in "
            << options.file << '\n';
        if (!parsed.functions.empty()) {
            err << "functions in the file:";
            for (const Function& function : parsed.functions) err << ' ' << displayName(function);
            err << '\n';
        }
        return kExitInput;
    }

    std::string knnUnavailable = "turned off (--no-knn)";
    Dataset dataset;
    std::vector<std::vector<double>> rows;
    std::vector<std::string> labels;
    std::vector<std::string> improvements;
    if (options.useKnn) {
        knnUnavailable.clear();
        dataset = loadKnnDataset(options, knnUnavailable);
        for (const Example& example : dataset.examples) {
            rows.push_back(example.features.values());
            labels.push_back(example.complexity);
            improvements.push_back(example.bestComplexity.empty()
                                       ? ""
                                       : example.inefficiency + " -> " + example.bestComplexity);
        }
        if (!dataset.errors.empty()) {
            err << "warning: " << dataset.errors.size() << " dataset row(s) were skipped; "
                << "run 'codecomplex evaluate' to list them\n";
        }
        if (knnUnavailable.empty() && static_cast<std::size_t>(options.k) > rows.size()) {
            knnUnavailable = "k = " + std::to_string(options.k) + " is more than the " +
                             std::to_string(rows.size()) + " labelled examples";
        }
    }
    const KnnClassifier knn(rows, labels, rows.size());
    const KnnClassifier improvementKnn(rows, improvements, rows.size());
    const double knnMs = millisecondsSince(knnStarted);
    const Clock::time_point analysisStarted = Clock::now();

    writeFileHeader(out, options.file, static_cast<int>(parsed.functions.size()));
    if (knnUnavailable.empty()) {
        out << "KNN dataset: " << rows.size() << " labelled programs from " << options.dataDir
            << " (k = " << options.k << ")\n";
    }
    if (parsed.functions.empty()) out << "\nNo function definitions found.\n";

    ReportOptions reportOptions;
    reportOptions.explain = options.explain;
    reportOptions.recommend = options.recommend;
    std::vector<FunctionResult> results;
    RuleCache ruleCache;
    for (const Function* function : chosen) {
        const FeatureVector features = extractFeatures(*function, lexed.tokens);
        const RuleResult rules = analyzeFunction(*function, parsed.functions, &ruleCache);
        KnnEstimate estimate;
        if (knnUnavailable.empty()) {
            estimate.ran = true;
            estimate.prediction = knn.predict(features.values(), knnOptions(options), rows.size());
            for (const Neighbour& n : estimate.prediction.neighbours) {
                std::ostringstream line;
                line << std::fixed << std::setprecision(3) << dataset.examples[n.index].file
                     << "  " << labels[n.index] << "  distance " << n.distance;
                estimate.neighbours.push_back(line.str());
            }
        } else {
            estimate.unavailable = knnUnavailable;
        }

        ImprovementVote improvement;
        if (!knnUnavailable.empty()) {
            improvement.note = knnUnavailable;
        } else if (improvementKnn.labelledCount() == 0) {
            improvement.note = "no program in the dataset has improvement labels yet";
        } else {
            improvement.ran = true;
            improvement.prediction =
                improvementKnn.predict(features.values(), knnOptions(options), rows.size());
        }
        const std::vector<Recommendation> recommendations =
            recommend(rules, detectPatterns(*function, rules), improvement);
        writeFunctionReport(out, *function, features, rules, estimate, recommendations,
                            reportOptions);

        FunctionResult result;
        result.file = options.file;
        result.name = displayName(*function);
        result.startLine = function->line;
        result.endLine = function->endLine;
        result.features = features;
        result.rules = rules;
        result.knn = estimate;
        result.recommendations = recommendations;
        results.push_back(result);
    }
    writeUnsupported(out, parsed.unsupported);
    const double analysisMs = millisecondsSince(analysisStarted);

    int exitCode = kExitOk;
    std::string error;
    if (!options.csvPath.empty()) {
        if (writeCsvFile(options.csvPath, results, error)) {
            out << "\nCSV written: " << options.csvPath << " (" << results.size() << " functions)\n";
        } else {
            err << "error: " << error << '\n';
            exitCode = kExitInput;
        }
    }
    if (!options.jsonPath.empty()) {
        if (writeJsonFile(options.jsonPath, options.file, results, error)) {
            out << "JSON written: " << options.jsonPath << " (" << results.size() << " functions)\n";
        } else {
            err << "error: " << error << '\n';
            exitCode = kExitInput;
        }
    }
    if (options.timing) {
        err << std::fixed << std::setprecision(1) << "timing: " << lexed.tokens.size()
            << " tokens, " << parsed.functions.size() << " functions; parse " << parseMs
            << " ms, KNN dataset " << knnMs << " ms, analysis " << analysisMs << " ms, total "
            << millisecondsSince(started) << " ms\n";
    }
    return exitCode;
}

int runEvaluate(const CliOptions& options, std::ostream& out, std::ostream& err) {
    std::string why;
    const Dataset dataset = loadKnnDataset(options, why);
    if (!why.empty()) {
        err << "error: " << why << '\n';
        for (const std::string& error : dataset.errors) err << "  " << error << '\n';
        return kExitInput;
    }
    if (static_cast<std::size_t>(options.k) >= dataset.examples.size()) {
        err << "error: leave-one-out with k = " << options.k << " needs more than " << options.k
            << " examples; the dataset has " << dataset.examples.size() << '\n';
        return kExitInput;
    }
    EvaluationOptions evaluation;
    evaluation.knn = knnOptions(options);
    writeEvaluation(out, dataset, evaluation);
    return kExitOk;
}

}

CliParseResult parseArgs(const std::vector<std::string>& args) {
    if (args.empty()) return fail("no command given");

    const std::string& command = args[0];
    if (command == "--help" || command == "-h" || command == "--version") {
        if (args.size() > 1) return fail(command + " takes no arguments");
        CliParseResult result;
        result.ok = true;
        result.options.command = command == "--version" ? Command::Version : Command::Help;
        return result;
    }
    if (command == "analyze") return parseCommand(args, Command::Analyze);
    if (command == "evaluate") return parseCommand(args, Command::Evaluate);
    return fail("unknown command '" + command + "'");
}

void printUsage(std::ostream& out) {
    out << "CodeComplex " << CODECOMPLEX_VERSION
        << " - C++ source-code complexity analyzer\n\n"
        << "Usage:\n"
        << "  codecomplex analyze <file> [options]\n"
        << "  codecomplex evaluate [--data DIR] [--k N] [--max-distance D] [--min-confidence P]\n"
        << "  codecomplex --version\n"
        << "  codecomplex --help\n\n"
        << "Options for analyze:\n"
        << "  --function NAME   only this function (NAME or Class::NAME)\n"
        << "  --explain         show line numbers, the extra features and the KNN neighbours\n"
        << "  --no-knn          don't run the KNN classifier\n"
        << "  --no-recommend    leave the Recommendation section out of the report\n"
        << "  --timing          print how long each phase took (on stderr)\n"
        << "  --k N             neighbours for the KNN (odd, default 5)\n"
        << "  --data DIR        folder with corcod/labels.csv and cpp/labels.csv (default data)\n"
        << "  --max-distance D  KNN: ignore neighbours further than D (default: no limit)\n"
        << "  --min-confidence P  KNN: no vote below this share of k, 0 to 1 (default 0)\n"
        << "  --csv OUT         write one CSV row per function\n"
        << "  --json OUT        write the results as JSON\n\n"
        << "Exit codes: 0 success, 1 usage error, 2 input or file error.\n";
}

int runCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    const CliParseResult parsed = parseArgs(args);
    if (!parsed.ok) {
        err << "error: " << parsed.error << "\n\n";
        printUsage(err);
        return kExitUsage;
    }

    switch (parsed.options.command) {
    case Command::Help:
        printUsage(out);
        return kExitOk;
    case Command::Version:
        out << "CodeComplex " << CODECOMPLEX_VERSION << '\n';
        return kExitOk;
    case Command::Analyze:
        return runAnalyze(parsed.options, out, err);
    case Command::Evaluate:
        return runEvaluate(parsed.options, out, err);
    }
    return kExitUsage;
}

}
