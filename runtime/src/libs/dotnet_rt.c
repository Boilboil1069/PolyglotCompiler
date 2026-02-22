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
#include <dirent.h>
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
        // Try the exact version first, then probe common LTS versions.
        int versions_to_try[] = {
            version_hint ? version_hint : 8,
            9, 8, 7, 6, 0  // Sentinel
        };
        for (int i = 0; versions_to_try[i] != 0; ++i) {
            int ver = versions_to_try[i];
            // Probe multiple patch versions (e.g. 8.0.0, 8.0.1, ...)
            // We start with x.0.0 as the most common layout.
#ifdef _WIN32
            snprintf(clr_path, sizeof(clr_path),
                     "%s\\shared\\Microsoft.NETCore.App\\%d.0.0\\coreclr.dll",
                     dotnet_root, ver);
#elif defined(__APPLE__)
            snprintf(clr_path, sizeof(clr_path),
                     "%s/shared/Microsoft.NETCore.App/%d.0.0/libcoreclr.dylib",
                     dotnet_root, ver);
#else
            snprintf(clr_path, sizeof(clr_path),
                     "%s/shared/Microsoft.NETCore.App/%d.0.0/libcoreclr.so",
                     dotnet_root, ver);
#endif
            clr_lib_ = CLR_LOAD(clr_path);
            if (clr_lib_) break;
        }
    } else {
        fprintf(stderr, "[polyglot-dotnet] warning: DOTNET_ROOT is not set; "
                        "CoreCLR cannot be loaded.  Set DOTNET_ROOT to your .NET SDK installation.\n");
    }

    if (!clr_lib_) {
        // Try loading from the default system search path as a last resort.
#ifdef _WIN32
        clr_lib_ = CLR_LOAD("coreclr.dll");
#elif defined(__APPLE__)
        clr_lib_ = CLR_LOAD("libcoreclr.dylib");
#else
        clr_lib_ = CLR_LOAD("libcoreclr.so");
#endif
    }

    if (!clr_lib_) {
        fprintf(stderr, "[polyglot-dotnet] error: CoreCLR library not found at '%s'. "
                        "Ensure DOTNET_ROOT points to a valid .NET SDK installation "
                        "(e.g. C:\\Program Files\\dotnet).\n",
                clr_path[0] ? clr_path : "(default search path)");
        return -1;
    }

    // Resolve hosting API entry points.
    clr_init_fn_     = (coreclr_initialize_fn)CLR_SYM(clr_lib_, "coreclr_initialize");
    clr_shutdown_fn_ = (coreclr_shutdown_fn)CLR_SYM(clr_lib_, "coreclr_shutdown");
    clr_delegate_fn_ = (coreclr_create_delegate_fn)CLR_SYM(clr_lib_, "coreclr_create_delegate");

    if (!clr_init_fn_ || !clr_shutdown_fn_ || !clr_delegate_fn_) {
        fprintf(stderr, "[polyglot-dotnet] error: CoreCLR library loaded but "
                        "hosting API symbols not found.  The library may be corrupt "
                        "or an unsupported version.\n");
        CLR_UNLOAD(clr_lib_);
        clr_lib_ = NULL;
        return -1;
    }

    // Build TRUSTED_PLATFORM_ASSEMBLIES by scanning the runtime directory.
    // CoreCLR requires a semicolon(Win)/colon(Unix)-separated list of managed
    // assembly paths for the default load context.
    char tpa_list[32768] = {0};  // Large buffer for assembly paths
    size_t tpa_len = 0;

    // Extract the runtime directory from clr_path (the directory containing coreclr).
    char runtime_dir[1024] = {0};
    {
        // Copy clr_path and strip the filename to get the directory.
        strncpy(runtime_dir, clr_path, sizeof(runtime_dir) - 1);
        char *last_sep = NULL;
        for (char *p = runtime_dir; *p; ++p) {
            if (*p == '/' || *p == '\\') last_sep = p;
        }
        if (last_sep) *last_sep = '\0';
    }

    // Scan the runtime directory for .dll files to populate TPA.
    if (runtime_dir[0]) {
#ifdef _WIN32
        char search_pattern[1100];
        snprintf(search_pattern, sizeof(search_pattern), "%s\\*.dll", runtime_dir);
        WIN32_FIND_DATAA find_data;
        HANDLE hFind = FindFirstFileA(search_pattern, &find_data);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                char full_path[1200];
                snprintf(full_path, sizeof(full_path), "%s\\%s",
                         runtime_dir, find_data.cFileName);
                size_t fp_len = strlen(full_path);
                if (tpa_len + fp_len + 2 < sizeof(tpa_list)) {
                    if (tpa_len > 0) tpa_list[tpa_len++] = ';';
                    memcpy(tpa_list + tpa_len, full_path, fp_len);
                    tpa_len += fp_len;
                }
            } while (FindNextFileA(hFind, &find_data));
            FindClose(hFind);
        }
