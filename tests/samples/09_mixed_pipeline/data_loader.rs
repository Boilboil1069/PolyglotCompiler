// ============================================================================
// data_loader.rs — Rust data loading for full pipeline demo
// Compiled by PolyglotCompiler's frontend_rust → shared IR
// ============================================================================

use std::fs;
use std::path::Path;

/// A chunk of data loaded from disk
pub struct DataChunk {
    pub data: Vec<f64>,
    pub label: String,
    pub size: usize,
}

impl DataChunk {
    /// Create a new DataChunk with the given label and size
    pub fn new(label: &str, size: usize) -> Self {
        DataChunk {
            data: vec![0.0; size],
            label: label.to_string(),
            size,
        }
    }

    /// Fill the chunk with sequential values for testing
    pub fn fill_sequential(&mut self) {
        for i in 0..self.size {
            self.data[i] = i as f64;
        }
    }

    /// Compute the sum of all elements
    pub fn sum(&self) -> f64 {
        self.data.iter().sum()
    }

    /// Compute the average value
    pub fn average(&self) -> f64 {
        if self.size == 0 {
            return 0.0;
        }
        self.sum() / self.size as f64
    }
}

/// Load data from a file path (simulated)
pub fn load_data(path: &str, chunk_size: usize) -> Vec<DataChunk> {
    let mut chunks = Vec::new();
    // Simulate loading 3 chunks
    for i in 0..3 {
        let mut chunk = DataChunk::new(&format!("chunk_{}", i), chunk_size);
        chunk.fill_sequential();
        chunks.push(chunk);
    }
    chunks
}

/// Parallel map over data (simulated rayon-style)
pub fn parallel_map(data: &[f64], factor: f64) -> Vec<f64> {
    data.iter().map(|&x| x * factor).collect()
}

/// Merge multiple chunks into a single vector
pub fn merge_chunks(chunks: &[DataChunk]) -> Vec<f64> {
    let mut result = Vec::new();
    for chunk in chunks {
        result.extend_from_slice(&chunk.data);
    }
    result
}
