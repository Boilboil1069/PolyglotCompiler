#include <stdio.h>
#include <string.h>

#include "runtime/include/libs/base.h"
#include "runtime/include/libs/dotnet_rt.h"

// ============================================================================
// .NET Runtime Bridge
//
// These functions provide a C-level interface between the PolyglotCompiler
// runtime and the .NET CoreCLR runtime.  At link time the actual hosting
// API calls are resolved; here we supply stub implementations that handle
// basic operations (print, string management, object lifecycle) so that the
// compiler toolchain can generate correct code without requiring a live CLR
// during compilation.
//
// Supported .NET versions: 6 (LTS), 7, 8 (LTS), 9.
// ============================================================================

static int dotnet_version_hint_ = 0;

int polyglot_dotnet_init(int version_hint) {
    dotnet_version_hint_ = version_hint;
    // In a full implementation this would load coreclr and call
    // coreclr_initialize() with the appropriate framework version.
    // Supported version_hint values: 6, 7, 8, 9, or 0 (auto).
    return 0; // success
}

void polyglot_dotnet_shutdown(void) {
    // In a full implementation this would call coreclr_shutdown().
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
    // Stub: in a full implementation this would use the CoreCLR hosting API
    // to invoke a static method on the specified type in the given assembly.
    (void)assembly_name;
    (void)type_name;
    (void)method_name;
    (void)args;
    (void)arg_count;
    return NULL;
}

void *polyglot_dotnet_new_object(const char *assembly_name,
                                 const char *type_name,
                                 const void *const *args,
                                 int arg_count) {
    // Stub: in a full implementation this would instantiate a .NET object
    // via the CoreCLR hosting API or through generated P/Invoke wrappers.
    (void)assembly_name;
    (void)type_name;
    (void)args;
    (void)arg_count;
    return NULL;
}

void *polyglot_dotnet_call_method(void *object,
                                  const char *method_name,
                                  const void *const *args,
                                  int arg_count) {
    // Stub: in a full implementation this would invoke an instance method
    // on a .NET object via generated delegate wrappers.
    (void)object;
    (void)method_name;
    (void)args;
    (void)arg_count;
    return NULL;
}

void *polyglot_dotnet_get_property(void *object,
                                   const char *property_name) {
    // Stub: in a full implementation this would call the property getter
    // on a .NET object via reflection or generated accessors.
    (void)object;
    (void)property_name;
    return NULL;
}

void polyglot_dotnet_set_property(void *object,
                                  const char *property_name,
                                  const void *value) {
    // Stub: in a full implementation this would call the property setter
    // on a .NET object via reflection or generated accessors.
    (void)object;
    (void)property_name;
    (void)value;
}

void polyglot_dotnet_dispose(void *object) {
    // Stub: in a full implementation this would check whether the object
    // implements IDisposable and call Dispose() if so.
    (void)object;
}

void polyglot_dotnet_release_object(void *object) {
    // Stub: in a full implementation this would release the GCHandle
    // or other reference holding the .NET object alive.
    (void)object;
}
