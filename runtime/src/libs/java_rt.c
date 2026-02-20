#include <stdio.h>
#include <string.h>

#include "runtime/include/libs/base.h"
#include "runtime/include/libs/java_rt.h"

// ============================================================================
// Java Runtime Bridge
//
// These functions provide a C-level interface between the PolyglotCompiler
// runtime and the Java Virtual Machine.  At link time the actual JNI calls
// are resolved; here we supply stub implementations that handle basic
// operations (print, string management, object lifecycle) so that the
// compiler toolchain can generate correct code without requiring a live JVM
// during compilation.
// ============================================================================

static int java_version_hint_ = 0;

int polyglot_java_init(int version_hint) {
    java_version_hint_ = version_hint;
    // In a full implementation this would load libjvm and call
    // JNI_CreateJavaVM with the appropriate version options.
    // Supported version_hint values: 8, 17, 21, 23, or 0 (auto).
    return 0; // success
}

void polyglot_java_shutdown(void) {
    // In a full implementation this would call DestroyJavaVM().
    java_version_hint_ = 0;
}

void polyglot_java_print(const char *message) {
    if (!message) return;
    printf("%s\n", message);
}

char *polyglot_java_strdup_gc(const char *message, void ***root_handle_out) {
    if (!message) return NULL;
    size_t len = strlen(message) + 1;
    char *buf = (char *)polyglot_alloc(len);
    if (!buf) return NULL;
    memcpy(buf, message, len);
    polyglot_gc_register_root((void **)&buf);
    if (root_handle_out) *root_handle_out = (void **)&buf;
    return buf;
}

void polyglot_java_release(char **ptr, void ***root_handle) {
    if (!ptr || !*ptr) return;
    polyglot_gc_unregister_root((void **)ptr);
    *ptr = NULL;
    if (root_handle) *root_handle = NULL;
}

void *polyglot_java_call_static(const char *class_name, const char *method_name,
                                const char *signature, const void *const *args,
                                int arg_count) {
    // Stub: in a full implementation this would use JNI CallStaticObjectMethod
    // with the given class, method, signature, and arguments.
    (void)class_name;
    (void)method_name;
    (void)signature;
    (void)args;
    (void)arg_count;
    return NULL;
}

void *polyglot_java_new_object(const char *class_name, const char *ctor_signature,
                               const void *const *args, int arg_count) {
    // Stub: in a full implementation this would use JNI NewObject
    // to instantiate a Java object with the given constructor.
    (void)class_name;
    (void)ctor_signature;
    (void)args;
    (void)arg_count;
    return NULL;
}

void *polyglot_java_call_method(void *object, const char *method_name,
                                const char *signature, const void *const *args,
                                int arg_count) {
    // Stub: in a full implementation this would use JNI CallObjectMethod.
    (void)object;
    (void)method_name;
    (void)signature;
    (void)args;
    (void)arg_count;
    return NULL;
}

void *polyglot_java_get_field(void *object, const char *field_name,
                              const char *field_type) {
    // Stub: in a full implementation this would use JNI GetObjectField.
    (void)object;
    (void)field_name;
    (void)field_type;
    return NULL;
}

void polyglot_java_set_field(void *object, const char *field_name,
                             const char *field_type, const void *value) {
    // Stub: in a full implementation this would use JNI SetObjectField.
    (void)object;
    (void)field_name;
    (void)field_type;
    (void)value;
}

void polyglot_java_release_object(void *object) {
    // Stub: in a full implementation this would use JNI DeleteGlobalRef
    // or DeleteLocalRef to release the Java object reference.
    (void)object;
}
