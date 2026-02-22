#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/include/libs/base.h"
#include "runtime/include/libs/java_rt.h"

// ============================================================================
// Java Runtime Bridge
//
// These functions provide a C-level interface between the PolyglotCompiler
// runtime and the Java Virtual Machine.  The JVM is loaded dynamically at
// runtime via LoadLibrary / dlopen so no hard dependency on a specific JDK
// is required at compile time.
// ============================================================================

// ============================================================================
// JNI dynamic loading infrastructure
//
// On Windows we use LoadLibrary/GetProcAddress to locate jvm.dll at runtime.
// On other platforms we use dlopen/dlsym with libjvm.so.
// This avoids a hard link-time dependency on a specific JDK installation.
// ============================================================================

#ifdef _WIN32
#include <windows.h>
typedef HMODULE jvm_lib_t;
#define JVM_LOAD(path)       LoadLibraryA(path)
#define JVM_SYM(lib, name)   ((void *)GetProcAddress(lib, name))
#define JVM_UNLOAD(lib)      FreeLibrary(lib)
#else
#include <dlfcn.h>
typedef void *jvm_lib_t;
#define JVM_LOAD(path)       dlopen(path, RTLD_LAZY)
#define JVM_SYM(lib, name)   dlsym(lib, name)
#define JVM_UNLOAD(lib)      dlclose(lib)
#endif

// Minimal JNI type definitions to avoid requiring jni.h at compile time.
// These match the layout defined by the JNI specification.
typedef int jint;
typedef long long jlong;
typedef unsigned char jboolean;

typedef void *jobject;
typedef jobject jclass;
typedef jobject jstring;
typedef jobject jmethodID;
typedef jobject jfieldID;
typedef void *JNIEnv;
typedef void *JavaVM;

// JNI function pointer types that we resolve at runtime.
typedef jint (*JNI_CreateJavaVM_t)(JavaVM **pvm, void **penv, void *args);
typedef jint (*JNI_GetCreatedJavaVMs_t)(JavaVM **vmBuf, jint bufLen, jint *nVMs);

static jvm_lib_t  jvm_lib_       = NULL;
static JavaVM    *jvm_instance_  = NULL;
static JNIEnv    *jni_env_       = NULL;

// JNI interface table offsets (from the JNI spec, version-stable).
// We access these through function pointer arrays embedded in JNIEnv / JavaVM.
//   JNIEnv is a pointer to a pointer to a function table.
//   (*env)->FindClass(env, name) etc.
// For type-safe access we define small dispatch helpers below.

static void *jni_call(int offset) {
    // Return the function pointer at the given offset in the JNI function table.
    if (!jni_env_) return NULL;
    void **table = *(void ***)jni_env_;
    return table[offset];
}

// JNI function table offsets (JNI 1.6+ spec, stable across versions).
#define JNI_FINDCLASS       6
#define JNI_GETSTATICMETHOD 113
#define JNI_CALLSTATICOBJ   114
#define JNI_GETMETHOD       33
#define JNI_CALLOBJ         34
#define JNI_NEWOBJECT       28
#define JNI_GETFIELDID      94
#define JNI_GETOBJFIELD     95
#define JNI_SETFIELDID      94
#define JNI_SETOBJFIELD     104
#define JNI_DELETEGLOBALREF 22
#define JNI_NEWGLOBALREF    21
#define JNI_EXCEPTIONCHECK  228
#define JNI_EXCEPTIONCLEAR  17

static int java_version_hint_ = 0;

