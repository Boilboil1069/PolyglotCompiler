/**
 * @file     ruby_rt.c
 * @brief    Implementation of the Ruby (MRI / CRuby) runtime support library.
 *
 * @details  Loads libruby dynamically and exposes a flat C bridge that the
 *           polyglot pipeline calls from lowered Ruby IR.  Mirrors the
 *           "library loader + symbol resolver + GC-rooted handles" pattern
 *           already established by java_rt and dotnet_rt so all language
 *           runtimes look and feel identical to embedders.
 *
 *           When libruby cannot be located, every entry point that requires
 *           a live VM returns NULL / -1 while `polyglot_ruby_print` and the
 *           strdup helpers continue to work via libc.  This keeps simple
 *           lowered "puts" programs functional on minimal hosts.
 *
 * @ingroup  Runtime / Libs
 * @author   Manning Cyrus
 * @date     2026-04-26
 */

#include "runtime/include/libs/ruby_rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/include/libs/base.h"
#include "runtime/include/memory/polyglot_alloc.h"

#ifdef _WIN32
#include <windows.h>
typedef HMODULE rb_lib_t;
#define RB_LOAD(path)     LoadLibraryA(path)
#define RB_SYM(lib, name) ((void *)GetProcAddress((lib), (name)))
#define RB_UNLOAD(lib)    FreeLibrary(lib)
#else
#include <dlfcn.h>
typedef void *rb_lib_t;
#define RB_LOAD(path)     dlopen((path), RTLD_LAZY | RTLD_GLOBAL)
#define RB_SYM(lib, name) dlsym((lib), (name))
#define RB_UNLOAD(lib)    dlclose(lib)
#endif

// ----------------------------------------------------------------------------
// Stable libruby ABI surface — declared as opaque so we never include ruby.h
// at compile time.  All structures are referenced as `void *`; CRuby's VALUE
// is "uintptr_t but always pointer-sized" which we represent as `void *`.
// ----------------------------------------------------------------------------

typedef void (*ruby_init_fn)(void);
typedef void (*ruby_finalize_fn)(void);
typedef int (*ruby_setup_fn)(void);
typedef int (*ruby_cleanup_fn)(int);
typedef void (*ruby_init_loadpath_fn)(void);

typedef void *(*rb_eval_string_protect_fn)(const char *, int *);
typedef void *(*rb_funcallv_fn)(void *, int /*ID*/, int, const void *const *);
typedef int (*rb_intern_fn)(const char *);
typedef void *(*rb_str_new_cstr_fn)(const char *);
typedef const char *(*rb_string_value_cstr_fn)(void **);
typedef void *(*rb_int2inum_fn)(long long);
typedef void *(*rb_float_new_fn)(double);
typedef void *(*rb_const_get_fn)(void *, int /*ID*/);
typedef void (*rb_gc_register_address_fn)(void **);
typedef void (*rb_gc_unregister_address_fn)(void **);
typedef void *(*rb_obj_as_string_fn)(void *);
typedef int (*rb_require_fn)(const char *);

// CRuby's `rb_cObject` is a global VALUE exported by libruby; we look it up
// through dlsym after the library is open.
typedef void *rb_cObject_ptr_t;

// ----------------------------------------------------------------------------
// Engine state
// ----------------------------------------------------------------------------

static rb_lib_t rb_lib_ = NULL;
static int rb_initialised_ = 0;
static int rb_version_hint_ = 0;

static ruby_init_fn ruby_init_ = NULL;
static ruby_finalize_fn ruby_finalize_ = NULL;
static ruby_setup_fn ruby_setup_ = NULL;
static ruby_cleanup_fn ruby_cleanup_ = NULL;
static ruby_init_loadpath_fn ruby_init_loadpath_ = NULL;

