/**
 * @file     go_rt.h
 * @brief    Go (1.18+) language runtime support library.
 *
 * @details  Go programs typically own their entire process and embed their own
 *           garbage collector and goroutine scheduler.  Embedding the upstream
 *           Go runtime into a host C/C++ process (the inverse of cgo) is only
 *           possible via gc compiler `-buildmode=c-archive`, which would force
 *           a hard build-time dependency on the Go toolchain.  PolyglotCompiler
 *           therefore re-implements the *minimum runtime surface* that lowered
 *           Go IR needs at execution time:
 *
 *             - println / strdup helpers backed by polyglot_alloc (the GC heap)
 *             - goroutine launch primitive that runs work on an OS thread
 *             - bounded channel (MPMC) primitive used by `chan` lowering
 *             - defer stack so deferred calls fire in LIFO at function return
 *
 *           This is sufficient for the polyglot pipeline to lower `go f()`,
 *           `chan T`, `defer` and the standard print builtins, while keeping
 *           the runtime self-contained.  When applications need the full Go
 *           standard library, the resulting object can still be linked
 *           against gccgo-produced archives.
 *
 * @ingroup  Runtime / Libs
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ----------------------------------------------------------------------------
// Basic helpers (mirror python_rt / rust_rt for cross-language uniformity).
// ----------------------------------------------------------------------------

// Print a message followed by '\n' (the Go `fmt.Println` semantic).
void polyglot_go_print(const char *message);

// Duplicate a C string into GC-managed memory and root the slot.
// Caller must release via polyglot_go_release.
char *polyglot_go_strdup_gc(const char *message, void ***root_handle_out);

// Release a GC-rooted string previously returned by polyglot_go_strdup_gc.
void polyglot_go_release(char **ptr, void ***root_handle);

// ----------------------------------------------------------------------------
// Goroutine scheduling.
// ----------------------------------------------------------------------------

// Opaque goroutine handle.  Internally an OS thread; the caller never owns
// the underlying thread object directly so it can be transparently swapped
// for an M:N scheduler in a future revision.
typedef struct polyglot_go_routine polyglot_go_routine_t;

// Launch a goroutine that calls `fn(arg)` and exits.  Returns NULL on failure.
// The returned handle must be released with polyglot_go_join (which waits
// for completion) or polyglot_go_detach (fire-and-forget).
polyglot_go_routine_t *polyglot_go_spawn(void (*fn)(void *), void *arg);

// Block until the goroutine finishes and free its handle.
void polyglot_go_join(polyglot_go_routine_t *r);

// Detach the goroutine: it will run to completion and self-destruct.
void polyglot_go_detach(polyglot_go_routine_t *r);

// Yield the current goroutine to the OS scheduler (Go's `runtime.Gosched`).
void polyglot_go_yield(void);

// Number of CPUs available for scheduling (Go's `runtime.NumCPU`).
int polyglot_go_num_cpu(void);

// ----------------------------------------------------------------------------
// Bounded channel (MPMC).  capacity == 0 means an unbuffered (rendezvous)
// channel which forces sender and receiver to synchronise on every op.
// ----------------------------------------------------------------------------

typedef struct polyglot_go_chan polyglot_go_chan_t;

// Create a channel that carries `elem_size`-byte payloads with the given
// capacity.  Returns NULL on allocation failure.
polyglot_go_chan_t *polyglot_go_chan_make(size_t elem_size, size_t capacity);

// Send `value` onto the channel.  Blocks while the channel is full.
// Returns 0 on success, -1 if the channel was already closed.
int polyglot_go_chan_send(polyglot_go_chan_t *ch, const void *value);

// Receive into `out`.  Blocks while empty.  Returns 1 on a fresh value, 0 if
// the channel was closed *and* drained (the standard Go `v, ok := <-ch`).
int polyglot_go_chan_recv(polyglot_go_chan_t *ch, void *out);

// Close a channel.  Subsequent sends fail; pending receives still drain.
void polyglot_go_chan_close(polyglot_go_chan_t *ch);

// Free a channel (must be already closed and drained).
void polyglot_go_chan_destroy(polyglot_go_chan_t *ch);

// ----------------------------------------------------------------------------
// Deferred call stack (Go's `defer`).
// ----------------------------------------------------------------------------

// Push a deferred callback for the current function frame.  `frame` is an
// opaque key — callers typically pass the address of a stack variable so each
// call site has a distinct identity.  Calls are executed in LIFO order when
// polyglot_go_defer_run is invoked with the same frame.
void polyglot_go_defer_push(void *frame, void (*fn)(void *), void *arg);

// Drain and invoke all deferred callbacks registered against `frame`.
void polyglot_go_defer_run(void *frame);

#ifdef __cplusplus
}
#endif
