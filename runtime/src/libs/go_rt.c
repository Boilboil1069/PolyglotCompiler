/**
 * @file     go_rt.c
 * @brief    Implementation of the Go runtime support library.
 *
 * @details  Re-implements the slice of Go's runtime that lowered IR depends on:
 *           goroutine launches (mapped to OS threads), bounded MPMC channels
 *           with closed-state semantics, deferred-call stacks and the
 *           print/strdup helpers shared by every language runtime.
 *
 *           The implementation is intentionally portable C and uses the
 *           polyglot raw heap (mimalloc) for all bookkeeping that the GC is
 *           not aware of (channel ring buffers, defer nodes, routine handles).
 *           Payloads stored inside channels are copied byte-for-byte using
 *           `polyglot_memcpy`, mirroring Go's value semantics.
 *
 * @ingroup  Runtime / Libs
 * @author   Manning Cyrus
 * @date     2026-04-26
 */

#include "runtime/include/libs/go_rt.h"

#include <stdio.h>
#include <string.h>

#include "runtime/include/libs/base.h"
#include "runtime/include/memory/polyglot_alloc.h"

// ----------------------------------------------------------------------------
// Threading / synchronisation primitives.  We intentionally do not pull in
// <thread> or std::mutex because this translation unit is plain C; instead we
// use the platform native API directly so the runtime can be linked into
// embedded contexts without a C++ standard library.
// ----------------------------------------------------------------------------

#ifdef _WIN32
#include <windows.h>
typedef HANDLE polyglot_thread_t;
typedef CRITICAL_SECTION polyglot_mutex_t;
typedef CONDITION_VARIABLE polyglot_cond_t;

static void polyglot_mutex_init(polyglot_mutex_t *m) { InitializeCriticalSection(m); }
static void polyglot_mutex_destroy(polyglot_mutex_t *m) { DeleteCriticalSection(m); }
static void polyglot_mutex_lock(polyglot_mutex_t *m) { EnterCriticalSection(m); }
static void polyglot_mutex_unlock(polyglot_mutex_t *m) { LeaveCriticalSection(m); }
static void polyglot_cond_init(polyglot_cond_t *c) { InitializeConditionVariable(c); }
static void polyglot_cond_destroy(polyglot_cond_t *c) { (void)c; }
static void polyglot_cond_wait(polyglot_cond_t *c, polyglot_mutex_t *m) {
  SleepConditionVariableCS(c, m, INFINITE);
}
static void polyglot_cond_signal(polyglot_cond_t *c) { WakeConditionVariable(c); }
static void polyglot_cond_broadcast(polyglot_cond_t *c) { WakeAllConditionVariable(c); }

typedef DWORD polyglot_thread_ret_t;
#define POLYGLOT_THREAD_CALL WINAPI

static int polyglot_thread_start(polyglot_thread_t *out,
                                 polyglot_thread_ret_t(POLYGLOT_THREAD_CALL *entry)(void *),
                                 void *arg) {
  *out = CreateThread(NULL, 0, entry, arg, 0, NULL);
  return *out ? 0 : -1;
}
static void polyglot_thread_join(polyglot_thread_t t) {
  WaitForSingleObject(t, INFINITE);
  CloseHandle(t);
}
static void polyglot_thread_detach(polyglot_thread_t t) { CloseHandle(t); }
static void polyglot_thread_yield(void) { SwitchToThread(); }

#include <sysinfoapi.h>
static int polyglot_num_cpu_native(void) {
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  return (int)info.dwNumberOfProcessors;
}
#else  // POSIX
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
typedef pthread_t polyglot_thread_t;
typedef pthread_mutex_t polyglot_mutex_t;
typedef pthread_cond_t polyglot_cond_t;

