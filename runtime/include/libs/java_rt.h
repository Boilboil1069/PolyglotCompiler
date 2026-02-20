#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the JVM for cross-language interop.
// version_hint selects JVM behaviour:
//   8  -> Java 8 (1.8) compatibility
//   17 -> Java 17 (LTS) compatibility
//   21 -> Java 21 (LTS) compatibility
//   23 -> Java 23 compatibility
//   0  -> auto-detect from the installed JDK
int polyglot_java_init(int version_hint);

// Shut down the JVM.
void polyglot_java_shutdown(void);

// Print a message via System.out.println.
void polyglot_java_print(const char *message);

// Duplicate a string into GC-managed memory.
char *polyglot_java_strdup_gc(const char *message, void ***root_handle_out);

// Release a GC-rooted string.
void polyglot_java_release(char **ptr, void ***root_handle);

// Invoke a static Java method by fully qualified name.
// Returns an opaque handle to the result or NULL on failure.
void *polyglot_java_call_static(const char *class_name, const char *method_name,
                                const char *signature, const void *const *args,
                                int arg_count);

// Instantiate a Java object (calls <init>).
void *polyglot_java_new_object(const char *class_name, const char *ctor_signature,
                               const void *const *args, int arg_count);

// Invoke an instance method on a Java object.
void *polyglot_java_call_method(void *object, const char *method_name,
                                const char *signature, const void *const *args,
                                int arg_count);

// Get a field value from a Java object.
void *polyglot_java_get_field(void *object, const char *field_name,
                              const char *field_type);

// Set a field value on a Java object.
void polyglot_java_set_field(void *object, const char *field_name,
                             const char *field_type, const void *value);

// Release/dispose a Java object reference.
void polyglot_java_release_object(void *object);

#ifdef __cplusplus
}
#endif
