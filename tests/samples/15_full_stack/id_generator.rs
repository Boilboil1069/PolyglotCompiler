// ============================================================================
// id_generator.rs — Rust unique-ID generator
// Compiled by PolyglotCompiler's frontend_rust → shared IR
// ============================================================================

use std::sync::atomic::{AtomicU64, Ordering};

static COUNTER: AtomicU64 = AtomicU64::new(1);

/// Generate the next unique ID as a string (e.g. "uid-00042").
pub fn next_id() -> String {
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    format!("uid-{:05}", n)
}

/// Seed the counter from a hash value so IDs are deterministic in tests.
pub fn seed_from_hash(hash: &str) -> u64 {
    let mut seed: u64 = 0;
    for b in hash.bytes() {
        seed = seed.wrapping_mul(31).wrapping_add(b as u64);
    }
    COUNTER.store(seed, Ordering::Relaxed);
    seed
}
