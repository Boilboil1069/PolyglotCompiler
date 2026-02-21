#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/include/libs/base.h"
#include "runtime/include/libs/dotnet_rt.h"

// ============================================================================
// .NET Runtime Bridge
//
// These functions provide a C-level interface between the PolyglotCompiler
// runtime and the .NET CoreCLR runtime.  CoreCLR is loaded dynamically via
// LoadLibrary / dlopen so no hard dependency on a specific .NET SDK is
// required at compile time.
//
// Supported .NET versions: 6 (LTS), 7, 8 (LTS), 9.
// ============================================================================

#ifdef _WIN32
#include <windows.h>
typedef HMODULE clr_lib_t;
#define CLR_LOAD(path)       LoadLibraryA(path)
#define CLR_SYM(lib, name)   ((void *)GetProcAddress(lib, name))
#define CLR_UNLOAD(lib)      FreeLibrary(lib)
#else
#include <dlfcn.h>
typedef void *clr_lib_t;
#define CLR_LOAD(path)       dlopen(path, RTLD_LAZY)
#define CLR_SYM(lib, name)   dlsym(lib, name)
#define CLR_UNLOAD(lib)      dlclose(lib)
#endif

// CoreCLR hosting API function pointer types.
typedef int (*coreclr_initialize_fn)(const char *exePath,
                                     const char *appDomainFriendlyName,
                                     int propertyCount,
                                     const char **propertyKeys,
                                     const char **propertyValues,
                                     void **hostHandle,
                                     unsigned int *domainId);
typedef int (*coreclr_shutdown_fn)(void *hostHandle, unsigned int domainId);
typedef int (*coreclr_create_delegate_fn)(void *hostHandle,
                                          unsigned int domainId,
                                          const char *assemblyName,
                                          const char *typeName,
                                          const char *methodName,
                                          void **delegate);

static clr_lib_t      clr_lib_      = NULL;
static void          *clr_host_     = NULL;
static unsigned int   clr_domain_   = 0;

static coreclr_initialize_fn      clr_init_fn_       = NULL;
static coreclr_shutdown_fn        clr_shutdown_fn_    = NULL;
static coreclr_create_delegate_fn clr_delegate_fn_   = NULL;

static int dotnet_version_hint_ = 0;

int polyglot_dotnet_init(int version_hint) {
    dotnet_version_hint_ = version_hint;

    // Attempt to locate coreclr on the system.
    const char *dotnet_root = getenv("DOTNET_ROOT");
    char clr_path[1024] = {0};

    if (dotnet_root && dotnet_root[0]) {
#ifdef _WIN32
        snprintf(clr_path, sizeof(clr_path), "%s\\shared\\Microsoft.NETCore.App\\%d.0.0\\coreclr.dll",
                 dotnet_root, version_hint ? version_hint : 8);
#elif defined(__APPLE__)
        snprintf(clr_path, sizeof(clr_path), "%s/shared/Microsoft.NETCore.App/%d.0.0/libcoreclr.dylib",
                 dotnet_root, version_hint ? version_hint : 8);
#else
        snprintf(clr_path, sizeof(clr_path), "%s/shared/Microsoft.NETCore.App/%d.0.0/libcoreclr.so",
                 dotnet_root, version_hint ? version_hint : 8);
#endif
    }

    if (clr_path[0]) {
        clr_lib_ = CLR_LOAD(clr_path);
    }

    if (!clr_lib_) {
        // CoreCLR not found — runtime operations will gracefully return
        // NULL but the toolchain can still generate and validate code.
        return -1;
    }

    // Resolve hosting API entry points.
    clr_init_fn_     = (coreclr_initialize_fn)CLR_SYM(clr_lib_, "coreclr_initialize");
    clr_shutdown_fn_ = (coreclr_shutdown_fn)CLR_SYM(clr_lib_, "coreclr_shutdown");
    clr_delegate_fn_ = (coreclr_create_delegate_fn)CLR_SYM(clr_lib_, "coreclr_create_delegate");

    if (!clr_init_fn_ || !clr_shutdown_fn_ || !clr_delegate_fn_) {
        CLR_UNLOAD(clr_lib_);
        clr_lib_ = NULL;
        return -1;
    }

    // Initialize CoreCLR with minimal properties.
    const char *property_keys[]   = {"TRUSTED_PLATFORM_ASSEMBLIES"};
    const char *property_values[] = {""};

    int rc = clr_init_fn_("polyglot", "PolyglotCompiler", 1,
                           property_keys, property_values,
                           &clr_host_, &clr_domain_);
    if (rc != 0) {
        clr_host_ = NULL;
        CLR_UNLOAD(clr_lib_);
        clr_lib_ = NULL;
        return -1;
    }

    return 0;
}

void polyglot_dotnet_shutdown(void) {
    if (clr_host_ && clr_shutdown_fn_) {
        clr_shutdown_fn_(clr_host_, clr_domain_);
        clr_host_ = NULL;
        clr_domain_ = 0;
    }
    if (clr_lib_) {
        CLR_UNLOAD(clr_lib_);
        clr_lib_ = NULL;
    }
    clr_init_fn_ = NULL;
    clr_shutdown_fn_ = NULL;
    clr_delegate_fn_ = NULL;
    dotnet_version_hint_ = 0;
}

void polyglot_dotnet_print(const char *message) {
    if (!message) return;
    printf("%s\n", message);
}

char *polyglot_dotnet_strdup_gc(const char *message, void ***root_handle_out) {
    if (!message) return NULL;
    size_t len = strlen(message) + 1;
    char *buf = (char *)polyglot_alloc(len);
    if (!buf) return NULL;
    memcpy(buf, message, len);
    polyglot_gc_register_root((void **)&buf);
    if (root_handle_out) *root_handle_out = (void **)&buf;
    return buf;
}

