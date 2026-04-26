/**
 * @file     metadata_reader.cpp
 * @brief    ECMA-335 CLI metadata reader implementation.
 *
 *           Reads .NET managed assemblies (PE32/PE32+ images carrying a
 *           CLI header) and decodes the metadata streams (#~, #Strings,
 *           #Blob, #GUID, #US) per ECMA-335 §II.22-25.  Only the tables
 *           and signature kinds needed by the symbol-table populator are
 *           materialised; the others are skipped using their declared
 *           row size so the row index stays in sync.
 *
 * @ingroup  Frontend / DotNet
 * @author   Manning Cyrus
 * @date     2026-04-26
 */

#include "frontends/dotnet/include/metadata_reader.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace polyglot::dotnet {
namespace {

namespace fs = std::filesystem;

// ============================================================================
// Diagnostics helper
// ============================================================================
core::SourceLoc LocOf(const std::string &p) { return core::SourceLoc{p, 0, 0}; }

bool LoadFile(const std::string &path, std::vector<std::uint8_t> &out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streamsize n = f.tellg();
    if (n < 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(std::size_t(n));
    if (n > 0 && !f.read(reinterpret_cast<char *>(out.data()), n)) return false;
    return true;
}

// ============================================================================
// Little-endian reader
// ============================================================================
class LEReader {
  public:
    LEReader(const std::uint8_t *base, std::size_t size, std::size_t off = 0)
        : base_(base), size_(size), pos_(off) {}

    bool ok() const { return !fail_; }
    std::size_t tell() const { return pos_; }
    void seek(std::size_t p) { pos_ = p; }
    void skip(std::size_t n) { pos_ += n; if (pos_ > size_) { pos_ = size_; fail_ = true; } }
    bool has(std::size_t n) const { return pos_ + n <= size_; }
    const std::uint8_t *ptr() const { return base_ + pos_; }

    std::uint8_t  u8()  { if (!has(1)) { fail_ = true; return 0; } return base_[pos_++]; }
    std::uint16_t u16() {
        if (!has(2)) { fail_ = true; return 0; }
        std::uint16_t v = std::uint16_t(base_[pos_]) | (std::uint16_t(base_[pos_+1]) << 8);
        pos_ += 2;
        return v;
    }
    std::uint32_t u32() {
        if (!has(4)) { fail_ = true; return 0; }
        std::uint32_t v = std::uint32_t(base_[pos_])        |
                          (std::uint32_t(base_[pos_+1]) << 8) |
                          (std::uint32_t(base_[pos_+2]) << 16) |
                          (std::uint32_t(base_[pos_+3]) << 24);
        pos_ += 4;
        return v;
    }
    std::uint64_t u64() {
        std::uint64_t lo = u32();
        std::uint64_t hi = u32();
        return lo | (hi << 32);
    }

    // Read NUL-terminated ASCII string (used by metadata version + stream names).
    std::string cstr() {
        std::string s;
        while (has(1)) {
            char c = char(base_[pos_++]);
            if (c == 0) return s;
            s.push_back(c);
        }
        fail_ = true;
        return s;
    }

  private:
    const std::uint8_t *base_;
    std::size_t size_;
    std::size_t pos_;
    bool fail_{false};
};

// ============================================================================
// PE → CLI metadata locator
// ============================================================================
struct SectionHeader {
    char name[8];
    std::uint32_t virtual_size;
    std::uint32_t virtual_address;
    std::uint32_t raw_size;
    std::uint32_t raw_pointer;
};

struct PEInfo {
    std::vector<SectionHeader> sections;
    std::uint32_t cli_rva{0};
    std::uint32_t cli_size{0};
};

// Map an RVA to a file offset using the section table.
std::optional<std::size_t> RvaToOffset(const PEInfo &pe, std::uint32_t rva) {
    for (const auto &s : pe.sections) {
        if (rva >= s.virtual_address && rva < s.virtual_address + s.virtual_size) {
            std::uint32_t delta = rva - s.virtual_address;
            return std::size_t(s.raw_pointer + delta);
        }
    }
    return std::nullopt;
}

bool ReadPE(const std::vector<std::uint8_t> &bytes, PEInfo &out,
            const std::string &path, frontends::Diagnostics &diags) {
    LEReader r(bytes.data(), bytes.size(), 0);
    if (bytes.size() < 0x40) {
        diags.ReportError(LocOf(path), frontends::ErrorCode::kUnknown,
                          "PE: file too small: " + path);
        return false;
    }
    if (bytes[0] != 'M' || bytes[1] != 'Z') {
        diags.ReportError(LocOf(path), frontends::ErrorCode::kUnknown,
                          "PE: missing MZ signature: " + path);
        return false;
    }
    r.seek(0x3C);
    std::uint32_t pe_off = r.u32();
    r.seek(pe_off);
    if (r.u32() != 0x00004550u) {  // "PE\0\0"
        diags.ReportError(LocOf(path), frontends::ErrorCode::kUnknown,
                          "PE: missing PE\\0\\0 signature: " + path);
        return false;
    }
    // COFF header
    r.skip(2);                                    // machine
    std::uint16_t num_sections = r.u16();
    r.skip(4);                                    // timestamp
    r.skip(4);                                    // symtab ptr
    r.skip(4);                                    // num symbols
    std::uint16_t opt_size = r.u16();
    r.skip(2);                                    // characteristics

    std::size_t opt_start = r.tell();
    if (opt_size < 2) {
        diags.ReportError(LocOf(path), frontends::ErrorCode::kUnknown,
                          "PE: missing optional header: " + path);
        return false;
    }
    std::uint16_t magic = r.u16();
    bool pe32plus = (magic == 0x20B);
    bool pe32     = (magic == 0x10B);
    if (!pe32 && !pe32plus) {
        diags.ReportError(LocOf(path), frontends::ErrorCode::kUnknown,
                          "PE: unknown optional-header magic: " + path);
        return false;
    }
    // Skip to NumberOfRvaAndSizes.  Layout differs between PE32 and PE32+.
    // Standard fields: PE32 = 26 bytes after magic, PE32+ = 22 bytes.
    // Then NT-specific fields up to NumberOfRvaAndSizes.
    // We compute the offset of NumberOfRvaAndSizes from opt_start:
    //   PE32:  92,  PE32+: 108
    std::size_t num_dirs_off = opt_start + (pe32plus ? 108 : 92);
    r.seek(num_dirs_off);
    std::uint32_t num_dirs = r.u32();
    if (num_dirs < 15) {
        diags.ReportError(LocOf(path), frontends::ErrorCode::kUnknown,
                          "PE: not enough data directories: " + path);
        return false;
    }
    // Data directory 14 is CLI header; each entry is 8 bytes.
    r.seek(num_dirs_off + 4 + 14 * 8);
    out.cli_rva  = r.u32();
    out.cli_size = r.u32();
    if (out.cli_rva == 0) {
        diags.ReportError(LocOf(path), frontends::ErrorCode::kUnknown,
                          "PE: not a managed assembly (no CLI header): " + path);
        return false;
    }
    // Section table starts right after the optional header.
    r.seek(opt_start + opt_size);
    out.sections.resize(num_sections);
    for (std::uint16_t i = 0; i < num_sections; ++i) {
        SectionHeader &s = out.sections[i];
        std::memcpy(s.name, r.ptr(), 8);
        r.skip(8);
        s.virtual_size    = r.u32();
        s.virtual_address = r.u32();
        s.raw_size        = r.u32();
        s.raw_pointer     = r.u32();
        r.skip(16);                              // remaining fields
    }
    return r.ok();
}

// ============================================================================
// Metadata streams + tables
// ============================================================================
constexpr std::uint8_t kTabModule       = 0x00;
constexpr std::uint8_t kTabTypeRef      = 0x01;
constexpr std::uint8_t kTabTypeDef      = 0x02;
constexpr std::uint8_t kTabField        = 0x04;
constexpr std::uint8_t kTabMethodDef    = 0x06;
constexpr std::uint8_t kTabParam        = 0x08;
constexpr std::uint8_t kTabInterfaceImpl= 0x09;
constexpr std::uint8_t kTabMemberRef    = 0x0A;
constexpr std::uint8_t kTabConstant     = 0x0B;
constexpr std::uint8_t kTabCustomAttr   = 0x0C;
constexpr std::uint8_t kTabFieldMarshal = 0x0D;
constexpr std::uint8_t kTabDeclSecurity = 0x0E;
constexpr std::uint8_t kTabClassLayout  = 0x0F;
constexpr std::uint8_t kTabFieldLayout  = 0x10;
constexpr std::uint8_t kTabStandAloneSig= 0x11;
constexpr std::uint8_t kTabEventMap     = 0x12;
constexpr std::uint8_t kTabEvent        = 0x14;
constexpr std::uint8_t kTabPropertyMap  = 0x15;
constexpr std::uint8_t kTabProperty     = 0x17;
constexpr std::uint8_t kTabMethodSemantics = 0x18;
constexpr std::uint8_t kTabMethodImpl   = 0x19;
constexpr std::uint8_t kTabModuleRef    = 0x1A;
constexpr std::uint8_t kTabTypeSpec     = 0x1B;
constexpr std::uint8_t kTabImplMap      = 0x1C;
constexpr std::uint8_t kTabFieldRVA     = 0x1D;
constexpr std::uint8_t kTabAssembly     = 0x20;
constexpr std::uint8_t kTabAssemblyProc = 0x21;
constexpr std::uint8_t kTabAssemblyOS   = 0x22;
constexpr std::uint8_t kTabAssemblyRef  = 0x23;
constexpr std::uint8_t kTabAssemblyRefProc = 0x24;
constexpr std::uint8_t kTabAssemblyRefOS   = 0x25;
constexpr std::uint8_t kTabFile         = 0x26;
constexpr std::uint8_t kTabExportedType = 0x27;
constexpr std::uint8_t kTabManifestRes  = 0x28;
constexpr std::uint8_t kTabNestedClass  = 0x29;
constexpr std::uint8_t kTabGenericParam = 0x2A;
constexpr std::uint8_t kTabMethodSpec   = 0x2B;
constexpr std::uint8_t kTabGenericParamConstraint = 0x2C;

struct StreamInfo { std::size_t offset{0}; std::size_t size{0}; };

struct MetadataInfo {
    const std::uint8_t *image_base{nullptr};
    std::size_t         image_size{0};
    StreamInfo tilde;     // #~ or #-
    StreamInfo strings;   // #Strings
    StreamInfo us;        // #US
    StreamInfo guid;      // #GUID
    StreamInfo blob;      // #Blob

    // Heap index sizes (in bytes — 2 or 4) from #~ heap_sizes byte.
    std::uint8_t string_idx_size{2};
    std::uint8_t guid_idx_size{2};
    std::uint8_t blob_idx_size{2};

    // Row counts indexed by table id (0..63).
    std::array<std::uint32_t, 64> rows{};

    // Helpers
    std::string ReadString(std::uint32_t idx) const {
        if (idx >= strings.size) return {};
        const char *p = reinterpret_cast<const char *>(image_base + strings.offset + idx);
        std::size_t maxn = strings.size - idx;
        std::size_t n = 0;
        while (n < maxn && p[n] != 0) ++n;
        return std::string(p, n);
    }

    // Read a #Blob entry.  Length is stored as a compressed integer per
    // ECMA-335 §II.23.2.
    std::vector<std::uint8_t> ReadBlob(std::uint32_t idx) const {
        if (idx >= blob.size) return {};
        const std::uint8_t *p = image_base + blob.offset + idx;
        std::size_t avail = blob.size - idx;
        std::uint32_t length = 0;
        std::size_t hdr = 0;
        if (avail < 1) return {};
        std::uint8_t b0 = p[0];
        if      ((b0 & 0x80) == 0)        { length = b0 & 0x7F;       hdr = 1; }
        else if ((b0 & 0xC0) == 0x80) {
            if (avail < 2) return {};
            length = (std::uint32_t(b0 & 0x3F) << 8) | p[1];          hdr = 2;
        } else if ((b0 & 0xE0) == 0xC0) {
            if (avail < 4) return {};
            length = (std::uint32_t(b0 & 0x1F) << 24) |
                     (std::uint32_t(p[1]) << 16) |
                     (std::uint32_t(p[2]) << 8) | p[3];                 hdr = 4;
        } else {
            return {};
        }
        if (hdr + length > avail) return {};
        return std::vector<std::uint8_t>(p + hdr, p + hdr + length);
    }
};

// Coded-index decode tables: list of source-tables for each coded-index kind
// and the number of tag bits.
struct CodedIndexInfo {
    std::vector<std::uint8_t> tables;  // table ids, indexed by tag value
    std::uint8_t              tag_bits{0};
};

// Per ECMA-335 §II.24.2.6
const CodedIndexInfo &CI_TypeDefOrRef() {
    static const CodedIndexInfo v{
        {kTabTypeDef, kTabTypeRef, kTabTypeSpec}, 2};
    return v;
}
const CodedIndexInfo &CI_HasConstant() {
    static const CodedIndexInfo v{{kTabField, kTabParam, kTabProperty}, 2};
    return v;
}
const CodedIndexInfo &CI_HasCustomAttribute() {
    static const CodedIndexInfo v{
        {kTabMethodDef, kTabField, kTabTypeRef, kTabTypeDef, kTabParam,
         kTabInterfaceImpl, kTabMemberRef, kTabModule, kTabDeclSecurity,
         kTabProperty, kTabEvent, kTabStandAloneSig, kTabModuleRef,
         kTabTypeSpec, kTabAssembly, kTabAssemblyRef, kTabFile,
         kTabExportedType, kTabManifestRes, kTabGenericParam,
         kTabGenericParamConstraint, kTabMethodSpec},
        5};
    return v;
}
const CodedIndexInfo &CI_HasFieldMarshal() {
    static const CodedIndexInfo v{{kTabField, kTabParam}, 1};
    return v;
}
const CodedIndexInfo &CI_HasDeclSecurity() {
    static const CodedIndexInfo v{{kTabTypeDef, kTabMethodDef, kTabAssembly}, 2};
    return v;
}
const CodedIndexInfo &CI_MemberRefParent() {
    static const CodedIndexInfo v{
        {kTabTypeDef, kTabTypeRef, kTabModuleRef, kTabMethodDef, kTabTypeSpec}, 3};
    return v;
}
const CodedIndexInfo &CI_HasSemantics() {
    static const CodedIndexInfo v{{kTabEvent, kTabProperty}, 1};
    return v;
}
const CodedIndexInfo &CI_MethodDefOrRef() {
    static const CodedIndexInfo v{{kTabMethodDef, kTabMemberRef}, 1};
    return v;
}
const CodedIndexInfo &CI_MemberForwarded() {
    static const CodedIndexInfo v{{kTabField, kTabMethodDef}, 1};
    return v;
}
const CodedIndexInfo &CI_Implementation() {
    static const CodedIndexInfo v{{kTabFile, kTabAssemblyRef, kTabExportedType}, 2};
    return v;
}
const CodedIndexInfo &CI_CustomAttributeType() {
    // Per spec: 0=NotUsed, 1=NotUsed, 2=MethodDef, 3=MemberRef, 4=NotUsed
    static const CodedIndexInfo v{
        {0xFF, 0xFF, kTabMethodDef, kTabMemberRef, 0xFF}, 3};
    return v;
}
const CodedIndexInfo &CI_ResolutionScope() {
    static const CodedIndexInfo v{
        {kTabModule, kTabModuleRef, kTabAssemblyRef, kTabTypeRef}, 2};
    return v;
}
const CodedIndexInfo &CI_TypeOrMethodDef() {
    static const CodedIndexInfo v{{kTabTypeDef, kTabMethodDef}, 1};
    return v;
}

std::uint8_t SimpleIndexBytes(const MetadataInfo &md, std::uint8_t table) {
    return md.rows[table] >= (1u << 16) ? 4 : 2;
}

std::uint8_t CodedIndexBytes(const MetadataInfo &md, const CodedIndexInfo &ci) {
    // Threshold: max rows fits in (16 - tag_bits) bits → 2-byte index.
    std::uint32_t max_rows = 0;
    for (std::uint8_t t : ci.tables) {
        if (t == 0xFF) continue;
        max_rows = std::max(max_rows, md.rows[t]);
    }
    std::uint32_t threshold = 1u << (16 - ci.tag_bits);
    return max_rows < threshold ? 2 : 4;
}

// ============================================================================
// Decode metadata streams
// ============================================================================
bool LocateStreams(const std::uint8_t *base, std::size_t size,
                   std::size_t md_off, MetadataInfo &md,
                   const std::string &path, frontends::Diagnostics &diags) {
    md.image_base = base;
    md.image_size = size;
    LEReader r(base, size, md_off);
    if (r.u32() != 0x424A5342u) {  // "BSJB"
        diags.ReportError(LocOf(path), frontends::ErrorCode::kUnknown,
                          "metadata: bad BSJB signature: " + path);
        return false;
    }
    r.skip(2 + 2 + 4);                // major, minor, reserved
    std::uint32_t ver_len = r.u32();
    r.skip((ver_len + 3) & ~3u);      // version string, padded to 4 bytes
    r.skip(2);                        // flags
    std::uint16_t n_streams = r.u16();
    for (std::uint16_t i = 0; i < n_streams; ++i) {
        std::uint32_t off  = r.u32();
        std::uint32_t sz   = r.u32();
        std::string name = r.cstr();
        // Pad name field to 4-byte boundary.
        std::size_t name_with_nul = name.size() + 1;
        std::size_t pad = (4 - (name_with_nul & 3)) & 3;
        r.skip(pad);
        StreamInfo si{md_off + off, sz};
        if      (name == "#~"      || name == "#-") md.tilde = si;
        else if (name == "#Strings")                 md.strings = si;
        else if (name == "#US")                      md.us = si;
        else if (name == "#GUID")                    md.guid = si;
        else if (name == "#Blob")                    md.blob = si;
    }
    if (md.tilde.size == 0 || md.strings.size == 0 || md.blob.size == 0) {
        diags.ReportError(LocOf(path), frontends::ErrorCode::kUnknown,
                          "metadata: missing required streams: " + path);
        return false;
    }
    return r.ok();
}

bool DecodeTildeHeader(MetadataInfo &md, const std::string &path,
                       frontends::Diagnostics &diags) {
    LEReader r(md.image_base, md.image_size, md.tilde.offset);
    r.skip(4);                       // reserved
    r.skip(2);                       // major+minor version
    std::uint8_t heap_sizes = r.u8();
    r.skip(1);                       // reserved
    std::uint64_t valid  = r.u64();
    /*sorted*/ r.u64();
    md.string_idx_size = (heap_sizes & 0x01) ? 4 : 2;
    md.guid_idx_size   = (heap_sizes & 0x02) ? 4 : 2;
    md.blob_idx_size   = (heap_sizes & 0x04) ? 4 : 2;

    // Row counts: one u32 per set bit.
    md.rows.fill(0);
    for (int i = 0; i < 64; ++i) {
        if ((valid >> i) & 1u) md.rows[i] = r.u32();
    }
    (void)diags; (void)path;
    return r.ok();
}

// Compute row sizes for tables we care about (and skip-size for the rest).
struct ColumnSizes {
    std::uint8_t string_idx;
    std::uint8_t guid_idx;
    std::uint8_t blob_idx;
    // simple table indices
    std::array<std::uint8_t, 64> simple;
    // coded indices, indexed by an enum we define inline
    std::uint8_t TypeDefOrRef;
    std::uint8_t HasConstant;
    std::uint8_t HasCustomAttribute;
    std::uint8_t HasFieldMarshal;
    std::uint8_t HasDeclSecurity;
    std::uint8_t MemberRefParent;
    std::uint8_t HasSemantics;
    std::uint8_t MethodDefOrRef;
    std::uint8_t MemberForwarded;
    std::uint8_t Implementation;
    std::uint8_t CustomAttributeType;
    std::uint8_t ResolutionScope;
    std::uint8_t TypeOrMethodDef;
};

ColumnSizes ComputeColumnSizes(const MetadataInfo &md) {
    ColumnSizes c{};
    c.string_idx = md.string_idx_size;
    c.guid_idx   = md.guid_idx_size;
    c.blob_idx   = md.blob_idx_size;
    for (int i = 0; i < 64; ++i) c.simple[i] = SimpleIndexBytes(md, std::uint8_t(i));
    c.TypeDefOrRef        = CodedIndexBytes(md, CI_TypeDefOrRef());
    c.HasConstant         = CodedIndexBytes(md, CI_HasConstant());
    c.HasCustomAttribute  = CodedIndexBytes(md, CI_HasCustomAttribute());
    c.HasFieldMarshal     = CodedIndexBytes(md, CI_HasFieldMarshal());
    c.HasDeclSecurity     = CodedIndexBytes(md, CI_HasDeclSecurity());
    c.MemberRefParent     = CodedIndexBytes(md, CI_MemberRefParent());
    c.HasSemantics        = CodedIndexBytes(md, CI_HasSemantics());
    c.MethodDefOrRef      = CodedIndexBytes(md, CI_MethodDefOrRef());
    c.MemberForwarded     = CodedIndexBytes(md, CI_MemberForwarded());
    c.Implementation      = CodedIndexBytes(md, CI_Implementation());
    c.CustomAttributeType = CodedIndexBytes(md, CI_CustomAttributeType());
    c.ResolutionScope     = CodedIndexBytes(md, CI_ResolutionScope());
    c.TypeOrMethodDef     = CodedIndexBytes(md, CI_TypeOrMethodDef());
    return c;
}

// Row sizes per ECMA-335 §II.22 for every table we know about.
// For tables we do not decode we still need the size to skip.
std::size_t RowSize(std::uint8_t tab, const ColumnSizes &c) {
    switch (tab) {
    case kTabModule:        return 2 + c.string_idx + 3 * c.guid_idx;
    case kTabTypeRef:       return c.ResolutionScope + 2 * c.string_idx;
    case kTabTypeDef:       return 4 + 2 * c.string_idx + c.TypeDefOrRef +
                                   c.simple[kTabField] + c.simple[kTabMethodDef];
    case kTabField:         return 2 + c.string_idx + c.blob_idx;
    case kTabMethodDef:     return 4 + 2 + 2 + c.string_idx + c.blob_idx + c.simple[kTabParam];
    case kTabParam:         return 2 + 2 + c.string_idx;
    case kTabInterfaceImpl: return c.simple[kTabTypeDef] + c.TypeDefOrRef;
    case kTabMemberRef:     return c.MemberRefParent + c.string_idx + c.blob_idx;
    case kTabConstant:      return 2 + c.HasConstant + c.blob_idx;
    case kTabCustomAttr:    return c.HasCustomAttribute + c.CustomAttributeType + c.blob_idx;
    case kTabFieldMarshal:  return c.HasFieldMarshal + c.blob_idx;
    case kTabDeclSecurity:  return 2 + c.HasDeclSecurity + c.blob_idx;
    case kTabClassLayout:   return 2 + 4 + c.simple[kTabTypeDef];
    case kTabFieldLayout:   return 4 + c.simple[kTabField];
    case kTabStandAloneSig: return c.blob_idx;
    case kTabEventMap:      return c.simple[kTabTypeDef] + c.simple[kTabEvent];
    case kTabEvent:         return 2 + c.string_idx + c.TypeDefOrRef;
    case kTabPropertyMap:   return c.simple[kTabTypeDef] + c.simple[kTabProperty];
    case kTabProperty:      return 2 + c.string_idx + c.blob_idx;
    case kTabMethodSemantics: return 2 + c.simple[kTabMethodDef] + c.HasSemantics;
    case kTabMethodImpl:    return c.simple[kTabTypeDef] + 2 * c.MethodDefOrRef;
    case kTabModuleRef:     return c.string_idx;
    case kTabTypeSpec:      return c.blob_idx;
    case kTabImplMap:       return 2 + c.MemberForwarded + c.string_idx + c.simple[kTabModuleRef];
    case kTabFieldRVA:      return 4 + c.simple[kTabField];
    case kTabAssembly:      return 4 + 4*2 + 4 + c.blob_idx + 2 * c.string_idx;
    case kTabAssemblyProc:  return 4;
    case kTabAssemblyOS:    return 12;
    case kTabAssemblyRef:   return 4*2 + 4 + 2 * c.blob_idx + 2 * c.string_idx;
    case kTabAssemblyRefProc: return 4 + c.simple[kTabAssemblyRef];
    case kTabAssemblyRefOS: return 12 + c.simple[kTabAssemblyRef];
    case kTabFile:          return 4 + c.string_idx + c.blob_idx;
    case kTabExportedType:  return 4 + 4 + 2 * c.string_idx + c.Implementation;
    case kTabManifestRes:   return 4 + 4 + c.string_idx + c.Implementation;
    case kTabNestedClass:   return 2 * c.simple[kTabTypeDef];
    case kTabGenericParam:  return 2 + 2 + c.TypeOrMethodDef + c.string_idx;
    case kTabMethodSpec:    return c.MethodDefOrRef + c.blob_idx;
    case kTabGenericParamConstraint: return c.simple[kTabGenericParam] + c.TypeDefOrRef;
    default: return 0;
    }
}

// MetadataInfo extension — we tuck the first-row offset into a member.
}  // namespace
}  // namespace polyglot::dotnet

