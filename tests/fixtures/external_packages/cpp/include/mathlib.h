// ============================================================================
// Test fixture: a minimal "third-party" header consumed by the polyc
// preprocessor through the -I include-search path.  No system headers are
// referenced so the test stays fully hermetic.
//
// See tests/integration/external_packages/demand_03_test.cpp
// ============================================================================
#ifndef MATHLIB_INCLUDE_MATHLIB_H
#define MATHLIB_INCLUDE_MATHLIB_H

#define MATHLIB_VERSION_MAJOR 4
#define MATHLIB_VERSION_MINOR 2

// A trivial public surface that the consumer .cpp will reference.
int mathlib_add(int a, int b);
int mathlib_mul(int a, int b);

#endif  // MATHLIB_INCLUDE_MATHLIB_H
