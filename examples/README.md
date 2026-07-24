# Complete examples

- [`iris_native_ml`](iris_native_ml/README.md) — fixed-point Iris ML with balanced minibatch regression, a trained decision tree, nearest-centroid classification, Pearson/ANOVA tests, vendored-package import, model persistence, and a runtime-generated SVG; Python/C++/Rust/Go are all compiled by PolyglotCompiler's own frontends.
- [`house_price_ml`](house_price_ml/README.md) — real univariate regression with Python data generation, C++ statistics, Rust training, and Go evaluation, built only by the repository's frontends and linker.
- [`customer_retention`](customer_retention/README.md) — a directly openable, zero-build HTML retention dashboard; Python generates telemetry, Java weighs activity, and JavaScript computes health, all through local PolyglotCompiler frontends, while the native executable generates the page data without external language compilers.
- [`order_risk_analyzer`](order_risk_analyzer/README.md) — one `polyc` command builds a data-driven Poly → C++ → Python → Rust → Go order engine with 16 foreign calls, object state/methods in all four languages, four local vendored packages, CSV streaming, and four business outcomes.