#else
        // On Unix, use opendir/readdir to scan for .dll managed assemblies.
        DIR *dir = opendir(runtime_dir);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                size_t name_len = strlen(entry->d_name);
                if (name_len > 4 && strcmp(entry->d_name + name_len - 4, ".dll") == 0) {
                    char full_path[1200];
                    snprintf(full_path, sizeof(full_path), "%s/%s",
                             runtime_dir, entry->d_name);
                    size_t fp_len = strlen(full_path);
                    if (tpa_len + fp_len + 2 < sizeof(tpa_list)) {
                        if (tpa_len > 0) tpa_list[tpa_len++] = ':';
                        memcpy(tpa_list + tpa_len, full_path, fp_len);
                        tpa_len += fp_len;
                    }
                }
            }
            closedir(dir);
        }
#endif
    }
    tpa_list[tpa_len] = '\0';

    // Initialize CoreCLR with the real TPA list.
    const char *property_keys[]   = {"TRUSTED_PLATFORM_ASSEMBLIES"};
    const char *property_values[] = {tpa_list};

    int rc = clr_init_fn_("polyglot", "PolyglotCompiler", 1,
                           property_keys, property_values,
                           &clr_host_, &clr_domain_);
    if (rc != 0) {
        fprintf(stderr, "[polyglot-dotnet] error: coreclr_initialize failed with "
                        "error code 0x%08x.  Check .NET SDK installation and "
                        "TRUSTED_PLATFORM_ASSEMBLIES configuration.\n", rc);
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

// ============================================================================
// __ploy_dotnet_* aliases
//
// The ploy frontend emits calls to __ploy_dotnet_* symbols.  These thin
// forwarding functions align the frontend names with the runtime's
// polyglot_dotnet_* ABI so that linking succeeds without special renaming.
// ============================================================================

int __ploy_dotnet_init(int version_hint) {
    return polyglot_dotnet_init(version_hint);
}

void __ploy_dotnet_shutdown(void) {
    polyglot_dotnet_shutdown();
}

void __ploy_dotnet_print(const char *message) {
    polyglot_dotnet_print(message);
}

char *__ploy_dotnet_strdup_gc(const char *message, void ***root_handle_out) {
    return polyglot_dotnet_strdup_gc(message, root_handle_out);
}

void __ploy_dotnet_release(char **ptr, void ***root_handle) {
    polyglot_dotnet_release(ptr, root_handle);
}

void *__ploy_dotnet_call_static(const char *assembly_name,
                                const char *type_name,
                                const char *method_name,
                                const void *const *args,
                                int arg_count) {
    return polyglot_dotnet_call_static(assembly_name, type_name, method_name, args, arg_count);
}

void *__ploy_dotnet_new_object(const char *assembly_name,
                               const char *type_name,
                               const void *const *args,
                               int arg_count) {
    return polyglot_dotnet_new_object(assembly_name, type_name, args, arg_count);
}

void *__ploy_dotnet_call_method(void *object,
                                const char *method_name,
                                const void *const *args,
                                int arg_count) {
    return polyglot_dotnet_call_method(object, method_name, args, arg_count);
}

void *__ploy_dotnet_get_property(void *object, const char *property_name) {
    return polyglot_dotnet_get_property(object, property_name);
}

void __ploy_dotnet_set_property(void *object,
                                const char *property_name,
                                const void *value) {
    polyglot_dotnet_set_property(object, property_name, value);
}

void __ploy_dotnet_dispose(void *object) {
    polyglot_dotnet_dispose(object);
}

void __ploy_dotnet_release_object(void *object) {
    polyglot_dotnet_release_object(object);
}
