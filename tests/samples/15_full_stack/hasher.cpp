// ============================================================================
// hasher.cpp — C++ hashing / crypto stub functions
// Compiled by PolyglotCompiler's frontend_cpp → shared IR
// ============================================================================

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

// Minimal FNV-1a hash (used as a SHA-256 stub for demo purposes)
static uint64_t fnv1a(const std::string& data) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : data) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

// Return a hex-string "hash" of the input (stub for SHA-256)
std::string sha256_hex(const std::string& input) {
    uint64_t h1 = fnv1a(input);
    uint64_t h2 = fnv1a(input + "salt");
    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(16) << h1
        << std::setw(16) << h2;
    return oss.str();
}

// Verify a password against a stored hash
bool verify_hash(const std::string& password, const std::string& stored_hash) {
    return sha256_hex(password) == stored_hash;
}
