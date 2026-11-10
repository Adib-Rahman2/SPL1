# CoRCoD (Code Runtime Complexity Dataset)

Java programs labelled with their time complexity. I use it to train and test the KNN.

It comes from the `corcod-dataset` repository (the download is in `corcod-dataset-master/`,
unchanged). The paper:

> J. Sikka, K. Satya, Y. Kumar, S. Uppal, R. R. Shah, R. Zimmermann.
> *Learning Based Methods for Code Runtime Complexity Prediction.* arXiv:1911.01155, 2019
> (ECIR 2020).

## What's here

| Path | What it is |
|------|------------|
| `corcod-dataset-master/` | the original download: `data.tar.gz`, `metadata.csv`, the authors' results |
| `java/` | the programs from `data.tar.gz` (`Dataset/*.java`), without the macOS `._*` files |
| `labels.csv` | my label file, made from `metadata.csv` (see below) |

## How I made `java/` and `labels.csv`

```bash
cd data/corcod
mkdir -p java
tar xzf corcod-dataset-master/data.tar.gz --exclude='._*' --strip-components=1 -C java
```

From `metadata.csv` I only took the file name and the label (the `complexity` column). I don't use
the other columns (the authors' features), because my own feature extractor counts the features
again, so the training programs and the analysed code are measured the same way. I changed the
label names to the notation my tool uses:

| `metadata.csv` | `labels.csv` |
|----------------|--------------|
| `1` | `O(1)` |
| `logn` | `O(log n)` |
| `n` | `O(n)` |
| `nlogn` | `O(n log n)` |
| `n_square` | `O(n^2)` |

`best_complexity` and `inefficiency` are empty (CoRCoD has no improvement labels), and `reviewed`
is `yes` because these are published labels.

`metadata.csv` has 933 rows but `data.tar.gz` only has 932 programs. `35.java` (labelled `n`) is
missing, so `labels.csv` has 932 rows: O(1) 143, O(log n) 55, O(n) 384, O(n log n) 150, O(n^2) 200.

The labels are for **whole programs**. For a whole program my tool takes the biggest complexity of
the functions reachable from `main` (see `docs/DESIGN.md`).