static void polyglot_mutex_init(polyglot_mutex_t *m) { pthread_mutex_init(m, NULL); }
static void polyglot_mutex_destroy(polyglot_mutex_t *m) { pthread_mutex_destroy(m); }
static void polyglot_mutex_lock(polyglot_mutex_t *m) { pthread_mutex_lock(m); }
static void polyglot_mutex_unlock(polyglot_mutex_t *m) { pthread_mutex_unlock(m); }
static void polyglot_cond_init(polyglot_cond_t *c) { pthread_cond_init(c, NULL); }
static void polyglot_cond_destroy(polyglot_cond_t *c) { pthread_cond_destroy(c); }
static void polyglot_cond_wait(polyglot_cond_t *c, polyglot_mutex_t *m) { pthread_cond_wait(c, m); }
static void polyglot_cond_signal(polyglot_cond_t *c) { pthread_cond_signal(c); }
static void polyglot_cond_broadcast(polyglot_cond_t *c) { pthread_cond_broadcast(c); }

typedef void *polyglot_thread_ret_t;
#define POLYGLOT_THREAD_CALL  /* nothing */

static int polyglot_thread_start(polyglot_thread_t *out,
                                 polyglot_thread_ret_t(POLYGLOT_THREAD_CALL *entry)(void *),
                                 void *arg) {
  return pthread_create(out, NULL, entry, arg);
}
static void polyglot_thread_join(polyglot_thread_t t) { pthread_join(t, NULL); }
static void polyglot_thread_detach(polyglot_thread_t t) { pthread_detach(t); }
static void polyglot_thread_yield(void) { sched_yield(); }
static int polyglot_num_cpu_native(void) {
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  return n > 0 ? (int)n : 1;
}
#endif

// ----------------------------------------------------------------------------
// Basic helpers
// ----------------------------------------------------------------------------

void polyglot_go_print(const char *message) {
  if (!message) return;
  // Go's fmt.Println terminates with '\n'.
  printf("%s\n", message);
}

char *polyglot_go_strdup_gc(const char *message, void ***root_handle_out) {
  if (!message) return NULL;
  size_t len = strlen(message) + 1;
  char *buf = (char *)polyglot_alloc(len);
  if (!buf) return NULL;
  memcpy(buf, message, len);
  polyglot_gc_register_root((void **)&buf);
  if (root_handle_out) *root_handle_out = (void **)&buf;
  return buf;
}

void polyglot_go_release(char **ptr, void ***root_handle) {
  if (!ptr || !*ptr) return;
  polyglot_gc_unregister_root((void **)ptr);
  *ptr = NULL;
  if (root_handle) *root_handle = NULL;
}

// ----------------------------------------------------------------------------
// Goroutine implementation
// ----------------------------------------------------------------------------

struct polyglot_go_routine {
  polyglot_thread_t thread;
  void (*fn)(void *);
  void *arg;
  int detached;
};

static polyglot_thread_ret_t POLYGLOT_THREAD_CALL go_routine_trampoline(void *raw) {
  struct polyglot_go_routine *r = (struct polyglot_go_routine *)raw;
  if (r && r->fn) r->fn(r->arg);
  if (r && r->detached) {
    // Detached goroutines own their handle and must self-destruct.
    polyglot_raw_free(r);
  }
  return (polyglot_thread_ret_t)0;
}

polyglot_go_routine_t *polyglot_go_spawn(void (*fn)(void *), void *arg) {
  if (!fn) return NULL;
  struct polyglot_go_routine *r =
      (struct polyglot_go_routine *)polyglot_raw_malloc(sizeof(*r));
  if (!r) return NULL;
  r->fn = fn;
  r->arg = arg;
  r->detached = 0;
  if (polyglot_thread_start(&r->thread, go_routine_trampoline, r) != 0) {
    polyglot_raw_free(r);
    return NULL;
  }
  return r;
}

void polyglot_go_join(polyglot_go_routine_t *r) {
  if (!r) return;
  polyglot_thread_join(r->thread);
  polyglot_raw_free(r);
}

void polyglot_go_detach(polyglot_go_routine_t *r) {
  if (!r) return;
  r->detached = 1;
  polyglot_thread_detach(r->thread);
  // Memory is released by the trampoline once the thread exits.
}

void polyglot_go_yield(void) { polyglot_thread_yield(); }