int polyglot_java_init(int version_hint) {
    java_version_hint_ = version_hint;

    // Attempt to locate jvm.dll / libjvm.so on the system.
    // We first try JAVA_HOME, then fall back to a well-known default path.
    const char *java_home = getenv("JAVA_HOME");
    char jvm_path[1024] = {0};

    if (java_home && java_home[0]) {
#ifdef _WIN32
        snprintf(jvm_path, sizeof(jvm_path), "%s\\bin\\server\\jvm.dll", java_home);
#elif defined(__APPLE__)
        snprintf(jvm_path, sizeof(jvm_path), "%s/lib/server/libjvm.dylib", java_home);
#else
        snprintf(jvm_path, sizeof(jvm_path), "%s/lib/server/libjvm.so", java_home);
#endif
    } else {
        fprintf(stderr, "[polyglot-java] warning: JAVA_HOME is not set; "
                        "JVM cannot be loaded.  Set JAVA_HOME to your JDK installation.\n");
    }

    if (jvm_path[0]) {
        jvm_lib_ = JVM_LOAD(jvm_path);
    }

    if (!jvm_lib_) {
        // Try common fallback paths on each platform.
#ifdef _WIN32
        jvm_lib_ = JVM_LOAD("jvm.dll");
#elif defined(__APPLE__)
        jvm_lib_ = JVM_LOAD("/usr/libexec/java_home/../lib/server/libjvm.dylib");
#else
        jvm_lib_ = JVM_LOAD("libjvm.so");
#endif
    }

    if (!jvm_lib_) {
        fprintf(stderr, "[polyglot-java] error: JVM library not found at '%s'. "
                        "Ensure JAVA_HOME points to a valid JDK installation.\n",
                jvm_path[0] ? jvm_path : "(default search path)");
        return -1;
    }

    // Resolve JNI_CreateJavaVM.
    JNI_CreateJavaVM_t create_vm =
        (JNI_CreateJavaVM_t)JVM_SYM(jvm_lib_, "JNI_CreateJavaVM");
    if (!create_vm) {
        fprintf(stderr, "[polyglot-java] error: JVM library loaded but "
                        "JNI_CreateJavaVM symbol not found.  The library may be corrupt.\n");
        JVM_UNLOAD(jvm_lib_);
        jvm_lib_ = NULL;
        return -1;
    }

    // Build JNI init args.
    // JavaVMInitArgs is { jint version; jint nOptions; JavaVMOption *options; jboolean ignoreUnrecognized; }
    // We only need the version field for basic initialisation.
    struct {
        jint version;
        jint nOptions;
        void *options;
        jboolean ignoreUnrecognized;
    } vm_args;
    memset(&vm_args, 0, sizeof(vm_args));

    // Map version_hint to JNI version constants.
    // JNI_VERSION_1_8 = 0x00010008, JNI_VERSION_10 = 0x000a0000, etc.
    switch (version_hint) {
        case 8:  vm_args.version = 0x00010008; break;  // JNI_VERSION_1_8
        case 17: vm_args.version = 0x00130000; break;  // JNI_VERSION_19 (17 maps to 19)
        case 21: vm_args.version = 0x00150000; break;  // JNI_VERSION_21
        case 23: vm_args.version = 0x00150000; break;  // Use 21 as baseline
        default: vm_args.version = 0x00010008; break;  // Default to 1.8
    }
    vm_args.ignoreUnrecognized = 1;

    jint rc = create_vm(&jvm_instance_, (void **)&jni_env_, &vm_args);
    if (rc != 0) {
        fprintf(stderr, "[polyglot-java] error: JNI_CreateJavaVM failed with "
                        "error code %d.  Check JVM compatibility and available memory.\n",
                (int)rc);
        jvm_instance_ = NULL;
        jni_env_ = NULL;
        JVM_UNLOAD(jvm_lib_);
        jvm_lib_ = NULL;
        return -1;
    }

    return 0;
}

void polyglot_java_shutdown(void) {
    if (jvm_instance_) {
        // DestroyJavaVM is at offset 3 in the JavaVM function table.
        void **table = *(void ***)jvm_instance_;
        typedef jint (*DestroyJavaVM_fn)(JavaVM *);
        DestroyJavaVM_fn destroy = (DestroyJavaVM_fn)table[3];
        if (destroy) destroy(jvm_instance_);
        jvm_instance_ = NULL;
        jni_env_ = NULL;
    }
    if (jvm_lib_) {
        JVM_UNLOAD(jvm_lib_);
        jvm_lib_ = NULL;
    }
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
    if (!jni_env_ || !class_name || !method_name || !signature) return NULL;

    // FindClass
    typedef jclass (*FindClass_fn)(JNIEnv *, const char *);
    FindClass_fn find_class = (FindClass_fn)jni_call(JNI_FINDCLASS);
    if (!find_class) return NULL;
    jclass cls = find_class(jni_env_, class_name);
    if (!cls) return NULL;

    // GetStaticMethodID
    typedef jmethodID (*GetStaticMethodID_fn)(JNIEnv *, jclass, const char *, const char *);
    GetStaticMethodID_fn get_method = (GetStaticMethodID_fn)jni_call(JNI_GETSTATICMETHOD);
    if (!get_method) return NULL;
    jmethodID mid = get_method(jni_env_, cls, method_name, signature);
    if (!mid) return NULL;

    // CallStaticObjectMethodA — we pass args as a jvalue array.
    // Each jvalue is 8 bytes (union of all JNI primitive types + jobject).
    typedef jobject (*CallStaticObjectMethodA_fn)(JNIEnv *, jclass, jmethodID, const void *);
    CallStaticObjectMethodA_fn call_fn = (CallStaticObjectMethodA_fn)jni_call(JNI_CALLSTATICOBJ + 2);
    if (!call_fn) return NULL;

    jobject result = call_fn(jni_env_, cls, mid, args);

    // Check for pending exceptions.
    typedef jboolean (*ExceptionCheck_fn)(JNIEnv *);
    ExceptionCheck_fn exc_check = (ExceptionCheck_fn)jni_call(JNI_EXCEPTIONCHECK);
    if (exc_check && exc_check(jni_env_)) {
        typedef void (*ExceptionClear_fn)(JNIEnv *);
        ExceptionClear_fn exc_clear = (ExceptionClear_fn)jni_call(JNI_EXCEPTIONCLEAR);
        if (exc_clear) exc_clear(jni_env_);
        return NULL;
    }

    // Wrap the result in a global reference so it survives frame pops.
    if (result) {
        typedef jobject (*NewGlobalRef_fn)(JNIEnv *, jobject);
        NewGlobalRef_fn new_ref = (NewGlobalRef_fn)jni_call(JNI_NEWGLOBALREF);
        if (new_ref) result = new_ref(jni_env_, result);
    }

    return result;
}

