/**
 * @file     python_rt.c
 * @brief    Implementation of the Python (CPython) runtime support library.
 *
 * @details  Mirrors java_rt / dotnet_rt / javascript_rt by dynamically
 *           loading libpython3.x at run time (no build-time dependency on
 *           the Python development headers).  The bridge resolves a small
 *           subset of the stable CPython C-API and exposes a tagged-union
 *           value descriptor backed by the runtime's mimalloc raw heap.
 *           Each descriptor that wraps a `PyObject*` owns one strong
 *           reference (incremented at wrap time, decremented on release).
 *
 *           When no libpython can be located the print and strdup helpers
 *           continue to work via libc; every other entry point degrades to
 *           NULL / -1 so simple lowered programs still execute.
 *
 * @ingroup  Runtime / Libs
 * @author   Manning Cyrus
 * @date     2026-04-26
 */

#include "runtime/include/libs/python_rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/include/libs/base.h"
#include "runtime/include/memory/polyglot_alloc.h"

#ifdef _WIN32
#include <windows.h>
typedef HMODULE py_lib_t;
#define PY_LOAD(path)     LoadLibraryA(path)
#define PY_SYM(lib, name) ((void *)GetProcAddress((lib), (name)))
#define PY_UNLOAD(lib)    FreeLibrary(lib)
#else
#include <dlfcn.h>
typedef void *py_lib_t;
#define PY_LOAD(path)     dlopen((path), RTLD_LAZY | RTLD_GLOBAL)
#define PY_SYM(lib, name) dlsym((lib), (name))
#define PY_UNLOAD(lib)    dlclose(lib)
#endif

// ----------------------------------------------------------------------------
// Stable CPython C-API surface (function pointer typedefs).  Only the
// symbols we actually call are declared so the runtime needs no Python
// headers at build time.  PyObject is opaque on our side.
// ----------------------------------------------------------------------------

typedef struct PyObject PyObject;

typedef void (*Py_Initialize_fn)(void);
typedef void (*Py_InitializeEx_fn)(int);
typedef int (*Py_IsInitialized_fn)(void);
typedef void (*Py_Finalize_fn)(void);
typedef int (*PyRun_SimpleStringFlags_fn)(const char *command, void *flags);
typedef PyObject *(*PyImport_ImportModule_fn)(const char *name);
typedef PyObject *(*PyObject_GetAttrString_fn)(PyObject *o, const char *attr);
typedef int (*PyObject_SetAttrString_fn)(PyObject *o, const char *attr, PyObject *v);
typedef PyObject *(*PyObject_CallObject_fn)(PyObject *callable, PyObject *args);
typedef PyObject *(*PyTuple_New_fn)(long long size);
typedef int (*PyTuple_SetItem_fn)(PyObject *tuple, long long pos, PyObject *o);
typedef PyObject *(*PyUnicode_FromString_fn)(const char *u);
typedef const char *(*PyUnicode_AsUTF8_fn)(PyObject *o);
typedef PyObject *(*PyLong_FromLongLong_fn)(long long v);
typedef long long (*PyLong_AsLongLong_fn)(PyObject *o);
typedef PyObject *(*PyFloat_FromDouble_fn)(double v);
typedef double (*PyFloat_AsDouble_fn)(PyObject *o);
typedef PyObject *(*PyObject_Str_fn)(PyObject *o);
typedef void (*Py_IncRef_fn)(PyObject *o);
typedef void (*Py_DecRef_fn)(PyObject *o);
typedef PyObject *(*PyErr_Occurred_fn)(void);
typedef void (*PyErr_Clear_fn)(void);
typedef void (*PyErr_Print_fn)(void);

// ----------------------------------------------------------------------------
// Engine state
// ----------------------------------------------------------------------------

static py_lib_t py_lib_ = NULL;
static int py_version_hint_ = 0;
static int py_owns_init_ = 0;  // did we call Py_Initialize ourselves?