static rb_eval_string_protect_fn rb_eval_string_protect_ = NULL;
static rb_funcallv_fn rb_funcallv_ = NULL;
static rb_intern_fn rb_intern_ = NULL;
static rb_str_new_cstr_fn rb_str_new_cstr_ = NULL;
static rb_string_value_cstr_fn rb_string_value_cstr_ = NULL;
static rb_int2inum_fn rb_int2inum_ = NULL;
static rb_float_new_fn rb_float_new_ = NULL;
static rb_const_get_fn rb_const_get_ = NULL;
static rb_gc_register_address_fn rb_gc_register_address_ = NULL;
static rb_gc_unregister_address_fn rb_gc_unregister_address_ = NULL;
static rb_obj_as_string_fn rb_obj_as_string_ = NULL;
static rb_require_fn rb_require_ = NULL;
static void *rb_cObject_value_ = NULL;

// ----------------------------------------------------------------------------
// Value descriptor — matches javascript_rt's design so callers see a single
// uniform "language value" abstraction.
// ----------------------------------------------------------------------------

typedef enum {
  POLYGLOT_RUBY_VAL_VM = 0,  // a real VALUE rooted via rb_gc_register_address
  POLYGLOT_RUBY_VAL_STRING,
  POLYGLOT_RUBY_VAL_INT,
  POLYGLOT_RUBY_VAL_FLOAT
} polyglot_ruby_val_kind_t;

typedef struct {
  polyglot_ruby_val_kind_t kind;
  union {
    void *vm_value;  // VALUE; rooted while `rooted` is true
    char *str;
    long long i64;
    double f64;
  } as;
  int rooted;  // VM-only: whether rb_gc_register_address has been called
} polyglot_ruby_value_t;

// ----------------------------------------------------------------------------
// libruby loading
// ----------------------------------------------------------------------------

static int probe_libruby(const char *base) {
  char path[1024];
#ifdef _WIN32
  snprintf(path, sizeof(path), "%s\\bin\\x64-vcruntime140-ruby310.dll", base);
  rb_lib_ = RB_LOAD(path);
  if (rb_lib_) return 0;
  snprintf(path, sizeof(path), "%s\\bin\\x64-msvcrt-ruby310.dll", base);
  rb_lib_ = RB_LOAD(path);
  if (rb_lib_) return 0;
  snprintf(path, sizeof(path), "%s\\bin\\msvcrt-ruby270.dll", base);
  rb_lib_ = RB_LOAD(path);
#elif defined(__APPLE__)
  snprintf(path, sizeof(path), "%s/lib/libruby.dylib", base);
  rb_lib_ = RB_LOAD(path);
#else
  snprintf(path, sizeof(path), "%s/lib/libruby.so", base);
  rb_lib_ = RB_LOAD(path);
#endif
  return rb_lib_ ? 0 : -1;
}

static int load_libruby(void) {
  const char *override_path = getenv("POLYGLOT_RUBY_LIBRARY");
  if (override_path && override_path[0]) {
    rb_lib_ = RB_LOAD(override_path);
    if (rb_lib_) return 0;
  }
  const char *ruby_root = getenv("RUBY_ROOT");
  if (ruby_root && ruby_root[0] && probe_libruby(ruby_root) == 0) return 0;
#ifdef _WIN32
  rb_lib_ = RB_LOAD("x64-vcruntime140-ruby310.dll");
  if (!rb_lib_) rb_lib_ = RB_LOAD("x64-msvcrt-ruby310.dll");
  if (!rb_lib_) rb_lib_ = RB_LOAD("msvcrt-ruby270.dll");
#elif defined(__APPLE__)
  rb_lib_ = RB_LOAD("libruby.dylib");
#else
  rb_lib_ = RB_LOAD("libruby.so");
  if (!rb_lib_) rb_lib_ = RB_LOAD("libruby.so.3");
  if (!rb_lib_) rb_lib_ = RB_LOAD("libruby.so.2");
#endif
  return rb_lib_ ? 0 : -1;
}

