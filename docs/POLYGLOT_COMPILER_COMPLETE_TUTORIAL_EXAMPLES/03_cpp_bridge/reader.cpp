#include <cstdint>
#include <iostream>

extern "C" std::int32_t read_count(std::int32_t /*unused*/) {
    return 3;
}

extern "C" void print_count(std::int32_t count) {
    std::cout << "rows=" << count << '\n';
}

