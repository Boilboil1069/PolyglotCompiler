// transition_table.cpp — Two-state IDLE/BUSY transition table.
// Part of the PolyglotCompiler sample matrix.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" {

// States: 0 = IDLE, 1 = BUSY.  Events: 0 = START, 1 = DONE.
int polyglot_next_state(int current, int event) {
    if (current == 0 && event == 0) return 1;
    if (current == 1 && event == 1) return 0;
    return current;
}

int polyglot_state_count() { return 2; }


}  // extern "C"