static int resolve_symbols(void) {
  ruby_init_ = (ruby_init_fn)RB_SYM(rb_lib_, "ruby_init");
  ruby_finalize_ = (ruby_finalize_fn)RB_SYM(rb_lib_, "ruby_finalize");
  ruby_setup_ = (ruby_setup_fn)RB_SYM(rb_lib_, "ruby_setup");
  ruby_cleanup_ = (ruby_cleanup_fn)RB_SYM(rb_lib_, "ruby_cleanup");
  ruby_init_loadpath_ = (ruby_init_loadpath_fn)RB_SYM(rb_lib_, "ruby_init_loadpath");

  rb_eval_string_protect_ =
      (rb_eval_string_protect_fn)RB_SYM(rb_lib_, "rb_eval_string_protect");
  rb_funcallv_ = (rb_funcallv_fn)RB_SYM(rb_lib_, "rb_funcallv");
  rb_intern_ = (rb_intern_fn)RB_SYM(rb_lib_, "rb_intern");
  rb_str_new_cstr_ = (rb_str_new_cstr_fn)RB_SYM(rb_lib_, "rb_str_new_cstr");
  rb_string_value_cstr_ =
      (rb_string_value_cstr_fn)RB_SYM(rb_lib_, "rb_string_value_cstr");
  rb_int2inum_ = (rb_int2inum_fn)RB_SYM(rb_lib_, "rb_int2inum");
  rb_float_new_ = (rb_float_new_fn)RB_SYM(rb_lib_, "rb_float_new");
  rb_const_get_ = (rb_const_get_fn)RB_SYM(rb_lib_, "rb_const_get");
  rb_gc_register_address_ =
      (rb_gc_register_address_fn)RB_SYM(rb_lib_, "rb_gc_register_address");
  rb_gc_unregister_address_ =
      (rb_gc_unregister_address_fn)RB_SYM(rb_lib_, "rb_gc_unregister_address");
  rb_obj_as_string_ = (rb_obj_as_string_fn)RB_SYM(rb_lib_, "rb_obj_as_string");
  rb_require_ = (rb_require_fn)RB_SYM(rb_lib_, "rb_require");

  // `rb_cObject` is exported as a data symbol — read its current value once.
  void **cobj_slot = (void **)RB_SYM(rb_lib_, "rb_cObject");
  if (cobj_slot) rb_cObject_value_ = *cobj_slot;

  if (!ruby_setup_ && !ruby_init_) return -1;
  if (!rb_eval_string_protect_ || !rb_funcallv_ || !rb_intern_) return -1;
  return 0;
}

int polyglot_ruby_init(int version_hint) {
  rb_version_hint_ = version_hint;
  if (rb_initialised_) return 0;
  if (load_libruby() != 0) {
    fprintf(stderr,
            "[polyglot-ruby] warning: libruby not found; falling back to "
            "stand-alone helpers (puts/strdup only).\n");
    return -1;
  }
  if (resolve_symbols() != 0) {
    fprintf(stderr,
            "[polyglot-ruby] error: libruby loaded but required symbols are "
            "missing; runtime is unsupported.\n");
    RB_UNLOAD(rb_lib_);
    rb_lib_ = NULL;
    return -1;
  }
  // Prefer the modern ruby_setup over the deprecated ruby_init when present.
  if (ruby_setup_) {
    int rc = ruby_setup_();
    if (rc != 0) {
      fprintf(stderr, "[polyglot-ruby] error: ruby_setup() returned %d\n", rc);
      RB_UNLOAD(rb_lib_);
      rb_lib_ = NULL;
      return -1;
    }
  } else {
    ruby_init_();
  }
  if (ruby_init_loadpath_) ruby_init_loadpath_();
  rb_initialised_ = 1;
  return 0;
}

void polyglot_ruby_shutdown(void) {
  if (!rb_initialised_) {
    if (rb_lib_) {
      RB_UNLOAD(rb_lib_);
      rb_lib_ = NULL;
    }
    return;
  }
  if (ruby_cleanup_) {
    ruby_cleanup_(0);
  } else if (ruby_finalize_) {
    ruby_finalize_();
  }
  if (rb_lib_) {
    RB_UNLOAD(rb_lib_);
    rb_lib_ = NULL;
  }
  rb_initialised_ = 0;
}

