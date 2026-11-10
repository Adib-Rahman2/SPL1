#include "advice/Recommender.h"

#include "ml/Dataset.h"

#include <algorithm>

namespace codecomplex {

namespace {

struct Advice {
    const char* whySlow;
    const char* alternative;
    const char* prerequisites;
    const char* tradeOffs;
};

Advice adviceFor(PatternKind kind) {
    switch (kind) {
    case PatternKind::LinearSearchInLoop:
        return {"for every iteration of the outer loop the inner loop scans the data again, so "
                "each lookup costs O(n)",
                "put the values in a hash set (std::unordered_set, Java HashSet) once, before the "
                "outer loop, and look each one up in O(1) on average; or sort once and use binary "
                "search (O(log n) per lookup)",
                "the elements must be hashable (or comparable, for sorting); the searched data "
                "must not change inside the outer loop",
                "O(n) extra memory; hash lookups are O(1) on average but O(n) in the worst case; "
                "if positions are needed, keep them in a hash map instead"};
    case PatternKind::FindInLoop:
        return {"each find / count (or indexOf / contains on a list) walks the whole container: "
                "O(n) per iteration",
                "build an std::unordered_set (or an unordered_map of counts; Java HashSet / "
                "HashMap) of the searched container once, before the loop, and query that",
                "the elements must be hashable; the searched container must not change during "
                "the loop",
                "O(n) extra memory and the time to build the set; not worth it for very small "
                "containers"};
    case PatternKind::LoopInvariantCall:
        return {"a loop header is evaluated on every iteration, so an O(n) call there runs once "
                "per iteration",
                "compute the value once before the loop and keep it in a variable, e.g. "
                "`const std::size_t len = strlen(s);`",
                "the arguments, and the data they refer to, must not change inside the loop",
                "none in time; one extra variable"};
    case PatternKind::RepeatedCall:
        return {"the same work is done more than once",
                "call it once, store the result in a local variable and reuse it",
                "the call must have no side effects (no input or output, no changes to shared "
                "data) and its arguments must not change between the calls",
                "one extra variable; the complexity class may stay the same, only the repeated "
                "work is saved"};
    case PatternKind::OverlappingRecursion:
        return {"each call starts two or more calls on overlapping smaller sizes, so the same "
                "values are computed an exponential number of times",
                "memoize: keep each result in a table indexed by the argument and return it when "
                "it is already known; or fill the table bottom-up in a loop (dynamic programming)",
                "the result must depend only on the changing argument; the table needs one entry "
                "per possible value",
                "O(n) extra memory for the table; the bottom-up loop also avoids deep recursion"};
    case PatternKind::QuadraticOrdering:
        return {"comparing elements pairwise in two nested loops takes O(n^2) comparisons",
                "use the library sort (std::sort, Arrays.sort): O(n log n)",
                "the loops must really sort (or select by order); a comparison function is "
                "needed for a custom order",
                "std::sort is not stable (std::stable_sort is); for very small or nearly sorted "
                "inputs a simple sort can be as fast in practice"};
    case PatternKind::PrefixRecomputation:
        return {"each prefix is computed from scratch, although it differs from the previous one "
                "by a single element",
                "keep a running total updated once per outer iteration, or build a prefix-sum "
                "array once",
                "the operation must be updatable step by step (sum, product, count, maximum of a "
                "prefix)",
                "none in time; a prefix array needs O(n) memory, a running total O(1)"};
    }
    return {"", "", "", ""};
}

Advice adviceForLabel(const std::string& inefficiency) {
    if (inefficiency == "nested-linear-search" || inefficiency == "brute-force-pairs" ||
        inefficiency == "brute-force-triples") {
        return adviceFor(PatternKind::LinearSearchInLoop);
    }
    if (inefficiency == "repeated-find-in-loop") return adviceFor(PatternKind::FindInLoop);
    if (inefficiency == "overlapping-recursion") return adviceFor(PatternKind::OverlappingRecursion);
    if (inefficiency == "quadratic-sort") return adviceFor(PatternKind::QuadraticOrdering);
    if (inefficiency == "repeated-computation") return adviceFor(PatternKind::PrefixRecomputation);
    if (inefficiency == "linear-search-on-sorted-data") {
        return {"an element-by-element search ignores that the data is sorted",
                "use binary search (std::lower_bound, std::binary_search, Arrays.binarySearch)",
                "the data must be sorted by the key searched for",
                "none if the data is already sorted; sorting first costs O(n log n)"};
    }
    if (inefficiency == "brute-force-subarrays") {
        return {"every subarray is tried and summed",
                "keep a running best (Kadane's algorithm) or use prefix sums",
                "the task must be a best or a count over contiguous subarrays",
                "none in time; prefix sums need O(n) memory"};
    }
    if (inefficiency == "brute-force-permutations") {
        return {"every ordering of the items is tried",
                "dynamic programming over subsets (e.g. Held-Karp for tours)",
                "the cost of a partial solution must depend only on the set used and the last item",
                "O(2^n) memory for the table"};
    }
    if (inefficiency == "ordered-map-where-hash-map-suffices") {
        return {"an ordered map pays O(log n) per operation for an order that is not used",
                "use a hash map (std::unordered_map, Java HashMap)",
                "the keys' order must never be used (no ordered iteration, no lower_bound)",
                "no ordered iteration; O(n) per operation in the worst case"};
    }
    return {"similar labelled programs were marked with this inefficiency",
            "compare this function with the dataset's programs labelled with this inefficiency",
            "-", "-"};
}

std::string oneFactorLess(const Complexity& c) {
    if (c.kind != Complexity::Kind::Polynomial || c.power < 1) return "";
    return Complexity::polynomial(c.power - 1, c.logPower).notation();
}

int labelRank(const std::string& notation) {
    const std::vector<std::string>& labels = complexityLabels();
    const auto at = std::find(labels.begin(), labels.end(), notation);
    return at == labels.end() ? -1 : static_cast<int>(at - labels.begin());
}

void improvedFor(PatternKind kind, const RuleResult& rules, Recommendation& r) {
    const Complexity& now = rules.complexity;
    if (now.isUnknown()) {
        r.improvedNote = "the current estimate is Unknown, so no improved class is claimed";
        return;
    }
    switch (kind) {
    case PatternKind::LinearSearchInLoop:
    case PatternKind::FindInLoop:
    case PatternKind::LoopInvariantCall:
    case PatternKind::PrefixRecomputation:
        r.improved = oneFactorLess(now);
        if (!r.improved.empty()) {
            r.improvedNote = "from " + now.notation() + ", if this loop nest is the largest term";
        }
        break;
    case PatternKind::QuadraticOrdering:
        if (now.kind == Complexity::Kind::Polynomial && now.power == 2 && now.logPower == 0) {
            r.improved = Complexity::linearithmic().notation();
            r.improvedNote = "from " + now.notation() + ", if these loops only sort";
        }
        break;
    case PatternKind::OverlappingRecursion: {
        const Complexity memo = multiply(Complexity::linear(), rules.nonRecursiveWork);
        if (!memo.isUnknown()) {
            r.improved = memo.notation();
            r.improvedNote = "from " + now.notation() + ": O(n) different values, each computed "
                                                        "once with f(n) = " +
                             rules.nonRecursiveWork.notation();
        }
        break;
    }
    case PatternKind::RepeatedCall:
        r.improvedNote = "the class stays " + now.notation() + "; the repeated work is saved";
        break;
    }
    if (r.improved.empty() && r.improvedNote.empty()) {
        r.improvedNote = "no smaller class is claimed for " + now.notation();
    }
}

std::string voteText(const Prediction& p) {
    return std::to_string(p.votes) + " of " + std::to_string(p.k) + " neighbours";
}

}

const char* confidenceName(AdviceSource source) {
    switch (source) {
    case AdviceSource::Agree:     return "high";
    case AdviceSource::RulesOnly: return "medium";
    case AdviceSource::KnnOnly:   return "low";
    }
    return "?";
}

const char* sourceName(AdviceSource source) {
    switch (source) {
    case AdviceSource::Agree:     return "rules and KNN agree";
    case AdviceSource::RulesOnly: return "rules only";
    case AdviceSource::KnnOnly:   return "KNN only";
    }
    return "?";
}

bool splitImprovementLabel(const std::string& label, std::string& inefficiency, std::string& best) {
    const std::size_t arrow = label.find(" -> ");
    if (arrow == std::string::npos) return false;
    inefficiency = label.substr(0, arrow);
    best = label.substr(arrow + 4);
    return !inefficiency.empty() && !best.empty();
}

bool patternMatchesInefficiency(PatternKind kind, const std::string& inefficiency) {
    switch (kind) {
    case PatternKind::LinearSearchInLoop:
        return inefficiency == "nested-linear-search" || inefficiency == "brute-force-pairs" ||
               inefficiency == "brute-force-triples";
    case PatternKind::FindInLoop:
        return inefficiency == "repeated-find-in-loop";
    case PatternKind::OverlappingRecursion:
        return inefficiency == "overlapping-recursion";
    case PatternKind::QuadraticOrdering:
        return inefficiency == "quadratic-sort";
    case PatternKind::PrefixRecomputation:
    case PatternKind::LoopInvariantCall:
    case PatternKind::RepeatedCall:
        return inefficiency == "repeated-computation";
    }
    return false;
}

std::vector<Recommendation> recommend(const RuleResult& rules,
                                      const std::vector<DetectedPattern>& patterns,
                                      const ImprovementVote& knn) {
    std::string inefficiency, best;
    const bool knnVoted = knn.ran && knn.prediction.ok &&
                          splitImprovementLabel(knn.prediction.label, inefficiency, best);
    std::string knnSilent = knn.ran ? knn.prediction.note : knn.note;
    if (knnSilent.empty()) knnSilent = "the KNN gave no improvement vote";

    std::vector<Recommendation> result;
    bool knnMatched = false;
    for (const DetectedPattern& pattern : patterns) {
        Recommendation r;
        const Advice advice = adviceFor(pattern.kind);
        r.title = patternKindName(pattern.kind);
        r.line = pattern.line;
        r.found = pattern.detail;
        r.whySlow = advice.whySlow;
        r.alternative = advice.alternative;
        r.prerequisites = advice.prerequisites;
        r.tradeOffs = advice.tradeOffs;
        improvedFor(pattern.kind, rules, r);

        if (!knnVoted) {
            r.source = AdviceSource::RulesOnly;
            r.knnNote = knnSilent;
        } else if (patternMatchesInefficiency(pattern.kind, inefficiency)) {
            r.source = AdviceSource::Agree;
            knnMatched = true;
            r.knnNote = "similar labelled programs: " + inefficiency + ", best " + best + " (" +
                        voteText(knn.prediction) + ")";
        } else if (inefficiency == "none") {
            r.source = AdviceSource::RulesOnly;
            r.knnNote = "the most similar labelled programs were already at their best "
                        "complexity (" + voteText(knn.prediction) + ")";
        } else {
            r.source = AdviceSource::RulesOnly;
            r.knnNote = "the most similar labelled programs had another inefficiency: " +
                        inefficiency + ", best " + best + " (" + voteText(knn.prediction) + ")";
        }
        result.push_back(r);
    }

    if (knnVoted && !knnMatched && inefficiency != "none") {
        const int now = labelRank(rules.complexity.notation());
        const int target = labelRank(best);
        const bool better = rules.complexity.isUnknown() || (now >= 0 && target >= 0 && target < now);
        if (better) {
            Recommendation r;
            const Advice advice = adviceForLabel(inefficiency);
            r.title = "Similar programs: " + inefficiency;
            r.found = "the KNN's nearest labelled programs were marked " + inefficiency + "; " +
                      (patterns.empty() ? "no rule pattern was found"
                                        : "none of the rule patterns found matches it");
            r.whySlow = advice.whySlow;
            r.alternative = advice.alternative;
            r.prerequisites = advice.prerequisites;
            r.tradeOffs = advice.tradeOffs;
            r.improved = best;
            r.improvedNote = "the best complexity of those programs, not checked against this code";
            r.source = AdviceSource::KnnOnly;
            r.knnNote = inefficiency + ", best " + best + " (" + voteText(knn.prediction) + ")";
            result.push_back(r);
        }
    }
    return result;
}

}
