# CodeComplex

My Software Project Lab 1 project. It's a command line tool that reads a C++ file and tells you
the time complexity of each function in it, why it thinks so, and how the code could be made faster.

How it works, roughly:

1. It reads the file, splits it into tokens and finds the functions, loops, if/else and calls.
2. From that it counts some features (loops, nesting depth, recursion, log loops, calls ...).
3. A set of rules works out the complexity (O(1), O(log n), O(n), O(n log n), O(n^2), O(n^3),
   O(2^n), O(n!)). For recursion it builds a recurrence and solves it (Master Theorem etc).
4. A KNN classifier, which I wrote myself, guesses the complexity too, from the most similar programs
   in a labelled dataset. Its answer is only shown next to the rules' answer for comparison.
5. It looks for slow patterns (search inside a loop, bubble-sort shape, fib without memoization ...)
   and suggests a better way, with the complexity you could get.

If it can't work something out it says **Unknown**. It doesn't guess.

## Building

You need CMake 3.16 or newer and a C++17 compiler (I used g++ from MSYS2).

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Running

```bash
build/codecomplex analyze tests/data/loops.cpp
build/codecomplex analyze tests/data/loops.cpp --no-recommend
build/codecomplex analyze tests/data/recursion.cpp --function fib --explain
build/codecomplex analyze tests/data/advice.cpp --csv advice.csv
build/codecomplex evaluate
build/codecomplex --help
```

Run it from this folder, because the KNN loads its dataset from `data/` (or give `--data DIR`).
More examples with real output are in [docs/USER_GUIDE.md](docs/USER_GUIDE.md).

## Dataset

`data/corcod/` is CoRCoD: 932 Java programs, each labelled O(1), O(log n), O(n), O(n log n) or
O(n^2). I only use the labels. The features are counted again by my own code, so the training
programs and the analysed code are measured the same way. See `data/corcod/README.md`.

J. Sikka, K. Satya, Y. Kumar, S. Uppal, R. R. Shah, R. Zimmermann, *Learning Based Methods for
Code Runtime Complexity Prediction*, arXiv:1911.01155, 2019 (ECIR 2020).

## Folders

| Folder | What's in it |
|--------|--------------|
| `src/` | the program |
| `tests/` | unit tests (with my own small test framework, `tests/TestFramework.h`) and test inputs |
| `data/corcod/` | the dataset for the KNN |
| `docs/` | user guide, design notes and limitations, evaluation and timing results |
| `scripts/` | `demo.sh` runs everything once, `benchmark.sh` does the big-file and timing test |