static Py_Initialize_fn Py_Initialize_ = NULL;
static Py_InitializeEx_fn Py_InitializeEx_ = NULL;
static Py_IsInitialized_fn Py_IsInitialized_ = NULL;
static Py_Finalize_fn Py_Finalize_ = NULL;
static PyRun_SimpleStringFlags_fn PyRun_SimpleStringFlags_ = NULL;
static PyImport_ImportModule_fn PyImport_ImportModule_ = NULL;
static PyObject_GetAttrString_fn PyObject_GetAttrString_ = NULL;
static PyObject_SetAttrString_fn PyObject_SetAttrString_ = NULL;
static PyObject_CallObject_fn PyObject_CallObject_ = NULL;
static PyTuple_New_fn PyTuple_New_ = NULL;
static PyTuple_SetItem_fn PyTuple_SetItem_ = NULL;
static PyUnicode_FromString_fn PyUnicode_FromString_ = NULL;
static PyUnicode_AsUTF8_fn PyUnicode_AsUTF8_ = NULL;
static PyLong_FromLongLong_fn PyLong_FromLongLong_ = NULL;
static PyLong_AsLongLong_fn PyLong_AsLongLong_ = NULL;
static PyFloat_FromDouble_fn PyFloat_FromDouble_ = NULL;
static PyFloat_AsDouble_fn PyFloat_AsDouble_ = NULL;
static PyObject_Str_fn PyObject_Str_ = NULL;
static Py_IncRef_fn Py_IncRef_ = NULL;
static Py_DecRef_fn Py_DecRef_ = NULL;
static PyErr_Occurred_fn PyErr_Occurred_ = NULL;
static PyErr_Clear_fn PyErr_Clear_ = NULL;
static PyErr_Print_fn PyErr_Print_ = NULL;

static int py_alive(void) {
  return py_lib_ && Py_IsInitialized_ && Py_IsInitialized_() != 0;
}

// ----------------------------------------------------------------------------
// Value descriptor
// ----------------------------------------------------------------------------

typedef enum {
  POLYGLOT_PY_VAL_PYOBJ = 0,  // strong reference to a PyObject
  POLYGLOT_PY_VAL_STRING,
  POLYGLOT_PY_VAL_INT,
  POLYGLOT_PY_VAL_FLOAT,
  POLYGLOT_PY_VAL_NONE,
} polyglot_py_val_kind_t;

typedef struct {
  polyglot_py_val_kind_t kind;
  union {
    PyObject *obj;  // strong ref (Py_IncRef on wrap, Py_DecRef on release)
    char *str;
    long long i64;
    double f64;
  } as;
} polyglot_py_value_t;

static polyglot_py_value_t *new_descriptor(polyglot_py_val_kind_t kind) {
  polyglot_py_value_t *v =
      (polyglot_py_value_t *)polyglot_raw_calloc(1, sizeof(*v));
  if (v) v->kind = kind;
  return v;
}

static polyglot_py_value_t *wrap_pyobject(PyObject *obj) {
  if (!obj) return NULL;
  polyglot_py_value_t *v = new_descriptor(POLYGLOT_PY_VAL_PYOBJ);
  if (!v) return NULL;
  v->as.obj = obj;
  if (Py_IncRef_) Py_IncRef_(obj);
  return v;
}

// Convert any descriptor into a fresh PyObject* (caller owns one ref).
// Returns NULL when the engine isn't alive or kind unsupported.
static PyObject *descriptor_to_pyobject(const polyglot_py_value_t *v) {
  if (!v || !py_alive()) return NULL;
  switch (v->kind) {
    case POLYGLOT_PY_VAL_PYOBJ:
      if (v->as.obj && Py_IncRef_) Py_IncRef_(v->as.obj);
      return v->as.obj;
    case POLYGLOT_PY_VAL_STRING:
      return PyUnicode_FromString_ ? PyUnicode_FromString_(v->as.str) : NULL;
    case POLYGLOT_PY_VAL_INT:
      return PyLong_FromLongLong_ ? PyLong_FromLongLong_(v->as.i64) : NULL;
    case POLYGLOT_PY_VAL_FLOAT:
      return PyFloat_FromDouble_ ? PyFloat_FromDouble_(v->as.f64) : NULL;
    case POLYGLOT_PY_VAL_NONE:
      return NULL;
  }
  return NULL;
}

