// Hermetic Rust source crate consumed by polyc through `--crate-dir` /
// `--extern miniutils=<path>`.  Exposes a handful of public items so the
// CrateLoader can index them without invoking cargo.

pub fn double(x: i64) -> i64 {
    x * 2
}

pub fn clamp(x: i64, lo: i64, hi: i64) -> i64 {
    if x < lo { lo } else if x > hi { hi } else { x }
}

pub const VERSION: &str = "0.3.1";

pub struct Counter {
    pub value: i64,
}

pub mod inner {
    pub fn triple(x: i64) -> i64 { x * 3 }
}