int polyglot_go_num_cpu(void) {
  int n = polyglot_num_cpu_native();
  return n > 0 ? n : 1;
}

// ----------------------------------------------------------------------------
// Bounded channel implementation (MPMC ring buffer with closed-state flag).
// Unbuffered (capacity == 0) channels degenerate to a single-slot rendezvous
// that requires both peers to be present, matching Go's `chan T` of zero size.
// ----------------------------------------------------------------------------

struct polyglot_go_chan {
  size_t elem_size;
  size_t capacity;       // 0 means rendezvous channel
  size_t head;           // index of next element to read
  size_t tail;           // index of next element to write
  size_t count;          // number of elements currently buffered
  unsigned char *buffer; // capacity * elem_size bytes (NULL when capacity==0)

  // Rendezvous slot used when capacity == 0.
  unsigned char *rendezvous;
  int rendezvous_full;

  int closed;
  polyglot_mutex_t mu;
  polyglot_cond_t not_empty;
  polyglot_cond_t not_full;
};

polyglot_go_chan_t *polyglot_go_chan_make(size_t elem_size, size_t capacity) {
  if (elem_size == 0) return NULL;
  struct polyglot_go_chan *ch =
      (struct polyglot_go_chan *)polyglot_raw_calloc(1, sizeof(*ch));
  if (!ch) return NULL;
  ch->elem_size = elem_size;
  ch->capacity = capacity;
  if (capacity > 0) {
    ch->buffer = (unsigned char *)polyglot_raw_malloc(capacity * elem_size);
    if (!ch->buffer) {
      polyglot_raw_free(ch);
      return NULL;
    }
  } else {
    ch->rendezvous = (unsigned char *)polyglot_raw_malloc(elem_size);
    if (!ch->rendezvous) {
      polyglot_raw_free(ch);
      return NULL;
    }
  }
  polyglot_mutex_init(&ch->mu);
  polyglot_cond_init(&ch->not_empty);
  polyglot_cond_init(&ch->not_full);
  return ch;
}

int polyglot_go_chan_send(polyglot_go_chan_t *ch, const void *value) {
  if (!ch || !value) return -1;
  polyglot_mutex_lock(&ch->mu);
  if (ch->closed) {
    polyglot_mutex_unlock(&ch->mu);
    return -1;
  }
  if (ch->capacity == 0) {
    // Rendezvous: wait for the slot to be free, deposit, signal receiver and
    // wait again for the receiver to consume it before returning.
    while (ch->rendezvous_full && !ch->closed) {
      polyglot_cond_wait(&ch->not_full, &ch->mu);
    }
    if (ch->closed) {
      polyglot_mutex_unlock(&ch->mu);
      return -1;
    }
    memcpy(ch->rendezvous, value, ch->elem_size);
    ch->rendezvous_full = 1;
    polyglot_cond_signal(&ch->not_empty);
    while (ch->rendezvous_full && !ch->closed) {
      polyglot_cond_wait(&ch->not_full, &ch->mu);
    }
  } else {
    while (ch->count == ch->capacity && !ch->closed) {
      polyglot_cond_wait(&ch->not_full, &ch->mu);
    }
    if (ch->closed) {
      polyglot_mutex_unlock(&ch->mu);
      return -1;
    }
    memcpy(ch->buffer + ch->tail * ch->elem_size, value, ch->elem_size);
    ch->tail = (ch->tail + 1) % ch->capacity;
    ++ch->count;
    polyglot_cond_signal(&ch->not_empty);
  }
  polyglot_mutex_unlock(&ch->mu);
  return 0;
}