// ----------------------------------------------------------------------------
// Library probing
// ----------------------------------------------------------------------------

static int try_load(const char *path) {
  if (!path || !path[0]) return 0;
  py_lib_ = PY_LOAD(path);
  return py_lib_ ? 1 : 0;
}

static int load_libpython(int hint) {
  // 1. explicit override
  const char *override_path = getenv("POLYGLOT_PYTHON_LIBRARY");
  if (override_path && try_load(override_path)) return 0;

  // 2. PYTHONHOME / PYTHON_HOME
  const char *home = getenv("PYTHONHOME");
  if (!home) home = getenv("PYTHON_HOME");
  char path[1024];

  // Versions to try, ordered by `hint` then descending.
  const int candidates[] = {hint, 313, 312, 311, 310, 39, 38, 0};
  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
    int v = candidates[i];
    int major = v == 0 ? 3 : v / 10 == 3 ? 3 : v / 100;
    int minor = v == 0 ? 0 : v % 100 < 10 ? v % 10 : v % 100;
    if (v == 0) {
      // generic name, no version
#ifdef _WIN32
      if (try_load("python3.dll")) return 0;
#elif defined(__APPLE__)
      if (try_load("libpython3.dylib")) return 0;
#else
      if (try_load("libpython3.so")) return 0;
#endif
      continue;
    }
#ifdef _WIN32
    snprintf(path, sizeof(path), "python%d%d.dll", major, minor);
    if (home && home[0]) {
      char with_home[1024];
      snprintf(with_home, sizeof(with_home), "%s\\%s", home, path);
      if (try_load(with_home)) return 0;
    }
    if (try_load(path)) return 0;
#elif defined(__APPLE__)
    snprintf(path, sizeof(path), "libpython%d.%d.dylib", major, minor);
    if (home && home[0]) {
      char with_home[1024];
      snprintf(with_home, sizeof(with_home), "%s/lib/%s", home, path);
      if (try_load(with_home)) return 0;
    }
    if (try_load(path)) return 0;
    snprintf(path, sizeof(path), "libpython%d.%dm.dylib", major, minor);
    if (try_load(path)) return 0;
#else
    snprintf(path, sizeof(path), "libpython%d.%d.so.1.0", major, minor);
    if (home && home[0]) {
      char with_home[1024];
      snprintf(with_home, sizeof(with_home), "%s/lib/%s", home, path);
      if (try_load(with_home)) return 0;
    }
    if (try_load(path)) return 0;
    snprintf(path, sizeof(path), "libpython%d.%d.so", major, minor);
    if (try_load(path)) return 0;
    snprintf(path, sizeof(path), "libpython%d.%dm.so.1.0", major, minor);
    if (try_load(path)) return 0;
#endif
  }
  return -1;
}

#define PY_RESOLVE(name) name##_ = (name##_fn)PY_SYM(py_lib_, #name)

static void resolve_symbols(void) {
  PY_RESOLVE(Py_Initialize);
  PY_RESOLVE(Py_InitializeEx);
  PY_RESOLVE(Py_IsInitialized);
  PY_RESOLVE(Py_Finalize);
  PY_RESOLVE(PyRun_SimpleStringFlags);
  PY_RESOLVE(PyImport_ImportModule);
  PY_RESOLVE(PyObject_GetAttrString);
  PY_RESOLVE(PyObject_SetAttrString);
  PY_RESOLVE(PyObject_CallObject);
  PY_RESOLVE(PyTuple_New);
  PY_RESOLVE(PyTuple_SetItem);
  PY_RESOLVE(PyUnicode_FromString);
  PY_RESOLVE(PyUnicode_AsUTF8);
  PY_RESOLVE(PyLong_FromLongLong);
  PY_RESOLVE(PyLong_AsLongLong);
  PY_RESOLVE(PyFloat_FromDouble);
  PY_RESOLVE(PyFloat_AsDouble);
  PY_RESOLVE(PyObject_Str);
  PY_RESOLVE(Py_IncRef);
  PY_RESOLVE(Py_DecRef);
  PY_RESOLVE(PyErr_Occurred);
  PY_RESOLVE(PyErr_Clear);
  PY_RESOLVE(PyErr_Print);
}