namespace polyglot::dotnet {
namespace {
// Compute the byte size of the #~ stream header up to (but not including)
// the first row of the first present table.
std::size_t TildeHeaderSize(const MetadataInfo &md) {
    LEReader r(md.image_base, md.image_size, md.tilde.offset);
    r.skip(4);
    r.skip(2);
    r.u8();
    r.u8();
    std::uint64_t valid = r.u64();
    r.u64();
    int n = 0;
    for (int i = 0; i < 64; ++i) if ((valid >> i) & 1u) ++n;
    return (r.tell() - md.tilde.offset) + std::size_t(n) * 4u;
}
}
}  // namespace polyglot::dotnet

namespace polyglot::dotnet {
namespace {

// We use this rather than the diagnostics-emitting DecodeTildeHeader so that
// failures are reported by the higher-level parser using a single error site.
bool DecodeTildeHeader2(MetadataInfo &md) {
    LEReader r(md.image_base, md.image_size, md.tilde.offset);
    r.skip(4);
    r.skip(2);
    std::uint8_t heap_sizes = r.u8();
    r.skip(1);
    std::uint64_t valid = r.u64();
    r.u64();
    md.string_idx_size = (heap_sizes & 0x01) ? 4 : 2;
    md.guid_idx_size   = (heap_sizes & 0x02) ? 4 : 2;
    md.blob_idx_size   = (heap_sizes & 0x04) ? 4 : 2;
    md.rows.fill(0);
    for (int i = 0; i < 64; ++i) {
        if ((valid >> i) & 1u) md.rows[i] = r.u32();
    }
    return r.ok();
}

// Helper: read a column of given byte width (2 or 4).
std::uint32_t ReadCol(LEReader &r, std::uint8_t width) {
    return width == 4 ? r.u32() : std::uint32_t(r.u16());
}

// Decode a coded index into (table, row).  row==0 means null.
std::pair<std::uint8_t, std::uint32_t>
DecodeCoded(std::uint32_t value, const CodedIndexInfo &ci) {
    std::uint32_t mask = (1u << ci.tag_bits) - 1u;
    std::uint32_t tag  = value & mask;
    std::uint32_t row  = value >> ci.tag_bits;
    if (tag >= ci.tables.size()) return {0xFF, 0};
    return {ci.tables[tag], row};
}

// Parse a TypeDefOrRef-style coded index into a fully-qualified type name.
std::string ResolveTypeRef(const MetadataInfo &md, std::uint8_t tab, std::uint32_t row,
                           const ColumnSizes &c, const std::array<std::size_t,64> &row_off);

// A helper that reads TypeDef row #row (1-based) and produces "ns.name".
std::string ReadTypeDefName(const MetadataInfo &md, std::uint32_t row,
                            const ColumnSizes &c,
                            const std::array<std::size_t,64> &row_off) {
    if (row == 0 || row > md.rows[kTabTypeDef]) return {};
    std::size_t off = row_off[kTabTypeDef] + (row - 1) * RowSize(kTabTypeDef, c);
    LEReader r(md.image_base, md.image_size, off);
    r.skip(4);                                    // flags
    std::uint32_t name_idx = ReadCol(r, c.string_idx);
    std::uint32_t ns_idx   = ReadCol(r, c.string_idx);
    std::string name = md.ReadString(name_idx);
    std::string ns   = md.ReadString(ns_idx);
    return ns.empty() ? name : ns + "." + name;
}

std::string ReadTypeRefName(const MetadataInfo &md, std::uint32_t row,
                            const ColumnSizes &c,
                            const std::array<std::size_t,64> &row_off) {
    if (row == 0 || row > md.rows[kTabTypeRef]) return {};
    std::size_t off = row_off[kTabTypeRef] + (row - 1) * RowSize(kTabTypeRef, c);
    LEReader r(md.image_base, md.image_size, off);
    /*resolution scope*/ ReadCol(r, c.ResolutionScope);
    std::uint32_t name_idx = ReadCol(r, c.string_idx);
    std::uint32_t ns_idx   = ReadCol(r, c.string_idx);
    std::string name = md.ReadString(name_idx);
    std::string ns   = md.ReadString(ns_idx);
    return ns.empty() ? name : ns + "." + name;
}

std::string ResolveTypeRef(const MetadataInfo &md, std::uint8_t tab, std::uint32_t row,
                           const ColumnSizes &c,
                           const std::array<std::size_t,64> &row_off) {
    if (row == 0) return {};
    if (tab == kTabTypeDef) return ReadTypeDefName(md, row, c, row_off);
    if (tab == kTabTypeRef) return ReadTypeRefName(md, row, c, row_off);
    return {};  // TypeSpec — generic instantiations skipped here.
}

// ============================================================================
// Signature blob decoder (ECMA-335 §II.23.2)
// ============================================================================
class SigReader {
  public:
    SigReader(const std::uint8_t *p, std::size_t n) : p_(p), end_(p + n) {}
    bool eof() const { return p_ >= end_; }
    bool fail() const { return fail_; }
    std::uint8_t peek() {
        if (p_ >= end_) { fail_ = true; return 0; }
        return *p_;
    }
    std::uint8_t u8() {
        if (p_ >= end_) { fail_ = true; return 0; }
        return *p_++;
    }
    // Compressed unsigned integer.
    std::uint32_t cu() {
        if (p_ >= end_) { fail_ = true; return 0; }
        std::uint8_t b = *p_++;
        if ((b & 0x80) == 0) return b;
        if ((b & 0xC0) == 0x80) {
            if (p_ >= end_) { fail_ = true; return 0; }
            std::uint32_t v = (std::uint32_t(b & 0x3F) << 8) | *p_++;
            return v;
        }
        if ((b & 0xE0) == 0xC0) {
            if (p_ + 3 > end_) { fail_ = true; return 0; }
            std::uint32_t v = (std::uint32_t(b & 0x1F) << 24) |
                              (std::uint32_t(p_[0]) << 16) |
                              (std::uint32_t(p_[1]) << 8) | p_[2];
            p_ += 3;
            return v;
        }
        fail_ = true;
        return 0;
    }
    // Compressed TypeDefOrRef coded token (§II.23.2.8).
    std::pair<std::uint8_t, std::uint32_t> typeDefOrRefToken() {
        std::uint32_t v = cu();
        std::uint32_t row = v >> 2;
        std::uint32_t tag = v & 0x3;
        std::uint8_t tab = 0xFF;
        if      (tag == 0) tab = kTabTypeDef;
        else if (tag == 1) tab = kTabTypeRef;
        else if (tag == 2) tab = kTabTypeSpec;
        return {tab, row};
    }
  private:
    const std::uint8_t *p_;
    const std::uint8_t *end_;
    bool fail_{false};
};

// ELEMENT_TYPE values
constexpr std::uint8_t ET_END     = 0x00;
constexpr std::uint8_t ET_VOID    = 0x01;
constexpr std::uint8_t ET_BOOLEAN = 0x02;
constexpr std::uint8_t ET_CHAR    = 0x03;
constexpr std::uint8_t ET_I1      = 0x04;
constexpr std::uint8_t ET_U1      = 0x05;
constexpr std::uint8_t ET_I2      = 0x06;
constexpr std::uint8_t ET_U2      = 0x07;
constexpr std::uint8_t ET_I4      = 0x08;
constexpr std::uint8_t ET_U4      = 0x09;
constexpr std::uint8_t ET_I8      = 0x0A;
constexpr std::uint8_t ET_U8      = 0x0B;
constexpr std::uint8_t ET_R4      = 0x0C;
constexpr std::uint8_t ET_R8      = 0x0D;
constexpr std::uint8_t ET_STRING  = 0x0E;
constexpr std::uint8_t ET_PTR     = 0x0F;
constexpr std::uint8_t ET_BYREF   = 0x10;
constexpr std::uint8_t ET_VALUETYPE = 0x11;
constexpr std::uint8_t ET_CLASS   = 0x12;
constexpr std::uint8_t ET_VAR     = 0x13;
constexpr std::uint8_t ET_ARRAY   = 0x14;
constexpr std::uint8_t ET_GENERICINST = 0x15;
constexpr std::uint8_t ET_TYPEDBYREF  = 0x16;
constexpr std::uint8_t ET_I       = 0x18;
constexpr std::uint8_t ET_U       = 0x19;
constexpr std::uint8_t ET_FNPTR   = 0x1B;
constexpr std::uint8_t ET_OBJECT  = 0x1C;
constexpr std::uint8_t ET_SZARRAY = 0x1D;
constexpr std::uint8_t ET_MVAR    = 0x1E;
constexpr std::uint8_t ET_CMOD_REQD = 0x1F;
constexpr std::uint8_t ET_CMOD_OPT  = 0x20;
constexpr std::uint8_t ET_PINNED  = 0x45;
constexpr std::uint8_t ET_SENTINEL = 0x41;

core::Type ParseSigType(SigReader &sr, const MetadataInfo &md,
                        const ColumnSizes &c,
                        const std::array<std::size_t,64> &row_off);

// Strip leading custom modifiers (CMOD_REQD/CMOD_OPT).
void SkipCMods(SigReader &sr) {
    while (!sr.eof() && !sr.fail()) {
        std::uint8_t b = sr.peek();
        if (b != ET_CMOD_REQD && b != ET_CMOD_OPT) break;
        sr.u8();
        sr.typeDefOrRefToken();
    }
}

core::Type ParseSigType(SigReader &sr, const MetadataInfo &md,
                        const ColumnSizes &c,
                        const std::array<std::size_t,64> &row_off) {
    SkipCMods(sr);
    if (sr.eof() || sr.fail()) return core::Type::Any();
    std::uint8_t et = sr.u8();
    switch (et) {
    case ET_VOID:    return core::Type::Void();
    case ET_BOOLEAN: return core::Type::Bool();
    case ET_CHAR:    return core::Type::Int(16, false);
    case ET_I1:      return core::Type::Int(8, true);
    case ET_U1:      return core::Type::Int(8, false);
    case ET_I2:      return core::Type::Int(16, true);
    case ET_U2:      return core::Type::Int(16, false);
    case ET_I4:      return core::Type::Int(32, true);
    case ET_U4:      return core::Type::Int(32, false);
    case ET_I8:      return core::Type::Int(64, true);
    case ET_U8:      return core::Type::Int(64, false);
    case ET_R4:      return core::Type::Float(32);
    case ET_R8:      return core::Type::Float(64);
    case ET_I:       return core::Type::Int(64, true);
    case ET_U:       return core::Type::Int(64, false);
    case ET_STRING:  return core::Type::String();
    case ET_OBJECT: {
        core::Type t; t.kind = core::TypeKind::kClass;
        t.name = "System.Object"; t.language = "csharp"; return t;
    }
    case ET_VALUETYPE:
    case ET_CLASS: {
        auto [tab, row] = sr.typeDefOrRefToken();
        std::string name = ResolveTypeRef(md, tab, row, c, row_off);
        if (name == "System.String") return core::Type::String();
        if (name == "System.Void")   return core::Type::Void();
        if (name == "System.Boolean") return core::Type::Bool();
        if (name == "System.Int32")  return core::Type::Int(32, true);
        if (name == "System.Int64")  return core::Type::Int(64, true);
        if (name == "System.Single") return core::Type::Float(32);
        if (name == "System.Double") return core::Type::Float(64);
        core::Type t;
        t.kind = (et == ET_VALUETYPE) ? core::TypeKind::kStruct : core::TypeKind::kClass;
        t.name = name.empty() ? "<unknown>" : name;
        t.language = "csharp";
        return t;
    }
    case ET_SZARRAY: {
        core::Type elem = ParseSigType(sr, md, c, row_off);
        return core::Type::Array(std::move(elem));
    }
    case ET_ARRAY: {
        core::Type elem = ParseSigType(sr, md, c, row_off);
        // rank, numSizes, sizes..., numLoBounds, loBounds...
        sr.cu();                              // rank
        std::uint32_t num_sizes = sr.cu();
        for (std::uint32_t i = 0; i < num_sizes; ++i) sr.cu();
        std::uint32_t num_lo = sr.cu();
        for (std::uint32_t i = 0; i < num_lo; ++i) sr.cu();
        return core::Type::Array(std::move(elem));
    }
    case ET_PTR: {
        core::Type inner = ParseSigType(sr, md, c, row_off);
        core::Type t;
        t.kind = core::TypeKind::kPointer;
        t.name = "ptr";
        t.type_args.push_back(std::move(inner));
        return t;
    }
    case ET_BYREF: {
        core::Type inner = ParseSigType(sr, md, c, row_off);
        core::Type t;
        t.kind = core::TypeKind::kReference;
        t.name = "ref";
        t.type_args.push_back(std::move(inner));
        return t;
    }
    case ET_GENERICINST: {
        // class | valuetype, TypeDefOrRef token, GenArgCount, args...
        sr.u8();  // class/valuetype tag
        auto [tab, row] = sr.typeDefOrRefToken();
        std::string name = ResolveTypeRef(md, tab, row, c, row_off);
        std::uint32_t argc = sr.cu();
        core::Type t;
        t.kind = core::TypeKind::kGenericInstance;
        t.name = name.empty() ? "<unknown>" : name;
        t.language = "csharp";
        for (std::uint32_t i = 0; i < argc; ++i) {
            t.type_args.push_back(ParseSigType(sr, md, c, row_off));
        }
        return t;
    }
    case ET_VAR:
    case ET_MVAR: {
        sr.cu();  // generic param number
        return core::Type::Any();
    }
    default:
        return core::Type::Any();
    }
}

// Decode a method signature blob (HASTHIS/EXPLICITTHIS/calling-convention,
// param count, return type, params...).
struct MethodSig {
    core::Type ret;
    std::vector<core::Type> params;
};

MethodSig ParseMethodSig(const std::vector<std::uint8_t> &blob,
                         const MetadataInfo &md, const ColumnSizes &c,
                         const std::array<std::size_t,64> &row_off) {
    MethodSig s;
    SigReader sr(blob.data(), blob.size());
    if (sr.eof()) return s;
    std::uint8_t hdr = sr.u8();
    bool generic = (hdr & 0x10) != 0;
    if (generic) sr.cu();                     // generic param count
    std::uint32_t param_count = sr.cu();
    s.ret = ParseSigType(sr, md, c, row_off);
    s.params.reserve(param_count);
    for (std::uint32_t i = 0; i < param_count && !sr.eof() && !sr.fail(); ++i) {
        if (sr.peek() == ET_SENTINEL) sr.u8();
        s.params.push_back(ParseSigType(sr, md, c, row_off));
    }
    return s;
}

// Decode a field signature blob.  Header byte = 0x06, then Type.
core::Type ParseFieldSig(const std::vector<std::uint8_t> &blob,
                         const MetadataInfo &md, const ColumnSizes &c,
                         const std::array<std::size_t,64> &row_off) {
    SigReader sr(blob.data(), blob.size());
    if (sr.eof()) return core::Type::Any();
    sr.u8();                                  // header (0x06)
    SkipCMods(sr);
    return ParseSigType(sr, md, c, row_off);
}

// ============================================================================
// Top-level table decoder
// ============================================================================
bool ParseAssembly(const std::vector<std::uint8_t> &bytes,
                   const PEInfo &pe, std::size_t md_off,
                   CliAssembly &out, const std::string &path,
                   frontends::Diagnostics &diags) {
    MetadataInfo md;
    if (!LocateStreams(bytes.data(), bytes.size(), md_off, md, path, diags)) return false;
    if (!DecodeTildeHeader2(md)) {
        diags.ReportError(LocOf(path), frontends::ErrorCode::kUnknown,
                          "metadata: malformed #~ header: " + path);
        return false;
    }
    ColumnSizes c = ComputeColumnSizes(md);

    // Compute row offsets for every present table.
    std::array<std::size_t, 64> row_off{};
    std::size_t cursor = md.tilde.offset + TildeHeaderSize(md);
    for (int t = 0; t < 64; ++t) {
        if (md.rows[t] == 0) continue;
        row_off[t] = cursor;
        cursor += std::size_t(md.rows[t]) * RowSize(std::uint8_t(t), c);
    }

    // 1) Assembly name (single-row table).
    if (md.rows[kTabAssembly] >= 1) {
        std::size_t off = row_off[kTabAssembly];
        LEReader r(bytes.data(), bytes.size(), off);
        r.skip(4);                            // hash alg
        r.skip(4 * 2);                        // major/minor/build/revision
        r.skip(4);                            // flags
        ReadCol(r, c.blob_idx);               // public key
        std::uint32_t name_idx = ReadCol(r, c.string_idx);
        out.name = md.ReadString(name_idx);
    }

    // 2) AssemblyRef rows.
    for (std::uint32_t i = 0; i < md.rows[kTabAssemblyRef]; ++i) {
        std::size_t off = row_off[kTabAssemblyRef] + std::size_t(i) * RowSize(kTabAssemblyRef, c);
        LEReader r(bytes.data(), bytes.size(), off);
        CliAssemblyRef ref;
        ref.major    = r.u16();
        ref.minor    = r.u16();
        ref.build    = r.u16();
        ref.revision = r.u16();
        r.skip(4);                            // flags
        ReadCol(r, c.blob_idx);               // public key or token
        std::uint32_t name_idx    = ReadCol(r, c.string_idx);
        std::uint32_t culture_idx = ReadCol(r, c.string_idx);
        ReadCol(r, c.blob_idx);               // hash value
        ref.name    = md.ReadString(name_idx);
        ref.culture = md.ReadString(culture_idx);
        out.references.push_back(std::move(ref));
    }

    // 3) TypeDef rows.  TypeDef[i] owns Field rows from FieldList[i] up to
    //    FieldList[i+1]-1, similarly for MethodDef.  TypeDef row 1 is always
    //    <Module> per ECMA-335; we skip it.
    out.types.reserve(md.rows[kTabTypeDef]);

    auto read_typedef = [&](std::uint32_t i, std::uint32_t &flags,
                            std::string &ns, std::string &name,
                            std::uint32_t &field_list, std::uint32_t &method_list) {
        std::size_t off = row_off[kTabTypeDef] + std::size_t(i) * RowSize(kTabTypeDef, c);
        LEReader r(bytes.data(), bytes.size(), off);
        flags         = r.u32();
        std::uint32_t name_idx = ReadCol(r, c.string_idx);
        std::uint32_t ns_idx   = ReadCol(r, c.string_idx);
        ReadCol(r, c.TypeDefOrRef);            // extends
        field_list    = ReadCol(r, c.simple[kTabField]);
        method_list   = ReadCol(r, c.simple[kTabMethodDef]);
        name = md.ReadString(name_idx);
        ns   = md.ReadString(ns_idx);
    };

    auto read_method_row = [&](std::uint32_t i, CliMethodMeta &m) {
        std::size_t off = row_off[kTabMethodDef] + std::size_t(i) * RowSize(kTabMethodDef, c);
        LEReader r(bytes.data(), bytes.size(), off);
        r.skip(4);                            // RVA
        m.impl_flags = r.u16();
        m.flags      = r.u16();
        std::uint32_t name_idx = ReadCol(r, c.string_idx);
        std::uint32_t sig_idx  = ReadCol(r, c.blob_idx);
        ReadCol(r, c.simple[kTabParam]);       // ParamList
        m.name = md.ReadString(name_idx);
        auto blob = md.ReadBlob(sig_idx);
        MethodSig sig = ParseMethodSig(blob, md, c, row_off);
        m.return_type = std::move(sig.ret);
        m.param_types = std::move(sig.params);
    };

    auto read_field_row = [&](std::uint32_t i, CliFieldMeta &f) {
        std::size_t off = row_off[kTabField] + std::size_t(i) * RowSize(kTabField, c);
        LEReader r(bytes.data(), bytes.size(), off);
        f.flags = r.u16();
        std::uint32_t name_idx = ReadCol(r, c.string_idx);
        std::uint32_t sig_idx  = ReadCol(r, c.blob_idx);
        f.name = md.ReadString(name_idx);
        auto blob = md.ReadBlob(sig_idx);
        f.type = ParseFieldSig(blob, md, c, row_off);
    };

    std::uint32_t typedef_count = md.rows[kTabTypeDef];
    std::uint32_t field_total   = md.rows[kTabField];
    std::uint32_t method_total  = md.rows[kTabMethodDef];

    for (std::uint32_t i = 0; i < typedef_count; ++i) {
        std::uint32_t flags, field_list, method_list;
        std::string ns, name;
        read_typedef(i, flags, ns, name, field_list, method_list);

        // Determine the next type's lists to bound this type's ranges.
        std::uint32_t next_field_list  = (i + 1 < typedef_count)
            ? [&]() {
                std::uint32_t f, fl, ml; std::string n2, ns2;
                read_typedef(i + 1, f, ns2, n2, fl, ml);
                return fl;
              }()
            : field_total + 1;
        std::uint32_t next_method_list = (i + 1 < typedef_count)
            ? [&]() {
                std::uint32_t f, fl, ml; std::string n2, ns2;
                read_typedef(i + 1, f, ns2, n2, fl, ml);
                return ml;
              }()
            : method_total + 1;

        // Skip the synthetic <Module> type at row 1.
        if (i == 0 && name == "<Module>") continue;

        CliTypeMeta t;
        t.flags = flags;
        t.name = name;
        t.ns   = ns;
        t.full_name = ns.empty() ? name : ns + "." + name;
        for (std::uint32_t fi = field_list; fi < next_field_list && fi >= 1 && fi <= field_total; ++fi) {
            CliFieldMeta f;
            read_field_row(fi - 1, f);
            t.fields.push_back(std::move(f));
        }
        for (std::uint32_t mi = method_list; mi < next_method_list && mi >= 1 && mi <= method_total; ++mi) {
            CliMethodMeta m;
            read_method_row(mi - 1, m);
            t.methods.push_back(std::move(m));
        }
        out.types.push_back(std::move(t));
    }

    // Build namespace index.
    for (std::size_t i = 0; i < out.types.size(); ++i) {
        out.ns_index[out.types[i].ns].push_back(i);
    }
    (void)pe;
    return true;
}

}  // namespace
}  // namespace polyglot::dotnet

