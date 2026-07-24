/**
 * @file polyrt_file.h
 * @brief Stable scalar C ABI for polyc's automatically linked file runtime.
 */
#pragma once

#if defined(__cplusplus)
extern "C" {
#endif

/** Open `path` read-only. Returns a non-negative descriptor or a negative value. */
long polyrt_open_read(const char *path);

/**
 * Parse the next signed decimal integer from a CSV/text stream.  Non-numeric
 * bytes (headers, commas, whitespace) are skipped.  `eof_value` is returned
 * when no further record is available or a read fails.
 */
long polyrt_read_i64_or(long fd, long eof_value);

/** Close a descriptor returned by polyrt_open_read. Returns zero on success. */
long polyrt_close_read(long fd);

/**
 * Open `path` for write-only creation, truncating an existing file. Returns a
 * non-negative descriptor or a negative value.
 */
long polyrt_open_write(const char *path);

/** Write the NUL-terminated bytes in `text`. Returns the kernel write result. */
long polyrt_write_text(long fd, const char *text);

/**
 * Write `value` as signed decimal text. If `separator` is non-zero, its low
 * byte is appended to the same write (for example '\n' or ',').
 */
long polyrt_write_i64(long fd, long value, long separator);

#if defined(__cplusplus)
} // extern "C"
#endif
