/**
 * @file     root_guard.h
 * @brief    Garbage collection subsystem
 *
 * @ingroup  Runtime / GC
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include "runtime/include/libs/base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct polyglot_root_guard {
  void **slot;
} polyglot_root_guard;

static inline void polyglot_root_guard_init(polyglot_root_guard *g, void **slot) {
  if (!g) return;
  g->slot = slot;
  if (g->slot) polyglot_gc_register_root(g->slot);
}

static inline void polyglot_root_guard_release(polyglot_root_guard *g) {
  if (!g || !g->slot) return;
  polyglot_gc_unregister_root(g->slot);
  g->slot = NULL;
}

#if defined(__GNUC__)
#define POLYGLOT_WITH_ROOT(name, slot_ptr) \
  polyglot_root_guard name __attribute__((cleanup(polyglot_root_guard_release))) = {0}; \
  polyglot_root_guard_init(&name, (void **)(slot_ptr))
#else
#define POLYGLOT_WITH_ROOT(name, slot_ptr) \
  polyglot_root_guard name; \
  polyglot_root_guard_init(&name, (void **)(slot_ptr))
#endif

#ifdef __cplusplus
}
#endif
