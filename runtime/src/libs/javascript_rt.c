/**
 * @file     javascript_rt.c
 * @brief    Implementation of the JavaScript runtime support library.
 *
 * @details  Mirrors java_rt / dotnet_rt by dynamically loading a JavaScript
 *           engine at run time (no build-time dependency on Node / V8 headers).
 *
 *           The bridge tries two integration paths in order:
 *
 *             1. **Node.js embedding** — looked up via `napi_create_string_utf8`
 *                & friends from libnode.so / node.dll.  This is the primary
 *                supported path and gives a full ECMAScript runtime.
 *
 *             2. **Standalone fallback** — when no engine library can be
 *                located, `polyglot_js_eval` and `polyglot_js_call_function`
 *                return NULL but the print and strdup helpers continue to
 *                work, so simple lowered programs that only need `console.log`
 *                still execute correctly.
 *
 *           All value handles returned from this layer are heap-allocated
 *           descriptors that wrap a `napi_value` (or, when running in the
 *           standalone fallback, a typed payload).  The descriptors are
 *           allocated through `polyglot_raw_malloc` so they live on the same
 *           mimalloc heap as the rest of the runtime's bookkeeping.
 *
 * @ingroup  Runtime / Libs
 * @author   Manning Cyrus
 * @date     2026-04-26
 */

#include "runtime/include/libs/javascript_rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/include/libs/base.h"
#include "runtime/include/memory/polyglot_alloc.h"

#ifdef _WIN32
#include <windows.h>
typedef HMODULE js_lib_t;
#define JS_LOAD(path)     LoadLibraryA(path)
#define JS_SYM(lib, name) ((void *)GetProcAddress((lib), (name)))
#define JS_UNLOAD(lib)    FreeLibrary(lib)
#else
#include <dlfcn.h>
typedef void *js_lib_t;
#define JS_LOAD(path)     dlopen((path), RTLD_LAZY | RTLD_GLOBAL)
#define JS_SYM(lib, name) dlsym((lib), (name))
#define JS_UNLOAD(lib)    dlclose(lib)
#endif

// ----------------------------------------------------------------------------
// Minimal N-API surface declarations.  We only declare the function pointer
// types we actually call so the runtime does not require the node-api headers
// at build time.  All other types are kept as opaque void*.
// ----------------------------------------------------------------------------

typedef struct napi_env__ *napi_env;
typedef struct napi_value__ *napi_value;
typedef int napi_status;  // napi_ok == 0

typedef napi_status (*napi_create_string_utf8_fn)(napi_env env, const char *str,
                                                  size_t length, napi_value *result);
typedef napi_status (*napi_create_double_fn)(napi_env env, double value, napi_value *result);
typedef napi_status (*napi_get_value_double_fn)(napi_env env, napi_value value, double *result);
typedef napi_status (*napi_get_value_string_utf8_fn)(napi_env env, napi_value value, char *buf,
                                                     size_t bufsize, size_t *result_len);
typedef napi_status (*napi_get_global_fn)(napi_env env, napi_value *result);
typedef napi_status (*napi_get_named_property_fn)(napi_env env, napi_value object,
                                                  const char *utf8name, napi_value *result);
typedef napi_status (*napi_call_function_fn)(napi_env env, napi_value recv, napi_value func,
                                             size_t argc, const napi_value *argv,
                                             napi_value *result);
typedef napi_status (*napi_create_reference_fn)(napi_env env, napi_value value,
                                                unsigned int refcount, void **result);
typedef napi_status (*napi_delete_reference_fn)(napi_env env, void *ref);
typedef napi_status (*napi_get_reference_value_fn)(napi_env env, void *ref, napi_value *result);

// ----------------------------------------------------------------------------
// Engine state
// ----------------------------------------------------------------------------

static js_lib_t js_lib_ = NULL;
static napi_env js_env_ = NULL;
static int js_version_hint_ = 0;

static napi_create_string_utf8_fn napi_create_string_utf8_ = NULL;
static napi_create_double_fn napi_create_double_ = NULL;
static napi_get_value_double_fn napi_get_value_double_ = NULL;
static napi_get_value_string_utf8_fn napi_get_value_string_utf8_ = NULL;
static napi_get_global_fn napi_get_global_ = NULL;
static napi_get_named_property_fn napi_get_named_property_ = NULL;
static napi_call_function_fn napi_call_function_ = NULL;
static napi_create_reference_fn napi_create_reference_ = NULL;
static napi_delete_reference_fn napi_delete_reference_ = NULL;
static napi_get_reference_value_fn napi_get_reference_value_ = NULL;

