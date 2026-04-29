// atomic_counter.cpp — Sequentially-consistent atomic counter helpers.
// Part of the PolyglotCompiler sample matrix.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" {

#include <atomic>

namespace {
std::atomic<long long> g_counter{0};
}

long long polyglot_increment(long long delta) {
    return g_counter.fetch_add(delta) + delta;
}

long long polyglot_load() {
    return g_counter.load();
}

void polyglot_reset() {
    g_counter.store(0);
}


}  // extern "C"