void *polyglot_java_new_object(const char *class_name, const char *ctor_signature,
                               const void *const *args, int arg_count) {
    if (!jni_env_ || !class_name || !ctor_signature) return NULL;

    typedef jclass (*FindClass_fn)(JNIEnv *, const char *);
    FindClass_fn find_class = (FindClass_fn)jni_call(JNI_FINDCLASS);
    if (!find_class) return NULL;
    jclass cls = find_class(jni_env_, class_name);
    if (!cls) return NULL;

    typedef jmethodID (*GetMethodID_fn)(JNIEnv *, jclass, const char *, const char *);
    GetMethodID_fn get_method = (GetMethodID_fn)jni_call(JNI_GETMETHOD);
    if (!get_method) return NULL;
    jmethodID mid = get_method(jni_env_, cls, "<init>", ctor_signature);
    if (!mid) return NULL;

    // NewObjectA — pass args as jvalue array.
    typedef jobject (*NewObjectA_fn)(JNIEnv *, jclass, jmethodID, const void *);
    NewObjectA_fn new_obj = (NewObjectA_fn)jni_call(JNI_NEWOBJECT + 2);
    if (!new_obj) return NULL;

    jobject result = new_obj(jni_env_, cls, mid, args);

    if (result) {
        typedef jobject (*NewGlobalRef_fn)(JNIEnv *, jobject);
        NewGlobalRef_fn new_ref = (NewGlobalRef_fn)jni_call(JNI_NEWGLOBALREF);
        if (new_ref) result = new_ref(jni_env_, result);
    }

    return result;
}

void *polyglot_java_call_method(void *object, const char *method_name,
                                const char *signature, const void *const *args,
                                int arg_count) {
    if (!jni_env_ || !object || !method_name || !signature) return NULL;

    // Get the class of the object.
    typedef jclass (*GetObjectClass_fn)(JNIEnv *, jobject);
    GetObjectClass_fn get_class = (GetObjectClass_fn)jni_call(31); // GetObjectClass
    if (!get_class) return NULL;
    jclass cls = get_class(jni_env_, (jobject)object);
    if (!cls) return NULL;

    typedef jmethodID (*GetMethodID_fn)(JNIEnv *, jclass, const char *, const char *);
    GetMethodID_fn get_method = (GetMethodID_fn)jni_call(JNI_GETMETHOD);
    if (!get_method) return NULL;
    jmethodID mid = get_method(jni_env_, cls, method_name, signature);
    if (!mid) return NULL;

    typedef jobject (*CallObjectMethodA_fn)(JNIEnv *, jobject, jmethodID, const void *);
    CallObjectMethodA_fn call_fn = (CallObjectMethodA_fn)jni_call(JNI_CALLOBJ + 2);
    if (!call_fn) return NULL;

    jobject result = call_fn(jni_env_, (jobject)object, mid, args);

    typedef jboolean (*ExceptionCheck_fn)(JNIEnv *);
    ExceptionCheck_fn exc_check = (ExceptionCheck_fn)jni_call(JNI_EXCEPTIONCHECK);
    if (exc_check && exc_check(jni_env_)) {
        typedef void (*ExceptionClear_fn)(JNIEnv *);
        ExceptionClear_fn exc_clear = (ExceptionClear_fn)jni_call(JNI_EXCEPTIONCLEAR);
        if (exc_clear) exc_clear(jni_env_);
        return NULL;
    }

    if (result) {
        typedef jobject (*NewGlobalRef_fn)(JNIEnv *, jobject);
        NewGlobalRef_fn new_ref = (NewGlobalRef_fn)jni_call(JNI_NEWGLOBALREF);
        if (new_ref) result = new_ref(jni_env_, result);
    }

    return result;
}

