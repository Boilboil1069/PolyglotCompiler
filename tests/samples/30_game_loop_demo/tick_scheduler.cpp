// tick_scheduler.cpp — Monotonic tick counter with a fixed step budget.
// Part of the PolyglotCompiler sample matrix.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" {

namespace {
unsigned long long g_tick = 0;
}

unsigned long long polyglot_next_tick(unsigned long long max_ticks) {
    if (g_tick >= max_ticks) return max_ticks;
    return ++g_tick;
}

void polyglot_reset_ticks() { g_tick = 0; }


}  // extern "C"
