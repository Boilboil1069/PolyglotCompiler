// box_blur.rs — 3x3 box blur over a flat greyscale image.
// Part of the PolyglotCompiler sample matrix.

#![allow(dead_code)]

pub fn blur(input: &[u8], width: u32, height: u32) -> Vec<u8> {
    let w = width as usize;
    let h = height as usize;
    let mut out = vec![0u8; input.len()];
    for y in 1..h.saturating_sub(1) {
        for x in 1..w.saturating_sub(1) {
            let mut acc: u32 = 0;
            for dy in -1i32..=1 {
                for dx in -1i32..=1 {
                    let ix = (y as i32 + dy) as usize * w + (x as i32 + dx) as usize;
                    acc += input[ix] as u32;
                }
            }
            out[y * w + x] = (acc / 9) as u8;
        }
    }
    out
}