// ----------------------------------------------------------------------------
// Value descriptor — a tagged union so the standalone fallback can still
// shuttle scalars / strings around even when no engine is loaded.
// ----------------------------------------------------------------------------

typedef enum {
  POLYGLOT_JS_VAL_NAPI_REF = 0,
  POLYGLOT_JS_VAL_STRING,
  POLYGLOT_JS_VAL_NUMBER
} polyglot_js_val_kind_t;

typedef struct {
  polyglot_js_val_kind_t kind;
  union {
    void *napi_ref;  // strong reference into the JS engine
    char *str;       // owned string (raw heap)
    double num;
  } as;
} polyglot_js_value_t;

static polyglot_js_value_t *make_string_value(const char *utf8) {
  polyglot_js_value_t *v =
      (polyglot_js_value_t *)polyglot_raw_calloc(1, sizeof(*v));
  if (!v) return NULL;
  v->kind = POLYGLOT_JS_VAL_STRING;
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

static polyglot_js_value_t *make_number_value(double n) {
  polyglot_js_value_t *v =
      (polyglot_js_value_t *)polyglot_raw_calloc(1, sizeof(*v));
  if (!v) return NULL;
  v->kind = POLYGLOT_JS_VAL_NUMBER;
  v->as.num = n;
  return v;
}

// ----------------------------------------------------------------------------
// Initialisation / teardown
// ----------------------------------------------------------------------------

static int load_node_library(void) {
  // Honour explicit override first; this is what test fixtures use.
  const char *override_path = getenv("POLYGLOT_JS_LIBRARY");
  if (override_path && override_path[0]) {
    js_lib_ = JS_LOAD(override_path);
    if (js_lib_) return 0;
  }

  // Probe NODE_HOME / well-known names per platform.
  const char *node_home = getenv("NODE_HOME");
  char path[1024] = {0};

#ifdef _WIN32
  if (node_home && node_home[0]) {
    snprintf(path, sizeof(path), "%s\\node.dll", node_home);
    js_lib_ = JS_LOAD(path);
  }
  if (!js_lib_) js_lib_ = JS_LOAD("node.dll");
  if (!js_lib_) js_lib_ = JS_LOAD("libnode.dll");
#elif defined(__APPLE__)
  if (node_home && node_home[0]) {
    snprintf(path, sizeof(path), "%s/lib/libnode.dylib", node_home);
    js_lib_ = JS_LOAD(path);
  }
  if (!js_lib_) js_lib_ = JS_LOAD("libnode.dylib");
#else
  if (node_home && node_home[0]) {
    snprintf(path, sizeof(path), "%s/lib/libnode.so", node_home);
    js_lib_ = JS_LOAD(path);
  }
  if (!js_lib_) js_lib_ = JS_LOAD("libnode.so");
#endif

  return js_lib_ ? 0 : -1;
}

int polyglot_js_init(int version_hint) {
  js_version_hint_ = version_hint;

  if (load_node_library() != 0) {
    fprintf(stderr,
            "[polyglot-js] warning: no Node.js / libnode shared library "
            "found. JavaScript helpers fall back to standalone mode.\n");
    return -1;
  }

  napi_create_string_utf8_ =
      (napi_create_string_utf8_fn)JS_SYM(js_lib_, "napi_create_string_utf8");
  napi_create_double_ =
      (napi_create_double_fn)JS_SYM(js_lib_, "napi_create_double");
  napi_get_value_double_ =
      (napi_get_value_double_fn)JS_SYM(js_lib_, "napi_get_value_double");
  napi_get_value_string_utf8_ =
      (napi_get_value_string_utf8_fn)JS_SYM(js_lib_, "napi_get_value_string_utf8");
  napi_get_global_ = (napi_get_global_fn)JS_SYM(js_lib_, "napi_get_global");
  napi_get_named_property_ =
      (napi_get_named_property_fn)JS_SYM(js_lib_, "napi_get_named_property");
  napi_call_function_ = (napi_call_function_fn)JS_SYM(js_lib_, "napi_call_function");
  napi_create_reference_ =
      (napi_create_reference_fn)JS_SYM(js_lib_, "napi_create_reference");
  napi_delete_reference_ =
      (napi_delete_reference_fn)JS_SYM(js_lib_, "napi_delete_reference");
  napi_get_reference_value_ =
      (napi_get_reference_value_fn)JS_SYM(js_lib_, "napi_get_reference_value");

  // The Node embedder API (`node::Start` / `node::CommonEnvironmentSetup`) is
  // C++ and changes signature across versions, so we can't probe it portably
  // through dlsym.  An external launcher (the `polyrt --js-host` mode) is
  // expected to call `polyglot_js_register_env` once it has bootstrapped Node.
  // Until then, eval/call paths fall back to the standalone behaviour.
  return 0;
}

// Allow an external embedder (e.g. polyrt's optional Node host) to inject the
// active napi_env so the eval/call helpers below can talk to a real engine.
void polyglot_js_register_env(void *env) { js_env_ = (napi_env)env; }

void polyglot_js_shutdown(void) {
  if (js_lib_) {
    JS_UNLOAD(js_lib_);
    js_lib_ = NULL;
  }
  js_env_ = NULL;
  napi_create_string_utf8_ = NULL;
  napi_create_double_ = NULL;
  napi_get_value_double_ = NULL;
  napi_get_value_string_utf8_ = NULL;
  napi_get_global_ = NULL;
  napi_get_named_property_ = NULL;
  napi_call_function_ = NULL;
  napi_create_reference_ = NULL;
  napi_delete_reference_ = NULL;
  napi_get_reference_value_ = NULL;
  js_version_hint_ = 0;
}

// ----------------------------------------------------------------------------
// Always-available helpers
// ----------------------------------------------------------------------------

void polyglot_js_print(const char *message) {
  if (!message) return;
  printf("%s\n", message);
}

char *polyglot_js_strdup_gc(const char *message, void ***root_handle_out) {
  if (!message) return NULL;
  size_t len = strlen(message) + 1;
  char *buf = (char *)polyglot_alloc(len);
  if (!buf) return NULL;
  memcpy(buf, message, len);
  polyglot_gc_register_root((void **)&buf);
  if (root_handle_out) *root_handle_out = (void **)&buf;
  return buf;
}

void polyglot_js_release(char **ptr, void ***root_handle) {
  if (!ptr || !*ptr) return;
  polyglot_gc_unregister_root((void **)ptr);
  *ptr = NULL;
  if (root_handle) *root_handle = NULL;
}

// ----------------------------------------------------------------------------
// Engine-backed value operations (return NULL when the engine is not active)
// ----------------------------------------------------------------------------

static polyglot_js_value_t *wrap_napi_value(napi_value raw) {
  if (!js_env_ || !napi_create_reference_) return NULL;
  void *ref = NULL;
  if (napi_create_reference_(js_env_, raw, 1, &ref) != 0 || !ref) return NULL;
  polyglot_js_value_t *v =
      (polyglot_js_value_t *)polyglot_raw_calloc(1, sizeof(*v));
  if (!v) {
    napi_delete_reference_(js_env_, ref);
    return NULL;
  }
  v->kind = POLYGLOT_JS_VAL_NAPI_REF;
  v->as.napi_ref = ref;
  return v;
}

void *polyglot_js_eval(const char *source) {
  if (!source || !js_env_) return NULL;
  // We invoke the global `eval` to keep the surface tiny.  Engine-side code
  // injection is the embedder's responsibility (see polyrt's --js-host mode).
  napi_value global = NULL;
  if (napi_get_global_(js_env_, &global) != 0 || !global) return NULL;
  napi_value eval_fn = NULL;
  if (napi_get_named_property_(js_env_, global, "eval", &eval_fn) != 0 || !eval_fn) {
    return NULL;
  }
  napi_value src_val = NULL;
  if (napi_create_string_utf8_(js_env_, source, strlen(source), &src_val) != 0) {
    return NULL;
  }
  napi_value result = NULL;
  if (napi_call_function_(js_env_, global, eval_fn, 1, &src_val, &result) != 0) {
    return NULL;
  }
  return wrap_napi_value(result);
}

void *polyglot_js_get_global(const char *name) {
  if (!name || !js_env_) return NULL;
  napi_value global = NULL;
  if (napi_get_global_(js_env_, &global) != 0) return NULL;
  napi_value v = NULL;
  if (napi_get_named_property_(js_env_, global, name, &v) != 0 || !v) return NULL;
  return wrap_napi_value(v);
}

void *polyglot_js_get_property(void *object, const char *name) {
  if (!object || !name || !js_env_) return NULL;
  polyglot_js_value_t *obj = (polyglot_js_value_t *)object;
  if (obj->kind != POLYGLOT_JS_VAL_NAPI_REF) return NULL;
  napi_value raw = NULL;
  if (napi_get_reference_value_(js_env_, obj->as.napi_ref, &raw) != 0) return NULL;
  napi_value v = NULL;
  if (napi_get_named_property_(js_env_, raw, name, &v) != 0 || !v) return NULL;
  return wrap_napi_value(v);
}

void *polyglot_js_call_function(void *function, void *this_arg,
                                const void *const *args, int arg_count) {
  if (!function || !js_env_) return NULL;
  polyglot_js_value_t *fn_val = (polyglot_js_value_t *)function;
  if (fn_val->kind != POLYGLOT_JS_VAL_NAPI_REF) return NULL;

  napi_value fn_raw = NULL;
  if (napi_get_reference_value_(js_env_, fn_val->as.napi_ref, &fn_raw) != 0) return NULL;

  napi_value this_raw = NULL;
  if (this_arg) {
    polyglot_js_value_t *t = (polyglot_js_value_t *)this_arg;
    if (t->kind == POLYGLOT_JS_VAL_NAPI_REF) {
      napi_get_reference_value_(js_env_, t->as.napi_ref, &this_raw);
    }
  }
  if (!this_raw) napi_get_global_(js_env_, &this_raw);

  enum { kMaxArgs = 32 };
  napi_value argv[kMaxArgs];
  int n = arg_count > kMaxArgs ? kMaxArgs : arg_count;
  for (int i = 0; i < n; ++i) {
    polyglot_js_value_t *a = (polyglot_js_value_t *)args[i];
    napi_value raw = NULL;
    if (a) {
      switch (a->kind) {
        case POLYGLOT_JS_VAL_NAPI_REF:
          napi_get_reference_value_(js_env_, a->as.napi_ref, &raw);
          break;
        case POLYGLOT_JS_VAL_STRING:
          napi_create_string_utf8_(js_env_, a->as.str, strlen(a->as.str), &raw);
          break;
        case POLYGLOT_JS_VAL_NUMBER:
          napi_create_double_(js_env_, a->as.num, &raw);
          break;
      }
    }
    argv[i] = raw;
  }
  napi_value result = NULL;
  if (napi_call_function_(js_env_, this_raw, fn_raw, (size_t)n, argv, &result) != 0) {
    return NULL;
  }
  return wrap_napi_value(result);
}

char *polyglot_js_value_to_string(void *value, void ***root_handle_out) {
  if (!value) return NULL;
  polyglot_js_value_t *v = (polyglot_js_value_t *)value;

  if (v->kind == POLYGLOT_JS_VAL_STRING) {
    return polyglot_js_strdup_gc(v->as.str, root_handle_out);
  }
  if (v->kind == POLYGLOT_JS_VAL_NUMBER) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", v->as.num);
    return polyglot_js_strdup_gc(buf, root_handle_out);
  }
  if (!js_env_ || v->kind != POLYGLOT_JS_VAL_NAPI_REF) return NULL;
  napi_value raw = NULL;
  if (napi_get_reference_value_(js_env_, v->as.napi_ref, &raw) != 0) return NULL;
  size_t needed = 0;
  if (napi_get_value_string_utf8_(js_env_, raw, NULL, 0, &needed) != 0) return NULL;
  char *tmp = (char *)polyglot_raw_malloc(needed + 1);
  if (!tmp) return NULL;
  size_t written = 0;
  if (napi_get_value_string_utf8_(js_env_, raw, tmp, needed + 1, &written) != 0) {
    polyglot_raw_free(tmp);
    return NULL;
  }
  tmp[written] = '\0';
  char *gc = polyglot_js_strdup_gc(tmp, root_handle_out);
  polyglot_raw_free(tmp);
  return gc;
}