void polyglot_dotnet_release(char **ptr, void ***root_handle) {
    if (!ptr || !*ptr) return;
    polyglot_gc_unregister_root((void **)ptr);
    *ptr = NULL;
    if (root_handle) *root_handle = NULL;
}

void *polyglot_dotnet_call_static(const char *assembly_name,
                                  const char *type_name,
                                  const char *method_name,
                                  const void *const *args,
                                  int arg_count) {
    if (!clr_host_ || !clr_delegate_fn_ || !assembly_name || !type_name || !method_name)
        return NULL;

    // Resolve the managed method as a function pointer via coreclr_create_delegate.
    // The method must have a compatible signature ([UnmanagedCallersOnly] or
    // a delegate with DllImport marshalling).
    typedef void *(*ManagedStaticMethod_fn)(const void *const *, int);
    ManagedStaticMethod_fn method = NULL;

    int rc = clr_delegate_fn_(clr_host_, clr_domain_,
                               assembly_name, type_name, method_name,
                               (void **)&method);
    if (rc != 0 || !method) return NULL;

    return method(args, arg_count);
}

void *polyglot_dotnet_new_object(const char *assembly_name,
                                 const char *type_name,
                                 const void *const *args,
                                 int arg_count) {
    if (!clr_host_ || !clr_delegate_fn_ || !assembly_name || !type_name)
        return NULL;

    // To instantiate a .NET object from native code we call a factory
    // method "__PolyglotFactory" on the type, which the .ploy code-gen
    // emits during the NEW lowering step.
    typedef void *(*FactoryMethod_fn)(const void *const *, int);
    FactoryMethod_fn factory = NULL;

    int rc = clr_delegate_fn_(clr_host_, clr_domain_,
                               assembly_name, type_name, "__PolyglotFactory",
                               (void **)&factory);
    if (rc != 0 || !factory) return NULL;

    return factory(args, arg_count);
}

void *polyglot_dotnet_call_method(void *object,
                                  const char *method_name,
                                  const void *const *args,
                                  int arg_count) {
    // Instance method calls on .NET objects require marshalling through
    // a generated wrapper.  The wrapper is resolved via a naming convention:
    //   PolyglotInterop.<TypeName>__<MethodName>
    // The object handle is prepended to the args array so the managed
    // side can recover the instance.
    if (!clr_host_ || !clr_delegate_fn_ || !object || !method_name)
        return NULL;

    // Build an extended args array with the object handle as the first element.
    const void **ext_args = (const void **)malloc((size_t)(arg_count + 1) * sizeof(void *));
    if (!ext_args) return NULL;
    ext_args[0] = object;
    for (int i = 0; i < arg_count; ++i) {
        ext_args[i + 1] = args[i];
    }

    typedef void *(*ManagedMethod_fn)(const void *const *, int);
    ManagedMethod_fn method = NULL;

    // We use "PolyglotInterop" as the assembly and type container.
    int rc = clr_delegate_fn_(clr_host_, clr_domain_,
                               "PolyglotInterop", "PolyglotInterop.Dispatch",
                               method_name, (void **)&method);

    void *result = NULL;
    if (rc == 0 && method) {
        result = method((const void *const *)ext_args, arg_count + 1);
    }

    free(ext_args);
    return result;
}

void *polyglot_dotnet_get_property(void *object,
                                   const char *property_name) {
    if (!clr_host_ || !clr_delegate_fn_ || !object || !property_name)
        return NULL;

    // Resolve a getter wrapper: PolyglotInterop.Dispatch.get_<PropertyName>
    char getter_name[256];
    snprintf(getter_name, sizeof(getter_name), "get_%s", property_name);

    typedef void *(*Getter_fn)(void *);
    Getter_fn getter = NULL;

    int rc = clr_delegate_fn_(clr_host_, clr_domain_,
                               "PolyglotInterop", "PolyglotInterop.Dispatch",
                               getter_name, (void **)&getter);
    if (rc != 0 || !getter) return NULL;

    return getter(object);
}

void polyglot_dotnet_set_property(void *object,
                                  const char *property_name,
                                  const void *value) {
    if (!clr_host_ || !clr_delegate_fn_ || !object || !property_name)
        return;

    char setter_name[256];
    snprintf(setter_name, sizeof(setter_name), "set_%s", property_name);

    typedef void (*Setter_fn)(void *, const void *);
    Setter_fn setter = NULL;

    int rc = clr_delegate_fn_(clr_host_, clr_domain_,
                               "PolyglotInterop", "PolyglotInterop.Dispatch",
                               setter_name, (void **)&setter);
    if (rc != 0 || !setter) return;

    setter(object, value);
}

void polyglot_dotnet_dispose(void *object) {
    if (!clr_host_ || !clr_delegate_fn_ || !object) return;

    // Resolve Dispose wrapper.
    typedef void (*Dispose_fn)(void *);
    Dispose_fn dispose = NULL;

    int rc = clr_delegate_fn_(clr_host_, clr_domain_,
                               "PolyglotInterop", "PolyglotInterop.Dispatch",
                               "Dispose", (void **)&dispose);
    if (rc == 0 && dispose) {
        dispose(object);
    }
}

void polyglot_dotnet_release_object(void *object) {
    if (!clr_host_ || !clr_delegate_fn_ || !object) return;

    // Release the GCHandle holding the .NET object alive.
    typedef void (*Release_fn)(void *);
    Release_fn release = NULL;

    int rc = clr_delegate_fn_(clr_host_, clr_domain_,
                               "PolyglotInterop", "PolyglotInterop.Dispatch",
                               "ReleaseHandle", (void **)&release);
    if (rc == 0 && release) {
        release(object);
    }
}