void *polyglot_java_get_field(void *object, const char *field_name,
                              const char *field_type) {
    if (!jni_env_ || !object || !field_name || !field_type) return NULL;

    typedef jclass (*GetObjectClass_fn)(JNIEnv *, jobject);
    GetObjectClass_fn get_class = (GetObjectClass_fn)jni_call(31);
    if (!get_class) return NULL;
    jclass cls = get_class(jni_env_, (jobject)object);
    if (!cls) return NULL;

    typedef jfieldID (*GetFieldID_fn)(JNIEnv *, jclass, const char *, const char *);
    GetFieldID_fn get_fid = (GetFieldID_fn)jni_call(JNI_GETFIELDID);
    if (!get_fid) return NULL;
    jfieldID fid = get_fid(jni_env_, cls, field_name, field_type);
    if (!fid) return NULL;

    typedef jobject (*GetObjectField_fn)(JNIEnv *, jobject, jfieldID);
    GetObjectField_fn get_field = (GetObjectField_fn)jni_call(JNI_GETOBJFIELD);
    if (!get_field) return NULL;

    jobject result = get_field(jni_env_, (jobject)object, fid);

    if (result) {
        typedef jobject (*NewGlobalRef_fn)(JNIEnv *, jobject);
        NewGlobalRef_fn new_ref = (NewGlobalRef_fn)jni_call(JNI_NEWGLOBALREF);
        if (new_ref) result = new_ref(jni_env_, result);
    }

    return result;
}

void polyglot_java_set_field(void *object, const char *field_name,
                             const char *field_type, const void *value) {
    if (!jni_env_ || !object || !field_name || !field_type) return;

    typedef jclass (*GetObjectClass_fn)(JNIEnv *, jobject);
    GetObjectClass_fn get_class = (GetObjectClass_fn)jni_call(31);
    if (!get_class) return;
    jclass cls = get_class(jni_env_, (jobject)object);
    if (!cls) return;

    typedef jfieldID (*GetFieldID_fn)(JNIEnv *, jclass, const char *, const char *);
    GetFieldID_fn get_fid = (GetFieldID_fn)jni_call(JNI_GETFIELDID);
    if (!get_fid) return;
    jfieldID fid = get_fid(jni_env_, cls, field_name, field_type);
    if (!fid) return;

    typedef void (*SetObjectField_fn)(JNIEnv *, jobject, jfieldID, jobject);
    SetObjectField_fn set_field = (SetObjectField_fn)jni_call(JNI_SETOBJFIELD);
    if (!set_field) return;

    set_field(jni_env_, (jobject)object, fid, (jobject)value);
}

void polyglot_java_release_object(void *object) {
    if (!jni_env_ || !object) return;

    typedef void (*DeleteGlobalRef_fn)(JNIEnv *, jobject);
    DeleteGlobalRef_fn del_ref = (DeleteGlobalRef_fn)jni_call(JNI_DELETEGLOBALREF);
    if (del_ref) {
        del_ref(jni_env_, (jobject)object);
    }
}

// ============================================================================
// __ploy_java_* aliases
//
// The ploy frontend emits calls to __ploy_java_* symbols.  These thin
// forwarding functions align the frontend names with the runtime's
// polyglot_java_* ABI so that linking succeeds without special renaming.
// ============================================================================

int __ploy_java_init(int version_hint) {
    return polyglot_java_init(version_hint);
}

void __ploy_java_shutdown(void) {
    polyglot_java_shutdown();
}

void __ploy_java_print(const char *message) {
    polyglot_java_print(message);
}

char *__ploy_java_strdup_gc(const char *message, void ***root_handle_out) {
    return polyglot_java_strdup_gc(message, root_handle_out);
}

void __ploy_java_release(void *object) {
    polyglot_java_release_object(object);
}

void *__ploy_java_call_static(const char *class_name, const char *method_name,
                              const char *signature, const void *const *args,
                              int arg_count) {
    return polyglot_java_call_static(class_name, method_name, signature, args, arg_count);
}

void *__ploy_java_new_object(const char *class_name, const char *ctor_signature,
                             const void *const *args, int arg_count) {
    return polyglot_java_new_object(class_name, ctor_signature, args, arg_count);
}

void *__ploy_java_call_method(void *object, const char *method_name,
                              const char *signature, const void *const *args,
                              int arg_count) {
    return polyglot_java_call_method(object, method_name, signature, args, arg_count);
}

void *__ploy_java_get_field(void *object, const char *field_name,
                            const char *field_type) {
    return polyglot_java_get_field(object, field_name, field_type);
}

void __ploy_java_set_field(void *object, const char *field_name,
                           const char *field_type, const void *value) {
    polyglot_java_set_field(object, field_name, field_type, value);
}

void __ploy_java_release_object(void *object) {
    polyglot_java_release_object(object);
}