double polyglot_js_value_to_number(void *value) {
  if (!value) return 0.0;
  polyglot_js_value_t *v = (polyglot_js_value_t *)value;
  if (v->kind == POLYGLOT_JS_VAL_NUMBER) return v->as.num;
  if (v->kind == POLYGLOT_JS_VAL_STRING) return atof(v->as.str);
  if (!js_env_ || v->kind != POLYGLOT_JS_VAL_NAPI_REF) return 0.0;
  napi_value raw = NULL;
  if (napi_get_reference_value_(js_env_, v->as.napi_ref, &raw) != 0) return 0.0;
  double out = 0.0;
  napi_get_value_double_(js_env_, raw, &out);
  return out;
}

void *polyglot_js_string_value(const char *utf8) { return make_string_value(utf8); }
void *polyglot_js_number_value(double n) { return make_number_value(n); }

void polyglot_js_release_value(void *value) {
  if (!value) return;
  polyglot_js_value_t *v = (polyglot_js_value_t *)value;
  switch (v->kind) {
    case POLYGLOT_JS_VAL_NAPI_REF:
      if (js_env_ && v->as.napi_ref && napi_delete_reference_) {
        napi_delete_reference_(js_env_, v->as.napi_ref);
      }
      break;
    case POLYGLOT_JS_VAL_STRING:
      if (v->as.str) polyglot_raw_free(v->as.str);
      break;
    case POLYGLOT_JS_VAL_NUMBER:
      break;
  }
  polyglot_raw_free(v);
}