namespace polyglot::dotnet {

std::optional<CliAssembly>
ReadAssemblyMetadata(const std::string &path, frontends::Diagnostics &diags) {
    std::vector<std::uint8_t> bytes;
    if (!LoadFile(path, bytes)) {
        diags.ReportError(LocOf(path), frontends::ErrorCode::kUnknown,
                          "cannot open assembly: " + path);
        return std::nullopt;
    }
    PEInfo pe;
    if (!ReadPE(bytes, pe, path, diags)) return std::nullopt;
    auto cli_off = RvaToOffset(pe, pe.cli_rva);
    if (!cli_off) {
        diags.ReportError(LocOf(path), frontends::ErrorCode::kUnknown,
                          "PE: CLI header RVA not in any section: " + path);
        return std::nullopt;
    }
    LEReader r(bytes.data(), bytes.size(), *cli_off);
    r.u32();  // cb
    r.u16(); r.u16();  // runtime versions
    std::uint32_t md_rva  = r.u32();
    std::uint32_t md_size = r.u32();
    (void)md_size;
    auto md_off = RvaToOffset(pe, md_rva);
    if (!md_off) {
        diags.ReportError(LocOf(path), frontends::ErrorCode::kUnknown,
                          "PE: metadata RVA not in any section: " + path);
        return std::nullopt;
    }
    CliAssembly out;
    out.path = path;
    if (!ParseAssembly(bytes, pe, *md_off, out, path, diags)) return std::nullopt;
    return out;
}

// ============================================================================
// AssemblyLoader
// ============================================================================
AssemblyLoader::AssemblyLoader(const std::vector<std::string> &references,
                               frontends::Diagnostics &diags) {
    assemblies_.reserve(references.size());
    for (const auto &p : references) {
        auto a = ReadAssemblyMetadata(p, diags);
        if (a) assemblies_.push_back(std::move(*a));
    }
}

std::vector<const CliTypeMeta *>
AssemblyLoader::ListNamespace(const std::string &namespace_name) const {
    std::vector<const CliTypeMeta *> out;
    for (const auto &asm_ : assemblies_) {
        auto it = asm_.ns_index.find(namespace_name);
        if (it == asm_.ns_index.end()) continue;
        for (std::size_t idx : it->second) {
            out.push_back(&asm_.types[idx]);
        }
    }
    return out;
}

const CliTypeMeta *
AssemblyLoader::ResolveType(const std::string &full_name) const {
    for (const auto &asm_ : assemblies_) {
        for (const auto &t : asm_.types) {
            if (t.full_name == full_name) return &t;
        }
    }
    return nullptr;
}

}  // namespace polyglot::dotnet