#undef PY_RESOLVE

// ----------------------------------------------------------------------------
// Lifecycle
// ----------------------------------------------------------------------------

int polyglot_python_init(int version_hint) {
  py_version_hint_ = version_hint;
  if (py_lib_) return 0;  // already loaded

  if (load_libpython(version_hint) != 0) {
    fprintf(stderr,
            "[polyglot-python] warning: no libpython3.x shared library "
            "found. Python helpers fall back to standalone mode.\n");
    return -1;
  }
  resolve_symbols();

  if (!Py_IsInitialized_ || !Py_Initialize_) {
    fprintf(stderr,
            "[polyglot-python] error: required CPython symbols missing in "
            "%p; aborting init.\n",
            (void *)py_lib_);
    PY_UNLOAD(py_lib_);
    py_lib_ = NULL;
    return -1;
  }

  if (Py_IsInitialized_() == 0) {
    if (Py_InitializeEx_) {
      Py_InitializeEx_(0);  // 0 = don't install signal handlers
    } else {
      Py_Initialize_();
    }
    py_owns_init_ = 1;
  }
  return 0;
}

void polyglot_python_shutdown(void) {
  if (py_owns_init_ && Py_Finalize_) {
    Py_Finalize_();
  }
  if (py_lib_) {
    PY_UNLOAD(py_lib_);
    py_lib_ = NULL;
  }
  py_owns_init_ = 0;
  py_version_hint_ = 0;
  Py_Initialize_ = NULL;
  Py_InitializeEx_ = NULL;
  Py_IsInitialized_ = NULL;
  Py_Finalize_ = NULL;
  PyRun_SimpleStringFlags_ = NULL;
  PyImport_ImportModule_ = NULL;
  PyObject_GetAttrString_ = NULL;
  PyObject_SetAttrString_ = NULL;
  PyObject_CallObject_ = NULL;
  PyTuple_New_ = NULL;
  PyTuple_SetItem_ = NULL;
  PyUnicode_FromString_ = NULL;
  PyUnicode_AsUTF8_ = NULL;
  PyLong_FromLongLong_ = NULL;
  PyLong_AsLongLong_ = NULL;
  PyFloat_FromDouble_ = NULL;
  PyFloat_AsDouble_ = NULL;
  PyObject_Str_ = NULL;
  Py_IncRef_ = NULL;
  Py_DecRef_ = NULL;
  PyErr_Occurred_ = NULL;
  PyErr_Clear_ = NULL;
  PyErr_Print_ = NULL;
}

// ----------------------------------------------------------------------------
// Always-available helpers
// ----------------------------------------------------------------------------

void polyglot_python_print(const char *message) {
  if (!message) return;
  printf("%s\n", message);
}

char *polyglot_python_strdup_gc(const char *message, void ***root_handle_out) {
  if (!message) return NULL;
  size_t len = strlen(message) + 1;
  char *buf = (char *)polyglot_alloc(len);
  if (!buf) return NULL;
  memcpy(buf, message, len);
  polyglot_gc_register_root((void **)&buf);
  if (root_handle_out) *root_handle_out = (void **)&buf;
  return buf;
}

void polyglot_python_release(char **ptr, void ***root_handle) {
  if (!ptr || !*ptr) return;
  polyglot_gc_unregister_root((void **)ptr);
  *ptr = NULL;
  if (root_handle) *root_handle = NULL;
}

// ----------------------------------------------------------------------------
// Interpreter operations
// ----------------------------------------------------------------------------

int polyglot_python_run_string(const char *source) {
  if (!source) return -1;
  if (!py_alive() || !PyRun_SimpleStringFlags_) return -1;
  int rc = PyRun_SimpleStringFlags_(source, NULL);
  if (rc != 0 && PyErr_Print_) PyErr_Print_();
  return rc;
}

void *polyglot_python_import(const char *module_name) {
  if (!module_name || !py_alive() || !PyImport_ImportModule_) return NULL;
  PyObject *mod = PyImport_ImportModule_(module_name);
  if (!mod) {
    if (PyErr_Print_) PyErr_Print_();
    return NULL;
  }
  polyglot_py_value_t *v = wrap_pyobject(mod);
  if (Py_DecRef_) Py_DecRef_(mod);  // wrap took its own ref
  return v;
}