// ----------------------------------------------------------------------------
// Always-available helpers
// ----------------------------------------------------------------------------

void polyglot_ruby_print(const char *message) {
  if (!message) return;
  printf("%s\n", message);
}

char *polyglot_ruby_strdup_gc(const char *message, void ***root_handle_out) {
  if (!message) return NULL;
  size_t len = strlen(message) + 1;
  char *buf = (char *)polyglot_alloc(len);
  if (!buf) return NULL;
  memcpy(buf, message, len);
  polyglot_gc_register_root((void **)&buf);
  if (root_handle_out) *root_handle_out = (void **)&buf;
  return buf;
}

void polyglot_ruby_release(char **ptr, void ***root_handle) {
  if (!ptr || !*ptr) return;
  polyglot_gc_unregister_root((void **)ptr);
  *ptr = NULL;
  if (root_handle) *root_handle = NULL;
}

// ----------------------------------------------------------------------------
// Engine-backed operations
// ----------------------------------------------------------------------------

static polyglot_ruby_value_t *wrap_vm_value(void *raw) {
  if (!raw) return NULL;
  polyglot_ruby_value_t *v =
      (polyglot_ruby_value_t *)polyglot_raw_calloc(1, sizeof(*v));
  if (!v) return NULL;
  v->kind = POLYGLOT_RUBY_VAL_VM;
  v->as.vm_value = raw;
  if (rb_gc_register_address_) {
    rb_gc_register_address_(&v->as.vm_value);
    v->rooted = 1;
  }
  return v;
}

int polyglot_ruby_require(const char *feature) {
  if (!rb_initialised_ || !feature || !rb_require_) return -1;
  return rb_require_(feature);
}

void *polyglot_ruby_eval(const char *source) {
  if (!rb_initialised_ || !source || !rb_eval_string_protect_) return NULL;
  int state = 0;
  void *result = rb_eval_string_protect_(source, &state);
  if (state != 0) return NULL;
  return wrap_vm_value(result);
}

