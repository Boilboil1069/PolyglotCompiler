/**
 * @file     class_file_reader.h
 * @brief    JVM .class / .jar reader (per JVMS §4)
 *
 * @ingroup  Frontend / Java
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/language_frontend.h"

namespace polyglot::java {

// ============================================================================
// Constant-pool entry tags from JVMS §4.4
// ============================================================================
enum class CpTag : std::uint8_t {
    kUtf8               = 1,
    kInteger            = 3,
    kFloat              = 4,
    kLong               = 5,
    kDouble             = 6,
    kClass              = 7,
    kString             = 8,
    kFieldRef           = 9,
    kMethodRef          = 10,
    kInterfaceMethodRef = 11,
    kNameAndType        = 12,
    kMethodHandle       = 15,
    kMethodType         = 16,
    kDynamic            = 17,
    kInvokeDynamic      = 18,
    kModule             = 19,
    kPackage            = 20,
};

struct CpEntry {
    CpTag tag{CpTag::kUtf8};
    // Polymorphic payload, using one struct keeps allocations contiguous.
    std::string utf8;             // kUtf8
    std::int32_t i32{0};          // kInteger / Float (bits)
    std::int64_t i64{0};          // kLong / Double (bits)
    std::uint16_t a{0};           // first 16-bit index
    std::uint16_t b{0};           // second 16-bit index
};

// ============================================================================
// Decoded class metadata (subset relevant to symbol-table integration)
// ============================================================================
struct FieldInfo {
    std::uint16_t access{0};
    std::string name;
    std::string descriptor;       // JVMS §4.3.2 field descriptor, e.g. "Ljava/lang/String;"
    std::string signature;        // optional generic signature, JVMS §4.7.9
};

struct MethodInfo {
    std::uint16_t access{0};
    std::string name;
    std::string descriptor;       // e.g. "(ILjava/lang/String;)V"
    std::string signature;        // optional generic signature
};

struct ClassFile {
    std::uint16_t minor_version{0};
    std::uint16_t major_version{0};
    std::vector<CpEntry> constants;   // index 0 unused (matches JVMS 1-based indexing)
    std::uint16_t access_flags{0};
    std::string this_class;           // fully qualified, dot-separated
    std::string super_class;          // empty for java.lang.Object
    std::vector<std::string> interfaces;
    std::vector<FieldInfo> fields;
    std::vector<MethodInfo> methods;

    // module-info.class only
    bool is_module{false};
    std::string module_name;
    std::vector<std::string> module_exports;   // "com/foo/api"
    std::vector<std::string> module_requires;  // "java.base"
};

// ============================================================================
// ParseClassFile — decode a single .class blob.
// Returns nullopt and emits a diagnostic on malformed input.
// ============================================================================
std::optional<ClassFile> ParseClassFile(const std::vector<std::uint8_t> &bytes,
                                        const std::string &source_label,
                                        frontends::Diagnostics &diags);

std::optional<ClassFile> ReadClassFile(const std::string &path,
                                       frontends::Diagnostics &diags);

// ============================================================================
// JarReader — central-directory based ZIP reader specialised for .jar.
// Only STORED (method 0) and DEFLATE (method 8) entries are supported, which
// covers every .class produced by `javac` / `jar` / `gradle` etc.
// ============================================================================
class JarReader {
  public:
    static std::shared_ptr<JarReader> Open(const std::string &path,
                                           frontends::Diagnostics &diags);

    // List archive entries (forward slashes, e.g. "java/util/List.class").
    const std::vector<std::string> &Entries() const { return entry_names_; }

    // Read raw bytes for an entry; nullopt if not found or decompression fails.
    std::optional<std::vector<std::uint8_t>>
    Read(const std::string &name, frontends::Diagnostics &diags) const;

  private:
    struct Entry {
        std::uint16_t method{0};
        std::uint32_t crc32{0};
        std::uint64_t compressed{0};
        std::uint64_t uncompressed{0};
        std::uint64_t local_header_offset{0};
    };

    std::vector<std::uint8_t> data_;
    std::string path_;
    std::vector<std::string> entry_names_;
    std::unordered_map<std::string, Entry> entries_;
};

// ============================================================================
// ClasspathLoader — walk classpath entries (dirs + .jar) and resolve a
// fully-qualified Java name (e.g. "java.util.List") to a decoded ClassFile.
// Lazily caches results.
// ============================================================================
class ClasspathLoader {
  public:
    explicit ClasspathLoader(std::vector<std::string> classpath,
                             frontends::Diagnostics &diags);

    // Look up "java.util.List" → optional<ClassFile>.
    // Wildcard imports (e.g. "java.util.*") are NOT handled here — call
    // ListPackage() instead.
    const ClassFile *Resolve(const std::string &qualified_name);

    // Enumerate all classes in a Java package (e.g. "java.util") in the order
    // they were discovered on the classpath.  Cached after first call.
    const std::vector<const ClassFile *> &
    ListPackage(const std::string &package_name);

  private:
    std::vector<std::string> classpath_;
    frontends::Diagnostics &diags_;
    // path-on-disk → opened jar
    std::unordered_map<std::string, std::shared_ptr<JarReader>> jars_;
    // qualified name → owned ClassFile (cache)
    std::unordered_map<std::string, std::unique_ptr<ClassFile>> cache_;
    // package name → list of pointers (into cache_)
    std::unordered_map<std::string, std::vector<const ClassFile *>> package_index_;

    std::optional<std::vector<std::uint8_t>>
    LocateBytes(const std::string &qualified_name);
    void IndexPackage(const std::string &package_name);
};

// ============================================================================
// Convert a JVM field descriptor (e.g. "Ljava/util/List;") or method descriptor
// (e.g. "(ILjava/lang/String;)V") into ForeignFunctionSignature components.
// ============================================================================
core::Type DescriptorToCoreType(const std::string &desc);

// Parse a method descriptor into (param_types, return_type).
struct ParsedMethodDescriptor {
    std::vector<core::Type> params;
    core::Type ret{core::Type::Any()};
};

ParsedMethodDescriptor ParseMethodDescriptor(const std::string &desc);

}  // namespace polyglot::java