int polyglot_go_chan_recv(polyglot_go_chan_t *ch, void *out) {
  if (!ch || !out) return 0;
  polyglot_mutex_lock(&ch->mu);
  if (ch->capacity == 0) {
    while (!ch->rendezvous_full && !ch->closed) {
      polyglot_cond_wait(&ch->not_empty, &ch->mu);
    }
    if (!ch->rendezvous_full && ch->closed) {
      polyglot_mutex_unlock(&ch->mu);
      return 0;  // closed and drained
    }
    memcpy(out, ch->rendezvous, ch->elem_size);
    ch->rendezvous_full = 0;
    // Wake either the sender waiting for handover, or other senders waiting
    // for the slot to clear.
    polyglot_cond_broadcast(&ch->not_full);
  } else {
    while (ch->count == 0 && !ch->closed) {
      polyglot_cond_wait(&ch->not_empty, &ch->mu);
    }
    if (ch->count == 0 && ch->closed) {
      polyglot_mutex_unlock(&ch->mu);
      return 0;  // closed and drained
    }
    memcpy(out, ch->buffer + ch->head * ch->elem_size, ch->elem_size);
    ch->head = (ch->head + 1) % ch->capacity;
    --ch->count;
    polyglot_cond_signal(&ch->not_full);
  }
  polyglot_mutex_unlock(&ch->mu);
  return 1;
}

void polyglot_go_chan_close(polyglot_go_chan_t *ch) {
  if (!ch) return;
  polyglot_mutex_lock(&ch->mu);
  ch->closed = 1;
  polyglot_cond_broadcast(&ch->not_empty);
  polyglot_cond_broadcast(&ch->not_full);
  polyglot_mutex_unlock(&ch->mu);
}

void polyglot_go_chan_destroy(polyglot_go_chan_t *ch) {
  if (!ch) return;
  polyglot_mutex_destroy(&ch->mu);
  polyglot_cond_destroy(&ch->not_empty);
  polyglot_cond_destroy(&ch->not_full);
  if (ch->buffer) polyglot_raw_free(ch->buffer);
  if (ch->rendezvous) polyglot_raw_free(ch->rendezvous);
  polyglot_raw_free(ch);
}

// ----------------------------------------------------------------------------
// Defer stack — a small per-thread linked list keyed by `frame` pointer.
// Each frame's deferred calls are popped in LIFO order at run time.
// ----------------------------------------------------------------------------

typedef struct polyglot_go_defer_node {
  void *frame;
  void (*fn)(void *);
  void *arg;
  struct polyglot_go_defer_node *next;
} polyglot_go_defer_node_t;

#ifdef _WIN32
static __declspec(thread) polyglot_go_defer_node_t *tls_defer_head_ = NULL;
#else
static __thread polyglot_go_defer_node_t *tls_defer_head_ = NULL;
#endif

void polyglot_go_defer_push(void *frame, void (*fn)(void *), void *arg) {
  if (!fn) return;
  polyglot_go_defer_node_t *n =
      (polyglot_go_defer_node_t *)polyglot_raw_malloc(sizeof(*n));
  if (!n) return;
  n->frame = frame;
  n->fn = fn;
  n->arg = arg;
  n->next = tls_defer_head_;
  tls_defer_head_ = n;
}

void polyglot_go_defer_run(void *frame) {
  // Pop the contiguous run of nodes belonging to `frame` and execute them in
  // LIFO order.  We splice the matching prefix into a local list so any new
  // defers a callback registers go onto a fresh, independent suffix.
  polyglot_go_defer_node_t *local = NULL;
  while (tls_defer_head_ && tls_defer_head_->frame == frame) {
    polyglot_go_defer_node_t *n = tls_defer_head_;
    tls_defer_head_ = n->next;
    n->next = local;
    local = n;
  }
  // The list `local` is now in original push order; iterate while popping to
  // execute LIFO.  (Push order reverses when splicing; the second reverse here
  // restores LIFO.)
  polyglot_go_defer_node_t *cursor = local;
  // Reverse `local` in place so the most recently pushed defer fires first.
  polyglot_go_defer_node_t *reversed = NULL;
  while (cursor) {
    polyglot_go_defer_node_t *next = cursor->next;
    cursor->next = reversed;
    reversed = cursor;
    cursor = next;
  }
  while (reversed) {
    polyglot_go_defer_node_t *n = reversed;
    reversed = n->next;
    if (n->fn) n->fn(n->arg);
    polyglot_raw_free(n);
  }
}
