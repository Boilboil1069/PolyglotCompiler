# Test Explorer, Inline Run-Test, and Coverage View

## Goal

Surface every CTest, pytest, cargo test, JUnit, xUnit, and NUnit suite
inside the polyui shell as a single, navigable Test Explorer tree;
allow individual cases to be launched (▶ Run) or attached to (🐞 Debug)
directly from the editor margin; and overlay coverage information on the
gutter from the five most common report formats.

## Components

| Module | Responsibility |
| --- | --- |
| [`tools/ui/common/testing/test_model.h`](../../tools/ui/common/testing/test_model.h) | Hierarchical `TestNode` tree, status book-keeping, failure-first ordering, summary aggregation. |
| [`tools/ui/common/testing/test_model.cpp`](../../tools/ui/common/testing/test_model.cpp) | Five report parsers — CTest CDash XML, JUnit / pytest XML, cargo libtest JSON line stream, xUnit v2, NUnit 3. |
| [`tools/ui/common/testing/inline_test_lens.h`](../../tools/ui/common/testing/inline_test_lens.h) | Per-line CodeLens descriptor (`Lens`) with Run + Debug actions, language-specific detector registry, and inline failure stamping. |
| [`tools/ui/common/testing/inline_test_lens.cpp`](../../tools/ui/common/testing/inline_test_lens.cpp) | Built-in detectors for Catch2, pytest, cargo (Rust `#[test]`), JUnit (`@Test`), xUnit (`[Fact]`/`[Theory]`), NUnit (`[Test]`). |
| [`tools/ui/common/testing/coverage_model.h`](../../tools/ui/common/testing/coverage_model.h) | `FileCoverage` value type, workspace aggregate, threshold filter. |
| [`tools/ui/common/testing/coverage_model.cpp`](../../tools/ui/common/testing/coverage_model.cpp) | Parsers for lcov, Cobertura / coverage.py, cargo-tarpaulin JSON, dotnet coverlet JSON. |

## Pipelines

### Discovery

1. The runner emits a report (CTest XML, JUnit XML, cargo libtest JSON,
   xUnit XML, NUnit XML).
2. The corresponding `Parse*Report` returns a flat vector of
   `TestNode`.
3. Each node is `Upsert`'d into `TestModel`, preserving insertion order
   so the Test Explorer renders deterministically.

### Inline run

1. On document open / change the IDE asks
   `InlineTestLens::ComputeForFile`.
2. The detector matching the file extension scans for canonical test
   declarations and emits one `Lens` per match with both `kRun` and
   `kDebug` actions.
3. After a run, the runner calls `RecordFailure(file, line, message)`
   so the cached lens carries the diagnostic for inline rendering.

### Coverage

1. `CoverageModel::Load(text)` sniffs the first 2 KB to pick a parser,
   or the caller can force the format.
2. Each file becomes a `FileCoverage` with a `line → hit-count` map.
3. The shell paints gutter bars from `line_hits`, lists files via
   `Files()`, and fires alerts via `BelowThreshold(threshold)`.

## Tests

* [`tests/unit/polyui/test_model_test.cpp`](../../tests/unit/polyui/test_model_test.cpp) — model semantics + every report parser.
* [`tests/unit/polyui/inline_test_lens_test.cpp`](../../tests/unit/polyui/inline_test_lens_test.cpp) — detector coverage for all five frameworks plus failure round-trip.
* [`tests/unit/polyui/coverage_model_test.cpp`](../../tests/unit/polyui/coverage_model_test.cpp) — format detection, all five parsers, threshold filter, overall percent.