void *polyglot_python_get_attr(void *object, const char *name) {
  if (!object || !name || !py_alive() || !PyObject_GetAttrString_) return NULL;
  polyglot_py_value_t *o = (polyglot_py_value_t *)object;
  if (o->kind != POLYGLOT_PY_VAL_PYOBJ || !o->as.obj) return NULL;
  PyObject *attr = PyObject_GetAttrString_(o->as.obj, name);
  if (!attr) {
    if (PyErr_Clear_) PyErr_Clear_();
    return NULL;
  }
  polyglot_py_value_t *v = wrap_pyobject(attr);
  if (Py_DecRef_) Py_DecRef_(attr);
  return v;
}

int polyglot_python_set_attr(void *object, const char *name, void *value) {
  if (!object || !name || !py_alive() || !PyObject_SetAttrString_) return -1;
  polyglot_py_value_t *o = (polyglot_py_value_t *)object;
  if (o->kind != POLYGLOT_PY_VAL_PYOBJ || !o->as.obj) return -1;
  PyObject *raw = descriptor_to_pyobject((polyglot_py_value_t *)value);
  if (!raw) return -1;
  int rc = PyObject_SetAttrString_(o->as.obj, name, raw);
  if (Py_DecRef_) Py_DecRef_(raw);
  if (rc != 0 && PyErr_Clear_) PyErr_Clear_();
  return rc == 0 ? 0 : -1;
}

void *polyglot_python_call(void *callable, const void *const *args, int arg_count) {
  if (!callable || !py_alive() || !PyObject_CallObject_) return NULL;
  polyglot_py_value_t *fn = (polyglot_py_value_t *)callable;
  if (fn->kind != POLYGLOT_PY_VAL_PYOBJ || !fn->as.obj) return NULL;

  PyObject *tuple = NULL;
  if (arg_count > 0 && PyTuple_New_ && PyTuple_SetItem_) {
    tuple = PyTuple_New_((long long)arg_count);
    if (!tuple) return NULL;
    for (int i = 0; i < arg_count; ++i) {
      PyObject *a = descriptor_to_pyobject((const polyglot_py_value_t *)args[i]);
      // PyTuple_SetItem steals the reference, which matches what we want.
      if (!a || PyTuple_SetItem_(tuple, (long long)i, a) != 0) {
        if (Py_DecRef_) Py_DecRef_(tuple);
        if (PyErr_Clear_) PyErr_Clear_();
        return NULL;
      }
    }
  }

  PyObject *result = PyObject_CallObject_(fn->as.obj, tuple);
  if (tuple && Py_DecRef_) Py_DecRef_(tuple);
  if (!result) {
    if (PyErr_Print_) PyErr_Print_();
    return NULL;
  }
  polyglot_py_value_t *out = wrap_pyobject(result);
  if (Py_DecRef_) Py_DecRef_(result);
  return out;
}

void *polyglot_python_call_method(void *receiver, const char *method_name,
                                  const void *const *args, int arg_count) {
  if (!receiver || !method_name) return NULL;
  void *callable = polyglot_python_get_attr(receiver, method_name);
  if (!callable) return NULL;
  void *out = polyglot_python_call(callable, args, arg_count);
  polyglot_python_release_value(callable);
  return out;
}