void *polyglot_ruby_get_constant(const char *name) {
  if (!rb_initialised_ || !name || !rb_const_get_ || !rb_intern_ ||
      !rb_cObject_value_) {
    return NULL;
  }
  // Resolve "A::B::C" by walking through rb_cObject.
  char buf[256];
  strncpy(buf, name, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  void *current = rb_cObject_value_;
  char *cursor = buf;
  while (cursor && *cursor) {
    char *sep = strstr(cursor, "::");
    if (sep) *sep = '\0';
    int id = rb_intern_(cursor);
    current = rb_const_get_(current, id);
    if (!current) return NULL;
    if (!sep) break;
    cursor = sep + 2;
  }
  return wrap_vm_value(current);
}

void *polyglot_ruby_call_method(void *receiver, const char *method_name,
                                const void *const *args, int arg_count) {
  if (!rb_initialised_ || !method_name || !rb_funcallv_ || !rb_intern_) return NULL;
  void *recv_val = NULL;
  if (receiver) {
    polyglot_ruby_value_t *r = (polyglot_ruby_value_t *)receiver;
    recv_val = r->kind == POLYGLOT_RUBY_VAL_VM ? r->as.vm_value : NULL;
  }
  if (!recv_val) recv_val = rb_cObject_value_;  // top-level Kernel methods

  enum { kMaxArgs = 32 };
  void *argv[kMaxArgs];
  int n = arg_count > kMaxArgs ? kMaxArgs : arg_count;
  for (int i = 0; i < n; ++i) {
    argv[i] = NULL;
    polyglot_ruby_value_t *a = (polyglot_ruby_value_t *)args[i];
    if (!a) continue;
    switch (a->kind) {
      case POLYGLOT_RUBY_VAL_VM:
        argv[i] = a->as.vm_value;
        break;
      case POLYGLOT_RUBY_VAL_STRING:
        if (rb_str_new_cstr_) argv[i] = rb_str_new_cstr_(a->as.str);
        break;
      case POLYGLOT_RUBY_VAL_INT:
        if (rb_int2inum_) argv[i] = rb_int2inum_(a->as.i64);
        break;
      case POLYGLOT_RUBY_VAL_FLOAT:
        if (rb_float_new_) argv[i] = rb_float_new_(a->as.f64);
        break;
    }
  }
  int mid = rb_intern_(method_name);
  void *result = rb_funcallv_(recv_val, mid, n, (const void *const *)argv);
  return wrap_vm_value(result);
}

char *polyglot_ruby_value_to_string(void *value, void ***root_handle_out) {
  if (!value) return NULL;
  polyglot_ruby_value_t *v = (polyglot_ruby_value_t *)value;
  if (v->kind == POLYGLOT_RUBY_VAL_STRING) {
    return polyglot_ruby_strdup_gc(v->as.str, root_handle_out);
  }
  if (v->kind == POLYGLOT_RUBY_VAL_INT) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", v->as.i64);
    return polyglot_ruby_strdup_gc(buf, root_handle_out);
  }
  if (v->kind == POLYGLOT_RUBY_VAL_FLOAT) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", v->as.f64);
    return polyglot_ruby_strdup_gc(buf, root_handle_out);
  }
  if (!rb_initialised_ || !rb_obj_as_string_ || !rb_string_value_cstr_) return NULL;
  void *str_val = rb_obj_as_string_(v->as.vm_value);
  if (!str_val) return NULL;
  // rb_string_value_cstr expects a VALUE *; we hand it a local pointer copy.
  void *holder = str_val;
  const char *cstr = rb_string_value_cstr_(&holder);
  if (!cstr) return NULL;
  return polyglot_ruby_strdup_gc(cstr, root_handle_out);
}

void *polyglot_ruby_string_value(const char *utf8) {
  polyglot_ruby_value_t *v =
      (polyglot_ruby_value_t *)polyglot_raw_calloc(1, sizeof(*v));
  if (!v) return NULL;
  v->kind = POLYGLOT_RUBY_VAL_STRING;
  size_t len = utf8 ? strlen(utf8) : 0;
  v->as.str = (char *)polyglot_raw_malloc(len + 1);
  if (!v->as.str) {
    polyglot_raw_free(v);
    return NULL;
  }
  if (utf8) memcpy(v->as.str, utf8, len);
  v->as.str[len] = '\0';
  return v;
}

void *polyglot_ruby_integer_value(long long n) {
  polyglot_ruby_value_t *v =
      (polyglot_ruby_value_t *)polyglot_raw_calloc(1, sizeof(*v));
  if (!v) return NULL;
  v->kind = POLYGLOT_RUBY_VAL_INT;
  v->as.i64 = n;
  return v;
}

void *polyglot_ruby_float_value(double n) {
  polyglot_ruby_value_t *v =
      (polyglot_ruby_value_t *)polyglot_raw_calloc(1, sizeof(*v));
  if (!v) return NULL;
  v->kind = POLYGLOT_RUBY_VAL_FLOAT;
  v->as.f64 = n;
  return v;
}

void polyglot_ruby_release_value(void *value) {
  if (!value) return;
  polyglot_ruby_value_t *v = (polyglot_ruby_value_t *)value;
  switch (v->kind) {
    case POLYGLOT_RUBY_VAL_VM:
      if (v->rooted && rb_gc_unregister_address_) {
        rb_gc_unregister_address_(&v->as.vm_value);
      }
      break;
    case POLYGLOT_RUBY_VAL_STRING:
      if (v->as.str) polyglot_raw_free(v->as.str);
      break;
    case POLYGLOT_RUBY_VAL_INT:
    case POLYGLOT_RUBY_VAL_FLOAT:
      break;
  }
  polyglot_raw_free(v);
}
