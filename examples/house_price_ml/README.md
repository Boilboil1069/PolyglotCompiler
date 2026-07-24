# Cross-language house-price linear regression

This is a small machine-learning project that really executes data generation, training, holdout prediction, and evaluation. The compiled program generates 12 noisy samples at runtime, fits an ordinary least-squares model on eight rows, and evaluates mean absolute error (MAE) on four unseen rows.

One Poly entry owns the complete project graph:

```text
Poly       train/test split, orchestration, and assertions
  ├─ Python  deterministic feature, noise, and target generation
  ├─ C++     x² and xy sufficient statistics
  ├─ Rust    ordinary least-squares parameter fitting
  └─ Go      prediction serving and holdout MAE
```

The build uses only the repository's `polyc` and `polyld`; it does not run CPython, clang/GCC, rustc, Cargo, or the system Go compiler.

## Data

The Python module generates samples inside the compiled executable:

```text
x = 1 + row
noise = repeating [+1, -1, -1, +1]
y = 3x + 7 + noise
```

Training noise satisfies both `Σnoise = 0` and `Σ(x·noise) = 0`, so OLS recovers `slope = 3` and `intercept = 7`. Each holdout row has one price unit of error, producing `MAE = 1`.

[`data/generated_samples.csv`](data/generated_samples.csv) is a human-readable snapshot. The executable does not parse or hard-code the CSV; [`python/data_generator.py`](python/data_generator.py) regenerates the same rows on the compiled cross-language path.

## Training

Rust evaluates the ordinary least-squares equations from C++ statistics:

```text
slope     = (nΣxy - ΣxΣy) / (nΣx² - (Σx)²)
intercept = (Σy - slope·Σx) / n
```

For this dataset, `n=8`, `Σx=36`, `Σy=164`, `Σx²=204`, and `Σxy=864`; the numerator is `1008` and denominator is `336`.

To keep the first compiler-native scalar ABI deterministic, Rust quantizes the positive slope to `0..8` and intercept to `0..16`; Go quantizes MAE to `0..8`. These are explicit domain bounds for this integer model and leave a clear path toward fixed-point values, multiple features, and iterative training.

## Build and test

Build `polyc` and `polyld` at the repository root, then run:

```bash
cd examples/house_price_ml
./run.sh
./test.sh
```

Expected output:

```text
house_price_ml: trained slope=3 intercept=7 holdout_mae=1 audit=142
```

`run.sh` makes one guarded compiler call:

```bash
PATH=/nonexistent ../../build/polyc \
  --strict --no-package-index --quiet \
  --polyld=../../build/polyld -O0 \
  -o build/polyc/house_price_ml house_price_ml.poly
```

The empty compiler `PATH`, explicit local `polyld`, and disabled package index make an external-tool fallback fail immediately. `main` validates the OLS numerator, denominator, learned parameters, and holdout MAE before deriving audit exit code `3×40 + 7×3 + 1 = 142`. `test.sh` also verifies all four frontend objects, 12 foreign descriptors, final binary symbols, and the single `polyc` build command.

## Files

| File | Responsibility |
| --- | --- |
| `house_price_ml.poly` | splitting, aggregation, training, prediction, assertions |
| `python/data_generator.py` | runtime synthetic data generation |
| `cpp/sufficient_statistics.cpp` | square and cross-product kernels |
| `rust/linear_regression.rs` | OLS parameter calculation and bounded integer quotient |
| `go/model_service.go` | prediction, absolute error, and MAE |
| `data/generated_samples.csv` | human-readable data snapshot |
| `run.sh` / `test.sh` | native build, execution, and integrity checks |

中文：[README_zh.md](README_zh.md)
