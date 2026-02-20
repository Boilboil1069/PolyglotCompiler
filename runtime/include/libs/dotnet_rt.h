#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the .NET runtime (CoreCLR) for cross-language interop.
// version_hint selects target framework:
//   6 -> .NET 6  (LTS)
//   7 -> .NET 7
//   8 -> .NET 8  (LTS)
//   9 -> .NET 9
//   0 -> auto-detect from installed SDK
int polyglot_dotnet_init(int version_hint);

// Shut down the .NET runtime.
void polyglot_dotnet_shutdown(void);

// Print a message via Console.WriteLine.
void polyglot_dotnet_print(const char *message);

// Duplicate a string into GC-managed memory.
char *polyglot_dotnet_strdup_gc(const char *message, void ***root_handle_out);

// Release a GC-rooted string.
void polyglot_dotnet_release(char **ptr, void ***root_handle);

// Invoke a static .NET method by fully qualified name (Namespace.Class::Method).
void *polyglot_dotnet_call_static(const char *assembly_name,
                                  const char *type_name,
                                  const char *method_name,
                                  const void *const *args,
                                  int arg_count);

// Instantiate a .NET object via constructor.
void *polyglot_dotnet_new_object(const char *assembly_name,
                                 const char *type_name,
                                 const void *const *args,
                                 int arg_count);

// Invoke an instance method on a .NET object.
void *polyglot_dotnet_call_method(void *object,
                                  const char *method_name,
                                  const void *const *args,
                                  int arg_count);

// Get a property value from a .NET object.
void *polyglot_dotnet_get_property(void *object,
                                   const char *property_name);

// Set a property value on a .NET object.
void polyglot_dotnet_set_property(void *object,
                                  const char *property_name,
                                  const void *value);

// Dispose a .NET object (calls IDisposable.Dispose if implemented).
void polyglot_dotnet_dispose(void *object);

// Release a .NET object reference.
void polyglot_dotnet_release_object(void *object);

#ifdef __cplusplus
}
#endif