char *polyglot_python_value_to_string(void *value, void ***root_handle_out) {
  if (!value) return NULL;
  polyglot_py_value_t *v = (polyglot_py_value_t *)value;
  switch (v->kind) {
    case POLYGLOT_PY_VAL_STRING:
      return polyglot_python_strdup_gc(v->as.str, root_handle_out);
    case POLYGLOT_PY_VAL_INT: {
      char buf[32];
      snprintf(buf, sizeof(buf), "%lld", v->as.i64);
      return polyglot_python_strdup_gc(buf, root_handle_out);
    }
    case POLYGLOT_PY_VAL_FLOAT: {
      char buf[64];
      snprintf(buf, sizeof(buf), "%g", v->as.f64);
      return polyglot_python_strdup_gc(buf, root_handle_out);
    }
    case POLYGLOT_PY_VAL_NONE:
      return polyglot_python_strdup_gc("None", root_handle_out);
    case POLYGLOT_PY_VAL_PYOBJ:
      break;
  }
  if (!py_alive() || !PyObject_Str_ || !PyUnicode_AsUTF8_) return NULL;
  PyObject *s = PyObject_Str_(v->as.obj);
  if (!s) {
    if (PyErr_Clear_) PyErr_Clear_();
    return NULL;
  }
  const char *utf8 = PyUnicode_AsUTF8_(s);
  char *gc = utf8 ? polyglot_python_strdup_gc(utf8, root_handle_out) : NULL;
  if (Py_DecRef_) Py_DecRef_(s);
  return gc;
}

long long polyglot_python_value_to_int(void *value) {
  if (!value) return 0;
  polyglot_py_value_t *v = (polyglot_py_value_t *)value;
  switch (v->kind) {
    case POLYGLOT_PY_VAL_INT: return v->as.i64;
    case POLYGLOT_PY_VAL_FLOAT: return (long long)v->as.f64;
    case POLYGLOT_PY_VAL_STRING: return v->as.str ? atoll(v->as.str) : 0;
    case POLYGLOT_PY_VAL_NONE: return 0;
    case POLYGLOT_PY_VAL_PYOBJ:
      if (py_alive() && PyLong_AsLongLong_) {
        long long out = PyLong_AsLongLong_(v->as.obj);
        if (PyErr_Occurred_ && PyErr_Occurred_()) {
          if (PyErr_Clear_) PyErr_Clear_();
          return 0;
        }
        return out;
      }
      return 0;
  }
  return 0;
}

double polyglot_python_value_to_float(void *value) {
  if (!value) return 0.0;
  polyglot_py_value_t *v = (polyglot_py_value_t *)value;
  switch (v->kind) {
    case POLYGLOT_PY_VAL_FLOAT: return v->as.f64;
    case POLYGLOT_PY_VAL_INT: return (double)v->as.i64;
    case POLYGLOT_PY_VAL_STRING: return v->as.str ? atof(v->as.str) : 0.0;
    case POLYGLOT_PY_VAL_NONE: return 0.0;
    case POLYGLOT_PY_VAL_PYOBJ:
      if (py_alive() && PyFloat_AsDouble_) {
        double out = PyFloat_AsDouble_(v->as.obj);
        if (PyErr_Occurred_ && PyErr_Occurred_()) {
          if (PyErr_Clear_) PyErr_Clear_();
          return 0.0;
        }
        return out;
      }
      return 0.0;
  }
  return 0.0;
}

void *polyglot_python_string_value(const char *utf8) {
  polyglot_py_value_t *v = new_descriptor(POLYGLOT_PY_VAL_STRING);
  if (!v) return NULL;
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

void *polyglot_python_integer_value(long long n) {
  polyglot_py_value_t *v = new_descriptor(POLYGLOT_PY_VAL_INT);
  if (v) v->as.i64 = n;
  return v;
}

void *polyglot_python_float_value(double n) {
  polyglot_py_value_t *v = new_descriptor(POLYGLOT_PY_VAL_FLOAT);
  if (v) v->as.f64 = n;
  return v;
}

void *polyglot_python_none_value(void) {
  return new_descriptor(POLYGLOT_PY_VAL_NONE);
}

void polyglot_python_release_value(void *value) {
  if (!value) return;
  polyglot_py_value_t *v = (polyglot_py_value_t *)value;
  switch (v->kind) {
    case POLYGLOT_PY_VAL_PYOBJ:
      if (v->as.obj && Py_DecRef_) Py_DecRef_(v->as.obj);
      break;
    case POLYGLOT_PY_VAL_STRING:
      if (v->as.str) polyglot_raw_free(v->as.str);
      break;
    case POLYGLOT_PY_VAL_INT:
    case POLYGLOT_PY_VAL_FLOAT:
    case POLYGLOT_PY_VAL_NONE:
      break;
  }
  polyglot_raw_free(v);
}
