/**
 * @file     pe_writer.cpp
 * @brief    PE32+ executable image writer
 *
 * @ingroup  Tool / polyld
 * @author   Manning Cyrus
 * @date     2026-04-28
 */

#include "tools/polyld/include/pe_writer.h"

#include <algorithm>
#include <cstring>

namespace polyglot::linker::pe {

// ===========================================================================
// PE32+ structural constants
//
// All numeric literals below are taken verbatim from the Microsoft PE/COFF
// specification (Revision 12, current as of 2026).  They are spelled out as
// named constants instead of being inlined at the use sites so that anyone
// reading the writer can cross-check them against the spec table-by-table.
// ===========================================================================

namespace {

// Section / file alignment.  We pick the canonical pair used by every modern
// Windows linker: 0x200 file alignment, 0x1000 section alignment.  This keeps
// section boundaries page-aligned in memory and 512-byte aligned on disk.
constexpr std::uint32_t kFileAlignment = 0x200;
constexpr std::uint32_t kSectionAlignment = 0x1000;

// Conventional first-section RVA (== section alignment).
constexpr std::uint32_t kTextRVA = 0x1000;

// COFF File Header machine value for AMD64.
constexpr std::uint16_t kImageFileMachineAmd64 = 0x8664;

// COFF Characteristics flags (relevant subset).
constexpr std::uint16_t kImageFileRelocsStripped = 0x0001;
constexpr std::uint16_t kImageFileExecutableImage = 0x0002;
constexpr std::uint16_t kImageFileLargeAddressAware = 0x0020;

// DllCharacteristics flags (relevant subset).
constexpr std::uint16_t kImageDllCharNxCompat = 0x0100;
constexpr std::uint16_t kImageDllCharTerminalServerAware = 0x8000;

// PE Optional Header magic for PE32+.
constexpr std::uint16_t kImageNtOptionalHdr64Magic = 0x020B;

// Subsystem: Windows console.
constexpr std::uint16_t kImageSubsystemWindowsCui = 3;

// Section header characteristics.
constexpr std::uint32_t kImageScnCntCode = 0x00000020;
constexpr std::uint32_t kImageScnCntInitializedData = 0x00000040;
constexpr std::uint32_t kImageScnMemExecute = 0x20000000;
constexpr std::uint32_t kImageScnMemRead = 0x40000000;
constexpr std::uint32_t kImageScnMemWrite = 0x80000000;

// Number of Optional Header data directories (the standard 16).
constexpr std::uint32_t kNumberOfDataDirectories = 16;

// Index of the Import Directory inside the data-directory array.
constexpr std::uint32_t kImportDirectoryIndex = 1;
// Index of the Import Address Table directory.
constexpr std::uint32_t kIATDirectoryIndex = 12;

// Sizes of the fixed-shape PE structures we manually lay out.
constexpr std::uint32_t kDosHeaderSize = 64;       // IMAGE_DOS_HEADER
constexpr std::uint32_t kDosStubSize = 64;         // 64-byte conventional stub
constexpr std::uint32_t kPeSignatureSize = 4;      // "PE\0\0"
constexpr std::uint32_t kCoffFileHeaderSize = 20;  // IMAGE_FILE_HEADER
constexpr std::uint32_t kOptionalHeader64Size = 240; // PE32+ Optional Header
constexpr std::uint32_t kSectionHeaderSize = 40;   // IMAGE_SECTION_HEADER
// Size of one IMAGE_IMPORT_DESCRIPTOR (5x uint32).
constexpr std::uint32_t kImportDescriptorSize = 20;

// ===========================================================================
// Little-endian byte-buffer helpers
// ===========================================================================

void Append8(std::vector<std::uint8_t> &out, std::uint8_t v) { out.push_back(v); }

void Append16(std::vector<std::uint8_t> &out, std::uint16_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

void Append32(std::vector<std::uint8_t> &out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

void Append64(std::vector<std::uint8_t> &out, std::uint64_t v) {
  for (int i = 0; i < 8; ++i)
    out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
}

void AppendBytes(std::vector<std::uint8_t> &out, const std::uint8_t *p, std::size_t n) {
  out.insert(out.end(), p, p + n);
}

void AppendCString(std::vector<std::uint8_t> &out, const std::string &s) {
  AppendBytes(out, reinterpret_cast<const std::uint8_t *>(s.data()), s.size());
  out.push_back(0);
}

void PadTo(std::vector<std::uint8_t> &out, std::size_t target) {
  if (out.size() < target)
    out.resize(target, 0);
}

void PadAlign(std::vector<std::uint8_t> &out, std::uint32_t align) {
  const std::size_t over = out.size() % align;
  if (over != 0)
    out.resize(out.size() + (align - over), 0);
}

void Patch32(std::vector<std::uint8_t> &out, std::size_t offset, std::uint32_t value) {
  out[offset + 0] = static_cast<std::uint8_t>(value & 0xFF);
  out[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
  out[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
  out[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

constexpr std::uint32_t AlignUp(std::uint32_t value, std::uint32_t align) {
  return (value + align - 1) & ~(align - 1);
}

// ===========================================================================
// DOS header + stub
//
// The 64-byte stub here is the canonical "This program cannot be run in DOS
// mode." stub that Microsoft tools have emitted since the early 1990s.  Any
// modern Windows loader ignores it; we keep the conventional bytes so that
// disassemblers / hex viewers recognise the file shape immediately.
// ===========================================================================

void EmitDosHeaderAndStub(std::vector<std::uint8_t> &out, std::uint32_t lfanew) {
  // IMAGE_DOS_HEADER (64 bytes).  Only e_magic ('MZ') and e_lfanew (the file
  // offset of the PE signature) are read by the Windows loader; the rest is
  // historical padding kept at zero except for a few fields tools expect.
  Append16(out, 0x5A4D); // e_magic 'MZ'
  Append16(out, 0x0090); // e_cblp
  Append16(out, 0x0003); // e_cp
  Append16(out, 0x0000); // e_crlc
  Append16(out, 0x0004); // e_cparhdr
  Append16(out, 0x0000); // e_minalloc
  Append16(out, 0xFFFF); // e_maxalloc
  Append16(out, 0x0000); // e_ss
  Append16(out, 0x00B8); // e_sp
  Append16(out, 0x0000); // e_csum
  Append16(out, 0x0000); // e_ip
  Append16(out, 0x0000); // e_cs
  Append16(out, 0x0040); // e_lfarlc
  Append16(out, 0x0000); // e_ovno
  for (int i = 0; i < 4; ++i)
    Append16(out, 0); // e_res[4]
  Append16(out, 0); // e_oemid
  Append16(out, 0); // e_oeminfo
  for (int i = 0; i < 10; ++i)
    Append16(out, 0); // e_res2[10]
  Append32(out, lfanew); // e_lfanew

  // Conventional 64-byte DOS stub, byte-identical to MSVC's emission, which
  // prints "This program cannot be run in DOS mode." then exits.
  static constexpr std::uint8_t kDosStub[kDosStubSize] = {
      0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD, 0x21, 0xB8, 0x01, 0x4C, 0xCD, 0x21,
      'T',  'h',  'i',  's',  ' ',  'p',  'r',  'o',  'g',  'r',  'a',  'm',  ' ',  'c',
      'a',  'n',  'n',  'o',  't',  ' ',  'b',  'e',  ' ',  'r',  'u',  'n',  ' ',  'i',
      'n',  ' ',  'D',  'O',  'S',  ' ',  'm',  'o',  'd',  'e',  '.',  '\r', '\r', '\n',
      '$',  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };
  AppendBytes(out, kDosStub, kDosStubSize);
}

// ===========================================================================
// Import directory layout
//
// Layout produced (all inside the .idata section, contiguous):
//
//   [Import Descriptor for DLL_0]
//   [Import Descriptor for DLL_1]
//   ...
//   [Null Import Descriptor terminator]
//   [ILT (Import Lookup Table) for DLL_0  -- one uint64 per fn + null term]
//   [ILT for DLL_1]
//   ...
//   [IAT (Import Address Table) for DLL_0 -- same shape; loader patches]
//   [IAT for DLL_1]
//   ...
//   [Hint/Name table -- one IMAGE_IMPORT_BY_NAME per function]
//   [DLL name strings -- ASCIZ, one per DLL]
//
// On disk both ILT and IAT carry the same RVAs into the Hint/Name table; at
// runtime the loader rewrites the IAT entries with the resolved function
// addresses, leaving the ILT untouched.  Code that wants to invoke an
// imported function dereferences the IAT slot, so we expose its RVA back to
// the caller via `BuildResult::iat_slot_rva`.
// ===========================================================================

struct ImportLayout {
  // Bytes that should be written into the .idata section, contiguous and
  // self-relocated (RVAs already point at their final positions, computed
  // from the caller-provided idata_rva base).
  std::vector<std::uint8_t> bytes;
  // RVA (relative to image base) of the start of the Import Directory
  // (i.e. the array of IMAGE_IMPORT_DESCRIPTOR records).
  std::uint32_t import_directory_rva{0};
  // Total size in bytes of the Import Directory array (descriptor count
  // including the null terminator).
  std::uint32_t import_directory_size{0};
  // RVA of the start of the IAT region (the merged region across all DLLs).
  std::uint32_t iat_rva{0};
  // Total size of the IAT region in bytes.
  std::uint32_t iat_size{0};
  // Per-function IAT slot RVA, in the same enumeration order as
  // BuildRequest::imports[i].function_names[j].
  std::vector<std::pair<std::string, std::uint32_t>> iat_slot_rva;
};

ImportLayout BuildImportSection(const std::vector<DllImport> &imports,
                                std::uint32_t idata_rva) {
  ImportLayout L;
  if (imports.empty()) {
    return L;
  }

  // 1) Compute sub-region offsets (relative to .idata base).
  const std::uint32_t descriptor_count = static_cast<std::uint32_t>(imports.size()) + 1; // +null
  const std::uint32_t descriptors_off = 0;
  const std::uint32_t descriptors_size = descriptor_count * kImportDescriptorSize;

  // ILT and IAT regions: per-DLL one uint64 per function plus a null
  // terminator uint64.
  std::uint32_t ilt_total_qwords = 0;
  for (const auto &dll : imports)
    ilt_total_qwords += static_cast<std::uint32_t>(dll.function_names.size()) + 1;
  const std::uint32_t ilt_off = AlignUp(descriptors_off + descriptors_size, 8);
  const std::uint32_t ilt_size = ilt_total_qwords * 8;
  const std::uint32_t iat_off = AlignUp(ilt_off + ilt_size, 8);
  const std::uint32_t iat_size = ilt_total_qwords * 8;

  // Hint/Name table starts after IAT.  We pre-compute each function's
  // IMAGE_IMPORT_BY_NAME offset so we can plant it into ILT and IAT in pass 2.
  std::uint32_t hint_name_off = iat_off + iat_size;
  // Per-function: 2-byte hint + ASCIZ name.  Each entry must be 2-byte aligned.
  std::vector<std::uint32_t> per_fn_hint_name_off; // relative to .idata base
  per_fn_hint_name_off.reserve(ilt_total_qwords);
  std::uint32_t cursor = hint_name_off;
  for (const auto &dll : imports) {
    for (const auto &fn : dll.function_names) {
      cursor = AlignUp(cursor, 2);
      per_fn_hint_name_off.push_back(cursor);
      cursor += 2 + static_cast<std::uint32_t>(fn.size()) + 1; // hint + name + NUL
    }
  }

  // DLL names appended after the hint/name table.
  std::vector<std::uint32_t> per_dll_name_off;
  per_dll_name_off.reserve(imports.size());
  for (const auto &dll : imports) {
    per_dll_name_off.push_back(cursor);
    cursor += static_cast<std::uint32_t>(dll.dll_name.size()) + 1;
  }

  const std::uint32_t total_size = cursor;
  L.bytes.assign(total_size, 0);

  // 2) Emit IMAGE_IMPORT_DESCRIPTOR array.
  std::uint32_t cur_ilt_qword = 0; // running per-function index
  for (std::size_t i = 0; i < imports.size(); ++i) {
    const auto &dll = imports[i];
    const std::uint32_t desc_base = descriptors_off + static_cast<std::uint32_t>(i) * kImportDescriptorSize;
    const std::uint32_t this_ilt_off = ilt_off + cur_ilt_qword * 8;
    const std::uint32_t this_iat_off = iat_off + cur_ilt_qword * 8;

    // OriginalFirstThunk = RVA of this DLL's ILT.
    Patch32(L.bytes, desc_base + 0, idata_rva + this_ilt_off);
    // TimeDateStamp / ForwarderChain = 0.
    Patch32(L.bytes, desc_base + 4, 0);
    Patch32(L.bytes, desc_base + 8, 0);
    // Name = RVA of DLL name string.
    Patch32(L.bytes, desc_base + 12, idata_rva + per_dll_name_off[i]);
    // FirstThunk = RVA of this DLL's IAT.
    Patch32(L.bytes, desc_base + 16, idata_rva + this_iat_off);

    // Walk this DLL's functions -> emit ILT and IAT entries pointing at the
    // matching IMAGE_IMPORT_BY_NAME RVA (low 63 bits; ordinal flag = 0).
    for (std::size_t j = 0; j < dll.function_names.size(); ++j) {
      const std::uint32_t hn_rva = idata_rva + per_fn_hint_name_off[cur_ilt_qword];
      const std::uint64_t qword = static_cast<std::uint64_t>(hn_rva); // ordinal=0
      const std::uint32_t ilt_slot_off = ilt_off + cur_ilt_qword * 8;
      const std::uint32_t iat_slot_off = iat_off + cur_ilt_qword * 8;
      for (int b = 0; b < 8; ++b) {
        L.bytes[ilt_slot_off + b] = static_cast<std::uint8_t>((qword >> (b * 8)) & 0xFF);
        L.bytes[iat_slot_off + b] = static_cast<std::uint8_t>((qword >> (b * 8)) & 0xFF);
      }
      L.iat_slot_rva.emplace_back(dll.dll_name + "!" + dll.function_names[j],
                                  idata_rva + iat_slot_off);
      ++cur_ilt_qword;
    }
    // Null terminator qword for this DLL's ILT/IAT (already zero from init).
    ++cur_ilt_qword;
  }
  // Final null IMAGE_IMPORT_DESCRIPTOR is already zero from init.

  // 3) Emit IMAGE_IMPORT_BY_NAME records.
  cur_ilt_qword = 0;
  for (const auto &dll : imports) {
    for (const auto &fn : dll.function_names) {
      const std::uint32_t hn_off = per_fn_hint_name_off[cur_ilt_qword];
      // Hint = 0.
      L.bytes[hn_off + 0] = 0;
      L.bytes[hn_off + 1] = 0;
      std::memcpy(&L.bytes[hn_off + 2], fn.data(), fn.size());
      L.bytes[hn_off + 2 + fn.size()] = 0;
      ++cur_ilt_qword;
    }
  }

  // 4) Emit DLL name strings.
  for (std::size_t i = 0; i < imports.size(); ++i) {
    const std::uint32_t off = per_dll_name_off[i];
    std::memcpy(&L.bytes[off], imports[i].dll_name.data(), imports[i].dll_name.size());
    L.bytes[off + imports[i].dll_name.size()] = 0;
  }

  L.import_directory_rva = idata_rva + descriptors_off;
  L.import_directory_size = descriptors_size;
  L.iat_rva = idata_rva + iat_off;
  L.iat_size = iat_size;
  return L;
}

} // namespace

// ===========================================================================
// Public API
// ===========================================================================

std::vector<std::uint8_t> BuildExitProcessShim(std::uint32_t shim_rva,
                                               std::uint32_t exit_process_iat_rva) {
  // Windows x64 ABI: the OS hands control to the entry point with a
  // 16-byte aligned RSP and NO return address on the stack. Before any
  // CALL we must (a) reserve 32 bytes of "shadow space" for the callee
  // and (b) keep RSP 16-byte aligned across the call. `sub rsp, 0x28`
  // does both: 0x28 = 40 = 32 (shadow) + 8 (re-align after the implicit
  // 8-byte return address pushed by CALL).
  //
  // sub rsp, 0x28                     ; 48 83 EC 28
  // xor ecx, ecx                      ; 31 C9              (arg0 = 0)
  // call qword ptr [rip + disp32]     ; FF 15 disp32       (ExitProcess(0))
  // int3                              ; CC                 (unreachable)
  //
  // disp32 = exit_process_iat_rva - (rip_after_call_inst)
  //        = exit_process_iat_rva - (shim_rva + 4 + 2 + 6)
  std::vector<std::uint8_t> bytes;
  bytes.reserve(13);
  Append8(bytes, 0x48);
  Append8(bytes, 0x83);
  Append8(bytes, 0xEC);
  Append8(bytes, 0x28);
  Append8(bytes, 0x31);
  Append8(bytes, 0xC9);
  Append8(bytes, 0xFF);
  Append8(bytes, 0x15);
  const std::uint32_t rip_after_call = shim_rva + 4 + 2 + 6;
  const std::uint32_t disp32 = exit_process_iat_rva - rip_after_call;
  Append32(bytes, disp32);
  Append8(bytes, 0xCC);
  return bytes;
}

BuildResult BuildPE32PlusImage(const BuildRequest &req) {
  BuildResult R;

  if (req.text_bytes.empty())
    return R;
  if (req.entry_offset_in_text >= req.text_bytes.size())
    return R;

  // ---------------------------------------------------------------------
  // Section layout planning (RVA + file offsets).
  // ---------------------------------------------------------------------
  const bool has_rdata = !req.rdata_bytes.empty();
  const bool has_idata = !req.imports.empty();
  std::uint32_t num_sections = 1u; // .text
  if (has_rdata)
    ++num_sections;
  if (has_idata)
    ++num_sections;

  // Headers region (DOS hdr+stub + PE sig + COFF hdr + Optional Hdr +
  // section table) gets rounded up to file alignment to form
  // SizeOfHeaders.
  const std::uint32_t headers_unaligned = kDosHeaderSize + kDosStubSize + kPeSignatureSize +
                                          kCoffFileHeaderSize + kOptionalHeader64Size +
                                          num_sections * kSectionHeaderSize;
  const std::uint32_t size_of_headers = AlignUp(headers_unaligned, kFileAlignment);

  // .text section
  const std::uint32_t text_rva = kTextRVA;
  const std::uint32_t text_virtual_size = static_cast<std::uint32_t>(req.text_bytes.size());
  const std::uint32_t text_raw_size = AlignUp(text_virtual_size, kFileAlignment);
  const std::uint32_t text_raw_offset = size_of_headers;

  // .rdata section (only if rdata_bytes is non-empty).
  const std::uint32_t rdata_rva =
      has_rdata ? AlignUp(text_rva + text_virtual_size, kSectionAlignment) : 0u;
  const std::uint32_t rdata_virtual_size =
      has_rdata ? static_cast<std::uint32_t>(req.rdata_bytes.size()) : 0u;
  const std::uint32_t rdata_raw_size = has_rdata ? AlignUp(rdata_virtual_size, kFileAlignment) : 0u;
  const std::uint32_t rdata_raw_offset = has_rdata ? text_raw_offset + text_raw_size : 0u;

  // .idata section (only if there are imports).  Sits after .rdata when
  // both are present, otherwise immediately after .text.
  const std::uint32_t after_rdata_rva =
      has_rdata ? rdata_rva + rdata_virtual_size : text_rva + text_virtual_size;
  const std::uint32_t idata_rva =
      has_idata ? AlignUp(after_rdata_rva, kSectionAlignment) : 0u;
  ImportLayout import_layout =
      has_idata ? BuildImportSection(req.imports, idata_rva) : ImportLayout{};
  const std::uint32_t idata_virtual_size =
      static_cast<std::uint32_t>(import_layout.bytes.size());
  const std::uint32_t idata_raw_size = AlignUp(idata_virtual_size, kFileAlignment);
  const std::uint32_t idata_raw_offset =
      has_idata ? (has_rdata ? rdata_raw_offset + rdata_raw_size
                             : text_raw_offset + text_raw_size)
                : 0u;

  std::uint32_t end_rva = text_rva + text_virtual_size;
  if (has_rdata)
    end_rva = rdata_rva + rdata_virtual_size;
  if (has_idata)
    end_rva = idata_rva + idata_virtual_size;
  const std::uint32_t size_of_image = AlignUp(end_rva, kSectionAlignment);

  // ---------------------------------------------------------------------
  // Emit headers
  // ---------------------------------------------------------------------
  std::vector<std::uint8_t> &out = R.image;
  EmitDosHeaderAndStub(out, kDosHeaderSize + kDosStubSize); // e_lfanew = 0x80

  // PE signature "PE\0\0".
  Append8(out, 'P');
  Append8(out, 'E');
  Append8(out, 0);
  Append8(out, 0);

  // IMAGE_FILE_HEADER (COFF File Header).
  Append16(out, kImageFileMachineAmd64); // Machine
  Append16(out, static_cast<std::uint16_t>(num_sections));      // NumberOfSections
  Append32(out, 0);                                             // TimeDateStamp
  Append32(out, 0);                                             // PointerToSymbolTable
  Append32(out, 0);                                             // NumberOfSymbols
  Append16(out, static_cast<std::uint16_t>(kOptionalHeader64Size)); // SizeOfOptionalHeader
  Append16(out, kImageFileRelocsStripped | kImageFileExecutableImage | kImageFileLargeAddressAware); // Characteristics

  // IMAGE_OPTIONAL_HEADER64 (240 bytes total).
  Append16(out, kImageNtOptionalHdr64Magic); // Magic = 0x20B
  Append8(out, 14);                          // MajorLinkerVersion (any)
  Append8(out, 0);                           // MinorLinkerVersion
  Append32(out, text_raw_size);              // SizeOfCode
  Append32(out, rdata_raw_size + idata_raw_size); // SizeOfInitializedData
  Append32(out, 0);                          // SizeOfUninitializedData
  // AddressOfEntryPoint
  R.entry_rva = text_rva + req.entry_offset_in_text;
  Append32(out, R.entry_rva);
  Append32(out, text_rva); // BaseOfCode
  Append64(out, req.image_base); // ImageBase
  Append32(out, kSectionAlignment); // SectionAlignment
  Append32(out, kFileAlignment);    // FileAlignment
  Append16(out, 6); // MajorOperatingSystemVersion
  Append16(out, 0); // MinorOperatingSystemVersion
  Append16(out, 0); // MajorImageVersion
  Append16(out, 0); // MinorImageVersion
  Append16(out, 6); // MajorSubsystemVersion
  Append16(out, 0); // MinorSubsystemVersion
  Append32(out, 0); // Win32VersionValue (reserved, must be 0)
  Append32(out, size_of_image);   // SizeOfImage
  Append32(out, size_of_headers); // SizeOfHeaders
  Append32(out, 0);               // CheckSum (loader does not require)
  Append16(out, kImageSubsystemWindowsCui); // Subsystem
  // DllCharacteristics: NX_COMPAT + TerminalServerAware. We deliberately do
  // NOT set DYNAMIC_BASE because we have no .reloc section; the loader honors
  // the preferred ImageBase (we also set IMAGE_FILE_RELOCS_STRIPPED above so
  // the loader will not attempt to rebase this image).
  Append16(out, kImageDllCharNxCompat | kImageDllCharTerminalServerAware);
  Append64(out, 0x100000); // SizeOfStackReserve
  Append64(out, 0x1000);   // SizeOfStackCommit
  Append64(out, 0x100000); // SizeOfHeapReserve
  Append64(out, 0x1000);   // SizeOfHeapCommit
  Append32(out, 0);        // LoaderFlags (reserved)
  Append32(out, kNumberOfDataDirectories); // NumberOfRvaAndSizes

  // Data directories array (16 entries x 8 bytes = 128 bytes).
  for (std::uint32_t i = 0; i < kNumberOfDataDirectories; ++i) {
    if (i == kImportDirectoryIndex) {
      Append32(out, import_layout.import_directory_rva);
      Append32(out, import_layout.import_directory_size);
    } else if (i == kIATDirectoryIndex) {
      Append32(out, import_layout.iat_rva);
      Append32(out, import_layout.iat_size);
    } else {
      Append32(out, 0);
      Append32(out, 0);
    }
  }

  // Section table (one IMAGE_SECTION_HEADER per section, 40 bytes each).
  auto emit_section = [&](const char name[8], std::uint32_t vsize, std::uint32_t rva,
                          std::uint32_t rsize, std::uint32_t roff,
                          std::uint32_t characteristics) {
    AppendBytes(out, reinterpret_cast<const std::uint8_t *>(name), 8);
    Append32(out, vsize); // VirtualSize
    Append32(out, rva);   // VirtualAddress
    Append32(out, rsize); // SizeOfRawData
    Append32(out, roff);  // PointerToRawData
    Append32(out, 0);     // PointerToRelocations
    Append32(out, 0);     // PointerToLinenumbers
    Append16(out, 0);     // NumberOfRelocations
    Append16(out, 0);     // NumberOfLinenumbers
    Append32(out, characteristics);
  };

  emit_section(".text\0\0\0", text_virtual_size, text_rva, text_raw_size, text_raw_offset,
               kImageScnCntCode | kImageScnMemExecute | kImageScnMemRead);
  if (has_rdata) {
    emit_section(".rdata\0\0", rdata_virtual_size, rdata_rva, rdata_raw_size, rdata_raw_offset,
                 kImageScnCntInitializedData | kImageScnMemRead);
  }
  if (has_idata) {
    emit_section(".idata\0\0", idata_virtual_size, idata_rva, idata_raw_size, idata_raw_offset,
                 kImageScnCntInitializedData | kImageScnMemRead | kImageScnMemWrite);
  }

  // Pad headers up to SizeOfHeaders.
  PadTo(out, size_of_headers);

  // Section data, in section-table order.
  AppendBytes(out, req.text_bytes.data(), req.text_bytes.size());
  PadTo(out, text_raw_offset + text_raw_size);
  if (has_rdata) {
    AppendBytes(out, req.rdata_bytes.data(), req.rdata_bytes.size());
    PadTo(out, rdata_raw_offset + rdata_raw_size);
  }
  if (has_idata) {
    AppendBytes(out, import_layout.bytes.data(), import_layout.bytes.size());
    PadTo(out, idata_raw_offset + idata_raw_size);
  }

  R.rdata_rva = rdata_rva;
  R.iat_slot_rva = std::move(import_layout.iat_slot_rva);
  return R;
}

BuildResult BuildMinimalExitZeroImage() {
  // Two-pass build: we need to know the IAT slot RVA in order to encode
  // the shim's call instruction, and we need the shim length to know where
  // .idata sits.  Solve it by laying out .text with a fixed 13-byte shim
  // first (a constant size, see BuildExitProcessShim() definition: 4-byte
  // sub rsp, 0x28 + 2-byte xor + 6-byte indirect call + 1-byte int3) so
  // .idata RVA is deterministic.
  constexpr std::uint32_t kShimSize = 13;
  const std::uint32_t shim_rva = kTextRVA; // entry is at start of .text

  // .idata RVA is the section-aligned RVA of the byte right after .text.
  const std::uint32_t idata_rva = AlignUp(kTextRVA + kShimSize, kSectionAlignment);

  // Build the import section to learn the IAT slot RVA for ExitProcess.
  std::vector<DllImport> imports = {DllImport{"kernel32.dll", {"ExitProcess"}}};
  ImportLayout layout = BuildImportSection(imports, idata_rva);

  // Find ExitProcess slot RVA.
  std::uint32_t exit_iat_rva = 0;
  for (const auto &kv : layout.iat_slot_rva) {
    if (kv.first == "kernel32.dll!ExitProcess") {
      exit_iat_rva = kv.second;
      break;
    }
  }

  // Now emit the shim with the correct disp32.
  std::vector<std::uint8_t> shim = BuildExitProcessShim(shim_rva, exit_iat_rva);

  BuildRequest req;
  req.text_bytes = std::move(shim);
  req.entry_offset_in_text = 0;
  req.imports = std::move(imports);
  return BuildPE32PlusImage(req);
}

BuildResult BuildExitZeroPE(const std::vector<std::uint8_t> &user_text_bytes) {
  // Degenerate case: no user code -> identical to the minimal image.
  if (user_text_bytes.empty())
    return BuildMinimalExitZeroImage();

  // Layout: [user_text_bytes][shim (13 bytes)] inside .text.
  // entry_rva   = kTextRVA + user_text_bytes.size()  (= start of shim)
  // text_size   = user_text_bytes.size() + kShimSize
  // idata_rva   = AlignUp(kTextRVA + text_size, kSectionAlignment)
  constexpr std::uint32_t kShimSize = 13;
  const std::uint32_t user_size = static_cast<std::uint32_t>(user_text_bytes.size());
  const std::uint32_t text_size = user_size + kShimSize;
  const std::uint32_t shim_rva = kTextRVA + user_size;
  const std::uint32_t idata_rva = AlignUp(kTextRVA + text_size, kSectionAlignment);

  // Resolve the ExitProcess IAT slot RVA at the planned .idata location.
  std::vector<DllImport> imports = {DllImport{"kernel32.dll", {"ExitProcess"}}};
  ImportLayout layout = BuildImportSection(imports, idata_rva);
  std::uint32_t exit_iat_rva = 0;
  for (const auto &kv : layout.iat_slot_rva) {
    if (kv.first == "kernel32.dll!ExitProcess") {
      exit_iat_rva = kv.second;
      break;
    }
  }

  // Encode the shim with the resolved disp32 and append it to user code.
  std::vector<std::uint8_t> shim = BuildExitProcessShim(shim_rva, exit_iat_rva);
  std::vector<std::uint8_t> text_bytes;
  text_bytes.reserve(text_size);
  text_bytes.insert(text_bytes.end(), user_text_bytes.begin(), user_text_bytes.end());
  text_bytes.insert(text_bytes.end(), shim.begin(), shim.end());

  BuildRequest req;
  req.text_bytes = std::move(text_bytes);
  req.entry_offset_in_text = user_size; // entry = first byte of the shim
  req.imports = std::move(imports);
  return BuildPE32PlusImage(req);
}

// ---------------------------------------------------------------------------
// User-entry shim: invoke user main, propagate its return value as exit code.
//
//   off    bytes  asm
//   0x00   4      sub  rsp, 0x28                       ; shadow space + align
//   0x04   5      call user_main                       ; E8 disp32
//   0x09   2      mov  ecx, eax                        ; arg0 = user return
//   0x0B   6      call qword ptr [rip + d_ExitProcess] ; ExitProcess(eax)
//   0x11   1      int3                                 ; unreachable
// Total: 18 bytes.
// ---------------------------------------------------------------------------
std::vector<std::uint8_t> BuildUserMainExitShim(std::uint32_t shim_rva,
                                                std::uint32_t user_main_rva,
                                                std::uint32_t exit_process_iat_rva) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(18);

  // sub rsp, 0x28
  Append8(bytes, 0x48);
  Append8(bytes, 0x83);
  Append8(bytes, 0xEC);
  Append8(bytes, 0x28);

  // call user_main : E8 disp32, where disp32 = user_main_rva - (RIP after E8 imm)
  // RIP after the 5-byte CALL = shim_rva + 4 + 5 = shim_rva + 9
  Append8(bytes, 0xE8);
  const std::uint32_t rip_after_call_user = shim_rva + 4 + 5;
  Append32(bytes, user_main_rva - rip_after_call_user);

  // mov ecx, eax
  Append8(bytes, 0x89);
  Append8(bytes, 0xC1);

  // call qword ptr [rip + disp32] : FF 15 disp32
  // RIP after this 6-byte CALL = shim_rva + 0x0B + 6 = shim_rva + 0x11 (17)
  Append8(bytes, 0xFF);
  Append8(bytes, 0x15);
  const std::uint32_t rip_after_call_exit = shim_rva + 0x0B + 6;
  Append32(bytes, exit_process_iat_rva - rip_after_call_exit);

  // int3
  Append8(bytes, 0xCC);
  return bytes;
}

BuildResult BuildExeWithUserEntry(const std::vector<std::uint8_t> &user_text_bytes,
                                  std::uint32_t user_main_offset_in_text) {
  BuildResult R;
  if (user_text_bytes.empty())
    return R;
  if (user_main_offset_in_text >= user_text_bytes.size())
    return R;

  // Layout: [user_text_bytes][user-entry shim (18 bytes)] inside .text.
  constexpr std::uint32_t kShimSize = 18;
  const std::uint32_t user_size = static_cast<std::uint32_t>(user_text_bytes.size());
  const std::uint32_t text_size = user_size + kShimSize;
  const std::uint32_t shim_rva = kTextRVA + user_size;
  const std::uint32_t user_main_rva = kTextRVA + user_main_offset_in_text;
  const std::uint32_t idata_rva = AlignUp(kTextRVA + text_size, kSectionAlignment);

  // Resolve the ExitProcess IAT slot RVA at the planned .idata location.
  std::vector<DllImport> imports = {DllImport{"kernel32.dll", {"ExitProcess"}}};
  ImportLayout layout = BuildImportSection(imports, idata_rva);
  std::uint32_t exit_iat_rva = 0;
  for (const auto &kv : layout.iat_slot_rva) {
    if (kv.first == "kernel32.dll!ExitProcess") {
      exit_iat_rva = kv.second;
      break;
    }
  }
  if (exit_iat_rva == 0)
    return R; // import layout failed to resolve ExitProcess

  // Encode shim and concatenate [user_text][shim].
  std::vector<std::uint8_t> shim =
      BuildUserMainExitShim(shim_rva, user_main_rva, exit_iat_rva);
  std::vector<std::uint8_t> text_bytes;
  text_bytes.reserve(text_size);
  text_bytes.insert(text_bytes.end(), user_text_bytes.begin(), user_text_bytes.end());
  text_bytes.insert(text_bytes.end(), shim.begin(), shim.end());

  BuildRequest req;
  req.text_bytes = std::move(text_bytes);
  req.entry_offset_in_text = user_size; // entry = first byte of shim
  req.imports = std::move(imports);
  return BuildPE32PlusImage(req);
}

// ===========================================================================
// BuildHelloWorldPE
//
// Produce a PE32+ that, on entry, performs:
//
//     HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);   // STD_OUTPUT_HANDLE = -11
//     DWORD  n = 0;
//     WriteFile(h, message_ptr, message_len, &n, NULL);
//     ExitProcess(0);
//
// The shim is exactly 68 bytes long (constant), enabling deterministic
// pre-layout of `.rdata` and `.idata` before disp32 fields are encoded.
//
// AMD64 / Win64 ABI accounting:
//   * The OS calls the entry point with RSP 16-byte aligned and an 8-byte
//     return address pushed (so RSP%16 == 8 on entry, like any function).
//   * To invoke a child function, RSP must be 16-byte aligned BEFORE the
//     CALL.  We therefore subtract `0x38` (which satisfies (8 - 0x38) % 16
//     == 0) to allocate:
//         [rsp+0x00 .. rsp+0x1F]  (32 bytes) shadow space for any child
//         [rsp+0x20 .. rsp+0x27]  ARG5 slot for WriteFile (lpOverlapped=NULL)
//         [rsp+0x28 .. rsp+0x2B]  DWORD bytes_written (out param via R9)
//         [rsp+0x2C .. rsp+0x37]  padding
//
// Per-instruction byte map (offsets relative to shim start):
//
//   off    bytes  asm
//   0x00   4      sub  rsp, 0x38
//   0x04   8      mov  dword ptr [rsp+0x28], 0          ; clear bytes_written
//   0x0C   9      mov  qword ptr [rsp+0x20], 0          ; ARG5 = NULL
//   0x15   5      mov  ecx, -11                          ; STD_OUTPUT_HANDLE
//   0x1A   6      call qword ptr [rip + d_GetStdHandle]
//   0x20   3      mov  rcx, rax                          ; hFile = handle
//   0x23   7      lea  rdx, [rip + d_message]            ; lpBuffer
//   0x2A   6      mov  r8d, message_len                  ; nNumberOfBytesToWrite
//   0x30   5      lea  r9, [rsp+0x28]                    ; lpNumberOfBytesWritten
//   0x35   6      call qword ptr [rip + d_WriteFile]
//   0x3B   2      xor  ecx, ecx                          ; ExitProcess(0)
//   0x3D   6      call qword ptr [rip + d_ExitProcess]
//   0x43   1      int3                                   ; unreachable
// ===========================================================================

namespace {

constexpr std::uint32_t kHelloShimSize = 0x44; // 68 bytes (see byte map above)

std::vector<std::uint8_t>
EncodeHelloShim(std::uint32_t shim_rva, std::uint32_t message_rva,
                std::uint32_t message_len, std::uint32_t getstdhandle_iat_rva,
                std::uint32_t writefile_iat_rva, std::uint32_t exitprocess_iat_rva) {
  std::vector<std::uint8_t> b;
  b.reserve(kHelloShimSize);

  // 0x00: sub rsp, 0x38
  Append8(b, 0x48); Append8(b, 0x83); Append8(b, 0xEC); Append8(b, 0x38);

  // 0x04: mov dword ptr [rsp+0x28], 0  (C7 44 24 28 00 00 00 00)
  Append8(b, 0xC7); Append8(b, 0x44); Append8(b, 0x24); Append8(b, 0x28);
  Append8(b, 0x00); Append8(b, 0x00); Append8(b, 0x00); Append8(b, 0x00);

  // 0x0C: mov qword ptr [rsp+0x20], 0  (48 C7 44 24 20 00 00 00 00)
  Append8(b, 0x48); Append8(b, 0xC7); Append8(b, 0x44); Append8(b, 0x24);
  Append8(b, 0x20); Append8(b, 0x00); Append8(b, 0x00); Append8(b, 0x00);
  Append8(b, 0x00);

  // 0x15: mov ecx, -11   (B9 F5 FF FF FF)
  Append8(b, 0xB9); Append8(b, 0xF5); Append8(b, 0xFF); Append8(b, 0xFF); Append8(b, 0xFF);

  // 0x1A: call qword ptr [rip + d_GetStdHandle]   (FF 15 disp32)
  Append8(b, 0xFF); Append8(b, 0x15);
  {
    const std::uint32_t rip_after = shim_rva + 0x20;
    Append32(b, getstdhandle_iat_rva - rip_after);
  }

  // 0x20: mov rcx, rax   (48 89 C1)
  Append8(b, 0x48); Append8(b, 0x89); Append8(b, 0xC1);

  // 0x23: lea rdx, [rip + d_message]   (48 8D 15 disp32)
  Append8(b, 0x48); Append8(b, 0x8D); Append8(b, 0x15);
  {
    const std::uint32_t rip_after = shim_rva + 0x2A;
    Append32(b, message_rva - rip_after);
  }

  // 0x2A: mov r8d, message_len   (41 B8 LL LL LL LL)
  Append8(b, 0x41); Append8(b, 0xB8);
  Append32(b, message_len);

  // 0x30: lea r9, [rsp+0x28]   (4C 8D 4C 24 28)
  Append8(b, 0x4C); Append8(b, 0x8D); Append8(b, 0x4C); Append8(b, 0x24); Append8(b, 0x28);

  // 0x35: call qword ptr [rip + d_WriteFile]   (FF 15 disp32)
  Append8(b, 0xFF); Append8(b, 0x15);
  {
    const std::uint32_t rip_after = shim_rva + 0x3B;
    Append32(b, writefile_iat_rva - rip_after);
  }

  // 0x3B: xor ecx, ecx   (31 C9)
  Append8(b, 0x31); Append8(b, 0xC9);

  // 0x3D: call qword ptr [rip + d_ExitProcess]   (FF 15 disp32)
  Append8(b, 0xFF); Append8(b, 0x15);
  {
    const std::uint32_t rip_after = shim_rva + 0x43;
    Append32(b, exitprocess_iat_rva - rip_after);
  }

  // 0x43: int3   (CC)
  Append8(b, 0xCC);

  return b;
}

} // namespace

BuildResult BuildHelloWorldPE(const std::string &message) {
  BuildResult err;
  // WriteFile's nNumberOfBytesToWrite is a DWORD; reject anything that would
  // not fit (4 GiB - 1).  Practical messages are tiny so this is purely a
  // defensive bound.
  if (message.size() > 0xFFFFFFFFu)
    return err;

  // Pre-plan the layout so disp32 fields can be resolved before code emission.
  const std::uint32_t shim_rva = kTextRVA;          // entry == start of shim
  const std::uint32_t text_size = kHelloShimSize;
  const std::uint32_t rdata_rva = AlignUp(kTextRVA + text_size, kSectionAlignment);
  const std::uint32_t message_rva = rdata_rva;       // message at .rdata + 0
  const std::uint32_t message_len = static_cast<std::uint32_t>(message.size());
  const std::uint32_t after_rdata =
      message_len == 0 ? rdata_rva : rdata_rva + message_len;
  const std::uint32_t idata_rva = AlignUp(after_rdata, kSectionAlignment);

  // Resolve the IAT slot RVAs at the planned .idata location.  Order
  // matters for cross-checking against the hand-encoded shim, but
  // BuildImportSection guarantees per-DLL ordering matches the input.
  std::vector<DllImport> imports = {
      DllImport{"kernel32.dll", {"GetStdHandle", "WriteFile", "ExitProcess"}}};
  ImportLayout layout = BuildImportSection(imports, idata_rva);

  std::uint32_t getstdhandle_iat = 0;
  std::uint32_t writefile_iat = 0;
  std::uint32_t exitprocess_iat = 0;
  for (const auto &kv : layout.iat_slot_rva) {
    if (kv.first == "kernel32.dll!GetStdHandle")
      getstdhandle_iat = kv.second;
    else if (kv.first == "kernel32.dll!WriteFile")
      writefile_iat = kv.second;
    else if (kv.first == "kernel32.dll!ExitProcess")
      exitprocess_iat = kv.second;
  }
  if (getstdhandle_iat == 0 || writefile_iat == 0 || exitprocess_iat == 0)
    return err; // BuildImportSection invariant violation; should never trigger

  // Encode the shim with the now-known disp32s.
  std::vector<std::uint8_t> shim =
      EncodeHelloShim(shim_rva, message_rva, message_len,
                      getstdhandle_iat, writefile_iat, exitprocess_iat);

  // Compose request: .text = shim, .rdata = message bytes, imports = the 3
  // kernel32 functions.
  BuildRequest req;
  req.text_bytes = std::move(shim);
  req.rdata_bytes.assign(message.begin(), message.end());
  req.entry_offset_in_text = 0;
  req.imports = std::move(imports);
  return BuildPE32PlusImage(req);
}

// ============================================================================
// BuildPrintlnSequencePE  (Stage B4 of demand 2026-04-28-49)
// ============================================================================
//
// Same overall shape as BuildHelloWorldPE, but issues N WriteFile calls
// against a single shared stdout handle instead of one.  Pseudocode:
//
//     HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
//     for (i = 0; i < N; ++i)
//         WriteFile(h, msg_ptr[i], msg_len[i], &dummy, NULL);
//     ExitProcess(0);
//
// AMD64 / Win64 ABI implementation notes:
//   - We reuse the proven 0x38-byte stack frame from the hello shim:
//       [rsp+0x00..0x1F] shadow space (Win64 mandates 32 bytes for any call)
//       [rsp+0x20]       lpOverlapped slot (must be NULL for synchronous
//                        WriteFile)
//       [rsp+0x28]       lpNumberOfBytesWritten slot (we never read it back)
//       [rsp+0x30]       saved stdout handle
//   - The handle save uses a stack slot rather than a callee-saved register
//     so we don't have to push/pop RBX/R12/R13 (and so the prologue / epilogue
//     bytes stay easy to count and unit-test).
//   - The `lpOverlapped` slot is zeroed once during the prologue; WriteFile
//     reads it but never writes it, so the value is reused across all N calls.
//
// Per-message block (29 bytes — see BuildPrintlnBlock below):
//     mov rcx, [rsp+0x30]           ; arg0 = saved handle
//     lea rdx, [rip + msg_i]        ; arg1 = pointer into .rdata
//     mov r8d, len_i                ; arg2 = length (DWORD)
//     lea r9, [rsp+0x28]            ; arg3 = &lpNumberOfBytesWritten
//     call qword ptr [rip + WriteFile_IAT]
//
// .rdata layout: identical messages share their bytes (we dedupe by content,
// matching the IR-layer interning contract).

namespace {

constexpr std::uint32_t kPrintlnPrologueSize = 0x25; // 37 bytes
constexpr std::uint32_t kPrintlnPerMessageSize = 0x1D; // 29 bytes
constexpr std::uint32_t kPrintlnEpilogueSize = 0x09;   // 9 bytes

// Emit the one-time prologue: stack frame + zero-out lpOverlapped & written-
// count slots + GetStdHandle(STD_OUTPUT_HANDLE) + save returned handle in
// [rsp+0x30].
void EncodePrintlnPrologue(std::vector<std::uint8_t> &b, std::uint32_t shim_rva,
                           std::uint32_t getstdhandle_iat_rva) {
  // 0x00: sub rsp, 0x38                       (4)
  Append8(b, 0x48); Append8(b, 0x83); Append8(b, 0xEC); Append8(b, 0x38);

  // 0x04: mov dword [rsp+0x28], 0             (8)
  Append8(b, 0xC7); Append8(b, 0x44); Append8(b, 0x24); Append8(b, 0x28);
  Append8(b, 0x00); Append8(b, 0x00); Append8(b, 0x00); Append8(b, 0x00);

  // 0x0C: mov qword [rsp+0x20], 0             (9)
  Append8(b, 0x48); Append8(b, 0xC7); Append8(b, 0x44); Append8(b, 0x24);
  Append8(b, 0x20); Append8(b, 0x00); Append8(b, 0x00); Append8(b, 0x00);
  Append8(b, 0x00);

  // 0x15: mov ecx, -11                        (5)
  Append8(b, 0xB9); Append8(b, 0xF5); Append8(b, 0xFF); Append8(b, 0xFF); Append8(b, 0xFF);

  // 0x1A: call qword ptr [rip+GetStdHandle]   (6)
  Append8(b, 0xFF); Append8(b, 0x15);
  {
    const std::uint32_t rip_after = shim_rva + 0x20;
    Append32(b, getstdhandle_iat_rva - rip_after);
  }

  // 0x20: mov [rsp+0x30], rax                 (5: 48 89 44 24 30)
  Append8(b, 0x48); Append8(b, 0x89); Append8(b, 0x44); Append8(b, 0x24); Append8(b, 0x30);
  // Now at offset 0x25 == kPrintlnPrologueSize.
}

// Emit a single per-message block.  `block_rva` is the RVA of the *first*
// byte of this block; the disp32 fields are computed against it.
void EncodePrintlnBlock(std::vector<std::uint8_t> &b, std::uint32_t block_rva,
                        std::uint32_t message_rva, std::uint32_t message_len,
                        std::uint32_t writefile_iat_rva) {
  // +0x00: mov rcx, [rsp+0x30]                (5: 48 8B 4C 24 30)
  Append8(b, 0x48); Append8(b, 0x8B); Append8(b, 0x4C); Append8(b, 0x24); Append8(b, 0x30);

  // +0x05: lea rdx, [rip + msg]               (7: 48 8D 15 disp32)
  Append8(b, 0x48); Append8(b, 0x8D); Append8(b, 0x15);
  {
    const std::uint32_t rip_after = block_rva + 0x0C;
    Append32(b, message_rva - rip_after);
  }

  // +0x0C: mov r8d, len                       (6: 41 B8 LL LL LL LL)
  Append8(b, 0x41); Append8(b, 0xB8);
  Append32(b, message_len);

  // +0x12: lea r9, [rsp+0x28]                 (5: 4C 8D 4C 24 28)
  Append8(b, 0x4C); Append8(b, 0x8D); Append8(b, 0x4C); Append8(b, 0x24); Append8(b, 0x28);

  // +0x17: call qword ptr [rip+WriteFile]     (6: FF 15 disp32)
  Append8(b, 0xFF); Append8(b, 0x15);
  {
    const std::uint32_t rip_after = block_rva + 0x1D;
    Append32(b, writefile_iat_rva - rip_after);
  }
  // Now at +0x1D == kPrintlnPerMessageSize.
}

// Emit the final epilogue: ExitProcess(0) + int3.
void EncodePrintlnEpilogue(std::vector<std::uint8_t> &b, std::uint32_t epilogue_rva,
                           std::uint32_t exitprocess_iat_rva) {
  // +0x00: xor ecx, ecx                       (2)
  Append8(b, 0x31); Append8(b, 0xC9);
  // +0x02: call qword ptr [rip+ExitProcess]   (6)
  Append8(b, 0xFF); Append8(b, 0x15);
  {
    const std::uint32_t rip_after = epilogue_rva + 0x08;
    Append32(b, exitprocess_iat_rva - rip_after);
  }
  // +0x08: int3                               (1)
  Append8(b, 0xCC);
  // Now at +0x09 == kPrintlnEpilogueSize.
}

} // namespace

BuildResult BuildPrintlnSequencePE(const std::vector<std::string> &call_messages) {
  BuildResult err;

  // No PRINTLN calls ⇒ behaviourally identical to BuildExitZeroPE.  Forwarding
  // keeps the contract crystal-clear and avoids a degenerate empty-shim case.
  if (call_messages.empty())
    return BuildExitZeroPE({});

  // Reject any single message that won't fit in a DWORD (WriteFile API
  // constraint).  Practical messages are small; this is purely defensive.
  for (const auto &m : call_messages) {
    if (m.size() > 0xFFFFFFFFu)
      return err;
  }

  // Dedupe message contents to mirror the IR-layer interning contract.  We
  // keep the *call* order intact for the shim, but each unique payload is
  // embedded in .rdata exactly once.  `unique_offsets[content] = byte offset
  // within .rdata`; `rdata_blob` accumulates the concatenated unique bytes.
  std::vector<std::uint8_t> rdata_blob;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> per_call;  // (rdata_off, len)
  per_call.reserve(call_messages.size());
  // Linear lookup is fine: even programs with hundreds of PRINTLNs have
  // tractable message counts and content comparison is cheap; we trade O(N²)
  // for the absence of an extra <unordered_map> include in this header-light
  // module.
  std::vector<std::pair<std::string, std::uint32_t>> unique_table;
  for (const auto &m : call_messages) {
    bool found = false;
    std::uint32_t off = 0;
    for (const auto &kv : unique_table) {
      if (kv.first == m) {
        off = kv.second;
        found = true;
        break;
      }
    }
    if (!found) {
      off = static_cast<std::uint32_t>(rdata_blob.size());
      rdata_blob.insert(rdata_blob.end(), m.begin(), m.end());
      unique_table.emplace_back(m, off);
    }
    per_call.emplace_back(off, static_cast<std::uint32_t>(m.size()));
  }

  // Plan the layout the same way BuildHelloWorldPE does.
  const std::uint32_t shim_rva = kTextRVA;
  const std::uint32_t text_size =
      kPrintlnPrologueSize +
      static_cast<std::uint32_t>(per_call.size()) * kPrintlnPerMessageSize +
      kPrintlnEpilogueSize;
  const std::uint32_t rdata_rva = AlignUp(kTextRVA + text_size, kSectionAlignment);
  const std::uint32_t rdata_size = static_cast<std::uint32_t>(rdata_blob.size());
  const std::uint32_t after_rdata = rdata_size == 0 ? rdata_rva : rdata_rva + rdata_size;
  const std::uint32_t idata_rva = AlignUp(after_rdata, kSectionAlignment);

  std::vector<DllImport> imports = {
      DllImport{"kernel32.dll", {"GetStdHandle", "WriteFile", "ExitProcess"}}};
  ImportLayout layout = BuildImportSection(imports, idata_rva);

  std::uint32_t getstdhandle_iat = 0;
  std::uint32_t writefile_iat = 0;
  std::uint32_t exitprocess_iat = 0;
  for (const auto &kv : layout.iat_slot_rva) {
    if (kv.first == "kernel32.dll!GetStdHandle")
      getstdhandle_iat = kv.second;
    else if (kv.first == "kernel32.dll!WriteFile")
      writefile_iat = kv.second;
    else if (kv.first == "kernel32.dll!ExitProcess")
      exitprocess_iat = kv.second;
  }
  if (getstdhandle_iat == 0 || writefile_iat == 0 || exitprocess_iat == 0)
    return err;

  // Emit the .text bytes: prologue, then one block per call, then epilogue.
  std::vector<std::uint8_t> text;
  text.reserve(text_size);
  EncodePrintlnPrologue(text, shim_rva, getstdhandle_iat);

  std::uint32_t cursor_rva = shim_rva + kPrintlnPrologueSize;
  for (const auto &pc : per_call) {
    const std::uint32_t msg_rva = rdata_rva + pc.first;
    EncodePrintlnBlock(text, cursor_rva, msg_rva, pc.second, writefile_iat);
    cursor_rva += kPrintlnPerMessageSize;
  }
  EncodePrintlnEpilogue(text, cursor_rva, exitprocess_iat);

  // Sanity check: total size must match the planned text_size exactly so the
  // .rdata RVA we computed remains valid.  A mismatch indicates a per-message
  // / prologue / epilogue size constant drift; surfacing it as an empty
  // result here is much cheaper to debug than a crashing PE on Windows.
  if (text.size() != text_size)
    return err;

  BuildRequest req;
  req.text_bytes = std::move(text);
  req.rdata_bytes = std::move(rdata_blob);
  req.entry_offset_in_text = 0;
  req.imports = std::move(imports);
  return BuildPE32PlusImage(req);
}

} // namespace polyglot::linker::pe
