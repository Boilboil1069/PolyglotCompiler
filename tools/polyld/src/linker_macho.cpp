/**
 * @file     linker_macho.cpp
 * @brief    Mach-O linker support: emits MH_EXECUTE / MH_DYLIB / MH_BUNDLE
 *           images with the full canonical load-command set, real
 *           __LINKEDIT (symbol + string + dysymtab + indirect symbol +
 *           reloc tables), x86_64 / arm64 relocation translation, and an
 *           optional ad-hoc LC_CODE_SIGNATURE placeholder region for
 *           later `codesign --force --sign -` injection.
 *
 * Companion to `linker_pe.cpp`: free helpers live in the inner `macho`
 * namespace; the three `Linker::Generate*` entry points appear at the
 * bottom of the file and are the only members that touch `Linker`'s
 * private state.
 *
 * @ingroup  Tool / polyld
 * @author   Manning Cyrus
 * @date     2026-05-06
 */

#include "tools/polyld/include/linker_macho.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "tools/polyld/include/linker.h"

namespace polyglot::linker::macho {

namespace {

// ---------------------------------------------------------------------------
// Endian helpers — Mach-O is always little-endian on the architectures
// we care about (x86_64 / arm64).
// ---------------------------------------------------------------------------

void PutU16(std::vector<std::uint8_t> &out, std::uint16_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

void PutU32(std::vector<std::uint8_t> &out, std::uint32_t v) {
  for (int i = 0; i < 4; ++i)
    out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
}

void PutU64(std::vector<std::uint8_t> &out, std::uint64_t v) {
  for (int i = 0; i < 8; ++i)
    out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
}

void PutFixedString(std::vector<std::uint8_t> &out, const std::string &s,
                    std::size_t fixed) {
  for (std::size_t i = 0; i < fixed; ++i) {
    out.push_back(i < s.size() ? static_cast<std::uint8_t>(s[i]) : 0);
  }
}

void PadTo(std::vector<std::uint8_t> &out, std::size_t target) {
  while (out.size() < target) out.push_back(0);
}

std::uint64_t AlignUp(std::uint64_t v, std::uint64_t a) {
  return (v + a - 1) & ~(a - 1);
}

// ---------------------------------------------------------------------------
// Minimal SHA-256 (RFC 6234) — used only to derive the LC_UUID payload
// from segment content.  Not exposed outside this TU.
// ---------------------------------------------------------------------------

struct Sha256 {
  std::uint32_t h[8];
  std::uint64_t length{0};
  std::uint8_t  buffer[64];
  std::size_t   buffer_len{0};

  Sha256() {
    static const std::uint32_t init[8] = {
      0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
    std::memcpy(h, init, sizeof(init));
  }

  static std::uint32_t Rotr(std::uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
  }

  void ProcessBlock(const std::uint8_t *block) {
    static const std::uint32_t k[64] = {
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(block[i * 4 + 0]) << 24) |
             (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
             (static_cast<std::uint32_t>(block[i * 4 + 2]) <<  8) |
             (static_cast<std::uint32_t>(block[i * 4 + 3]) <<  0);
    }
    for (int i = 16; i < 64; ++i) {
      std::uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      std::uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      std::uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
      std::uint32_t ch = (e & f) ^ (~e & g);
      std::uint32_t t1 = hh + S1 + ch + k[i] + w[i];
      std::uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
      std::uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
      std::uint32_t t2 = S0 + mj;
      hh = g; g = f; f = e; e = d + t1;
      d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }

  void Update(const std::uint8_t *data, std::size_t len) {
    length += len;
    while (len > 0) {
      std::size_t take = std::min<std::size_t>(64 - buffer_len, len);
      std::memcpy(buffer + buffer_len, data, take);
      buffer_len += take;
      data += take;
      len  -= take;
      if (buffer_len == 64) {
        ProcessBlock(buffer);
        buffer_len = 0;
      }
    }
  }

  std::array<std::uint8_t, 32> Finalize() {
    std::uint64_t bit_len = length * 8;
    std::uint8_t pad = 0x80;
    Update(&pad, 1);
    std::uint8_t zero = 0;
    while (buffer_len != 56) Update(&zero, 1);
    std::uint8_t lenbuf[8];
    for (int i = 0; i < 8; ++i)
      lenbuf[i] = static_cast<std::uint8_t>((bit_len >> (56 - i * 8)) & 0xFF);
    Update(lenbuf, 8);
    std::array<std::uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i) {
      out[i * 4 + 0] = static_cast<std::uint8_t>((h[i] >> 24) & 0xFF);
      out[i * 4 + 1] = static_cast<std::uint8_t>((h[i] >> 16) & 0xFF);
      out[i * 4 + 2] = static_cast<std::uint8_t>((h[i] >>  8) & 0xFF);
      out[i * 4 + 3] = static_cast<std::uint8_t>((h[i] >>  0) & 0xFF);
    }
    return out;
  }
};

// Pack a "X.Y.Z" string into the canonical Mach-O 32-bit version word
// layout: X << 16 | Y << 8 | Z (each component clamped to 0..255).
std::uint32_t PackVersion32(const std::string &s) {
  unsigned x = 0, y = 0, z = 0;
  std::sscanf(s.c_str(), "%u.%u.%u", &x, &y, &z);
  return ((x & 0xFFFFu) << 16) | ((y & 0xFFu) << 8) | (z & 0xFFu);
}

std::uint64_t PackVersion64(const std::string &s) {
  unsigned a = 0, b = 0, c = 0, d = 0, e = 0;
  std::sscanf(s.c_str(), "%u.%u.%u.%u.%u", &a, &b, &c, &d, &e);
  // Apple LC_SOURCE_VERSION layout: a24.b10.c10.d10.e10
  return (static_cast<std::uint64_t>(a) << 40) |
         (static_cast<std::uint64_t>(b) << 30) |
         (static_cast<std::uint64_t>(c) << 20) |
         (static_cast<std::uint64_t>(d) << 10) |
          static_cast<std::uint64_t>(e);
}

// Native arm64 Mach-O images on Apple Silicon must use 16 KiB page
// alignment for every segment; loading a 4 KiB-aligned arm64 image is a
// hard reject in the kernel mapper ("vm_map: bad alignment") even when
// every other Mach-O field is well-formed.  x86_64 / Rosetta keeps the
// classical 4 KiB page.  The writer picks the right page size from the
// `BuildRequest::arch` field via `PageSizeForArch`.
constexpr std::uint64_t kPageSize4K     = 0x1000;
constexpr std::uint64_t kPageSize16K    = 0x4000;
constexpr std::uint64_t kPageZeroSize64 = 0x100000000ULL;
constexpr std::uint64_t kDefaultExeBase = 0x100000000ULL;

std::uint64_t PageSizeForArch(MachOArch a) {
  return a == MachOArch::kArm64 ? kPageSize16K : kPageSize4K;
}

// ---------------------------------------------------------------------------
// ULEB128 helpers shared by the LC_DYLD_EXPORTS_TRIE and
// LC_FUNCTION_STARTS encoders below.  Both blobs use Apple's
// little-endian base-128 variable-length integer encoding exactly as
// described in `<mach-o/loader.h>`.  `EncodeULEB128` is also exposed
// publicly so the polyld unit-test suite can spot-check the encoder
// without re-declaring it.
// ---------------------------------------------------------------------------

std::size_t ULEB128Size(std::uint64_t v) {
  std::size_t n = 1;
  while (v >= 0x80) { v >>= 7; ++n; }
  return n;
}

void EncodeULEB128(std::vector<std::uint8_t> &out, std::uint64_t v) {
  do {
    std::uint8_t byte = static_cast<std::uint8_t>(v & 0x7Fu);
    v >>= 7;
    if (v != 0) byte |= 0x80u;
    out.push_back(byte);
  } while (v != 0);
}

// ---------------------------------------------------------------------------
// LC_DYLD_EXPORTS_TRIE builder.
//
// dyld's exports trie is a packed prefix tree whose nodes carry an
// optional terminal payload (flags + address) and zero-or-more outgoing
// edges keyed by the next character substring.  Every child reference
// is a ULEB128 byte offset measured from the start of the trie blob,
// so node sizes and node positions are mutually recursive: shrinking
// one node may shrink the encoded length of another node's child
// offsets, which may shrink the first node further.  We resolve the
// fixed point by iterating the layout until no node moves.
//
// For polyld's MH_EXECUTE images we materialise exactly two leaves —
// `_main` (regular export, address = `LC_MAIN.entryoff +
// __TEXT.fileoff`) and `_mh_execute_header` (regular export at the
// image base, address = 0) — sharing the `"_m"` prefix above an inner
// fan-out node.  AMFI / CoreTrust / AppleSystemPolicy on macOS 26
// (Tahoe) all walk this trie at execve(2) time and refuse to load
// images that lack a real `_main` entry.
// ---------------------------------------------------------------------------

constexpr std::uint64_t kExportFlagsRegular = 0x0u; // EXPORT_SYMBOL_FLAGS_KIND_REGULAR

struct TrieNode {
  bool        terminal = false;
  std::uint64_t flags  = 0;
  std::uint64_t address = 0;
  std::vector<std::pair<std::string, std::size_t>> edges; // (label, child_index)
  std::uint32_t offset = 0; // computed during layout
  std::uint32_t size   = 0; // computed during layout
};

// Compute the on-disk size of a single trie node given the current
// snapshot of every node's offset (used to size the child-offset
// ULEB128 fields).
std::uint32_t TrieNodeSize(const TrieNode &n,
                           const std::vector<TrieNode> &all) {
  std::size_t terminal_payload_size = 0;
  if (n.terminal) {
    terminal_payload_size =
        ULEB128Size(n.flags) + ULEB128Size(n.address);
  }
  std::size_t total = ULEB128Size(terminal_payload_size) +
                      terminal_payload_size +
                      1u; // children_count byte
  for (const auto &e : n.edges) {
    total += e.first.size() + 1u; // edge label + NUL terminator
    total += ULEB128Size(all[e.second].offset);
  }
  return static_cast<std::uint32_t>(total);
}

std::vector<std::uint8_t>
BuildExecutableExportsTrie(std::uint64_t main_address) {
  // Node ordering chosen so that parents appear before children in the
  // serialised stream (matches what ld64 emits and keeps the byte
  // walker single-pass).
  std::vector<TrieNode> nodes(4);
  // 0: root (no terminal, single edge "_m")
  nodes[0].edges.push_back({"_m", 1});
  // 1: inner node after consuming "_m" (no terminal, two edges)
  nodes[1].edges.push_back({"ain", 2});
  nodes[1].edges.push_back({"h_execute_header", 3});
  // 2: leaf for "_main" (regular export, address = LC_MAIN.entryoff +
  // __TEXT.fileoff; the writer passes the already-summed value).
  nodes[2].terminal = true;
  nodes[2].flags    = kExportFlagsRegular;
  nodes[2].address  = main_address;
  // 3: leaf for "_mh_execute_header" (regular export at image base).
  nodes[3].terminal = true;
  nodes[3].flags    = kExportFlagsRegular;
  nodes[3].address  = 0;

  // Iterate the size/offset fixed point.  Bounded by a hard cap so a
  // pathological input cannot loop forever; in practice convergence
  // happens within two iterations for this 4-node trie.
  for (int iter = 0; iter < 16; ++iter) {
    std::uint32_t off = 0;
    bool changed = false;
    for (auto &n : nodes) {
      if (n.offset != off) { n.offset = off; changed = true; }
      std::uint32_t sz = TrieNodeSize(n, nodes);
      if (n.size != sz) { n.size = sz; changed = true; }
      off += sz;
    }
    if (!changed) break;
  }

  // Serialise.
  std::vector<std::uint8_t> out;
  for (const auto &n : nodes) {
    std::size_t terminal_payload_size = 0;
    if (n.terminal) {
      terminal_payload_size =
          ULEB128Size(n.flags) + ULEB128Size(n.address);
    }
    EncodeULEB128(out, terminal_payload_size);
    if (n.terminal) {
      EncodeULEB128(out, n.flags);
      EncodeULEB128(out, n.address);
    }
    out.push_back(static_cast<std::uint8_t>(n.edges.size()));
    for (const auto &e : n.edges) {
      for (char c : e.first) out.push_back(static_cast<std::uint8_t>(c));
      out.push_back(0);
      EncodeULEB128(out, nodes[e.second].offset);
    }
  }
  // 8-byte alignment as required by every linkedit_data_command payload.
  while (out.size() % 8 != 0) out.push_back(0);
  return out;
}

// LC_FUNCTION_STARTS payload: a sequence of ULEB128 deltas measured
// from the previous function start (the first delta is from zero, i.e.
// the absolute value of `LC_MAIN.entryoff`), terminated by a single
// 0x00 byte and padded to 8-byte alignment.  For a single-function
// MH_EXECUTE this collapses to `ULEB128(entryoff) + 0x00 + padding`.
std::vector<std::uint8_t>
BuildFunctionStartsBlob(const std::vector<std::uint64_t> &starts_in_order) {
  std::vector<std::uint8_t> out;
  std::uint64_t prev = 0;
  for (std::uint64_t s : starts_in_order) {
    EncodeULEB128(out, s - prev);
    prev = s;
  }
  out.push_back(0); // terminator
  while (out.size() % 8 != 0) out.push_back(0);
  return out;
}

// ---------------------------------------------------------------------------
// Mach-O code-signature constants (from Apple's cs_blobs.h).  Used to emit
// a "linker-signed" ad-hoc signature inline so the binary loads on Apple
// Silicon without requiring a post-link `codesign --force --sign -` step.
// ---------------------------------------------------------------------------

constexpr std::uint32_t kCsMagicEmbeddedSig = 0xFADE0CC0u;
constexpr std::uint32_t kCsMagicCodeDir     = 0xFADE0C02u;
constexpr std::uint32_t kCsSlotCodeDir      = 0;
constexpr std::uint32_t kCsCodeDirVersion   = 0x20400u; // supports execSeg
constexpr std::uint32_t kCsAdhoc            = 0x00000002u;
constexpr std::uint32_t kCsLinkerSigned     = 0x00020000u;
constexpr std::uint8_t  kCsHashTypeSha256   = 2;
constexpr std::uint8_t  kCsHashSizeSha256   = 32;
constexpr std::uint8_t  kCsPageSizeLog2     = 12;     // 4096-byte pages
constexpr std::uint64_t kCsExecSegMainBin   = 0x1ULL;

// Compute the linker-signed embedded-signature SuperBlob covering bytes
// `image[0..code_limit)`.  The blob is an SHA-256 page-hash CodeDirectory
// wrapped in the standard SuperBlob envelope, with the `linker-signed` and
// `adhoc` flags both set so AMFI accepts it without a post-link codesign
// step.  The returned vector is padded to `reserved_size` so it can be
// dropped into a pre-allocated LC_CODE_SIGNATURE region in place.
std::vector<std::uint8_t>
BuildLinkerSignedSignature(const std::vector<std::uint8_t> &image,
                           std::uint32_t code_limit,
                           std::uint64_t exec_seg_base,
                           std::uint64_t exec_seg_limit,
                           const std::string &identifier,
                           std::uint32_t reserved_size) {
  const std::uint32_t page_size = 1u << kCsPageSizeLog2;
  const std::uint32_t n_code_slots =
      (code_limit + page_size - 1) / page_size;

  // CodeDirectory layout:
  //   [0..88)    fixed v20400 header
  //   [88..)     identifier string (NUL-terminated)
  //   [hashOff)  n_code_slots * 32-byte SHA-256 page hashes
  const std::uint32_t cd_header_size = 88;
  const std::uint32_t ident_size =
      static_cast<std::uint32_t>(identifier.size() + 1);
  const std::uint32_t hash_off = cd_header_size + ident_size;
  const std::uint32_t cd_total_size =
      hash_off + n_code_slots * kCsHashSizeSha256;

  // SuperBlob: 12-byte header + one 8-byte BlobIndex
  const std::uint32_t super_header_size = 12 + 8;
  const std::uint32_t super_total_size  = super_header_size + cd_total_size;

  std::vector<std::uint8_t> blob;
  blob.reserve(std::max<std::uint32_t>(super_total_size, reserved_size));
  auto put_u32_be = [&](std::uint32_t v) {
    blob.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    blob.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    blob.push_back(static_cast<std::uint8_t>((v >>  8) & 0xFF));
    blob.push_back(static_cast<std::uint8_t>((v >>  0) & 0xFF));
  };
  auto put_u64_be = [&](std::uint64_t v) {
    blob.push_back(static_cast<std::uint8_t>((v >> 56) & 0xFF));
    blob.push_back(static_cast<std::uint8_t>((v >> 48) & 0xFF));
    blob.push_back(static_cast<std::uint8_t>((v >> 40) & 0xFF));
    blob.push_back(static_cast<std::uint8_t>((v >> 32) & 0xFF));
    blob.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    blob.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    blob.push_back(static_cast<std::uint8_t>((v >>  8) & 0xFF));
    blob.push_back(static_cast<std::uint8_t>((v >>  0) & 0xFF));
  };

  // ----- SuperBlob header -------------------------------------------------
  put_u32_be(kCsMagicEmbeddedSig);   // magic
  put_u32_be(super_total_size);      // length
  put_u32_be(1);                     // count
  put_u32_be(kCsSlotCodeDir);        // BlobIndex.type
  put_u32_be(super_header_size);     // BlobIndex.offset (start of CD)

  // ----- CodeDirectory header --------------------------------------------
  put_u32_be(kCsMagicCodeDir);                          // magic
  put_u32_be(cd_total_size);                            // length
  put_u32_be(kCsCodeDirVersion);                        // version
  put_u32_be(kCsAdhoc | kCsLinkerSigned);               // flags
  put_u32_be(hash_off);                                 // hashOffset
  put_u32_be(cd_header_size);                           // identOffset
  put_u32_be(0);                                        // nSpecialSlots
  put_u32_be(n_code_slots);                             // nCodeSlots
  put_u32_be(code_limit);                               // codeLimit
  blob.push_back(kCsHashSizeSha256);                    // hashSize
  blob.push_back(kCsHashTypeSha256);                    // hashType
  blob.push_back(0);                                    // platform
  blob.push_back(kCsPageSizeLog2);                      // pageSize (log2)
  put_u32_be(0);                                        // spare2
  put_u32_be(0);                                        // scatterOffset (v20100)
  put_u32_be(0);                                        // teamOffset    (v20200)
  put_u32_be(0);                                        // spare3        (v20300)
  put_u64_be(0);                                        // codeLimit64   (v20300)
  put_u64_be(exec_seg_base);                            // execSegBase   (v20400)
  put_u64_be(exec_seg_limit);                           // execSegLimit
  put_u64_be(kCsExecSegMainBin);                        // execSegFlags

  // ----- Identifier string ------------------------------------------------
  for (char c : identifier) blob.push_back(static_cast<std::uint8_t>(c));
  blob.push_back(0);

  // ----- Code-slot SHA-256 page hashes -----------------------------------
  for (std::uint32_t i = 0; i < n_code_slots; ++i) {
    const std::uint32_t off = i * page_size;
    const std::uint32_t take = std::min(page_size, code_limit - off);
    Sha256 h;
    h.Update(image.data() + off, take);
    auto digest = h.Finalize();
    for (auto b : digest) blob.push_back(b);
  }

  // Pad to the reserved region size so the LC_CODE_SIGNATURE dataoff /
  // datasize already encoded in the load commands stays consistent.
  if (blob.size() < reserved_size) blob.resize(reserved_size, 0);
  return blob;
}

} // namespace

// ===========================================================================
// Public helpers
// ===========================================================================

std::array<std::uint8_t, 16>
Uuid16FromContent(const std::vector<std::uint8_t> &bytes) {
  Sha256 h;
  if (!bytes.empty()) h.Update(bytes.data(), bytes.size());
  auto digest = h.Finalize();
  std::array<std::uint8_t, 16> out{};
  std::memcpy(out.data(), digest.data(), 16);
  // Stamp UUID variant (RFC 4122 v4 style: high nibble of byte 6 = 4,
  // two top bits of byte 8 = 10).  Pure-content UUIDs are not strictly
  // required to follow RFC 4122 but Apple's loader is happier when they
  // do, and tools like `dwarfdump` print them more cleanly.
  out[6] = static_cast<std::uint8_t>((out[6] & 0x0F) | 0x40);
  out[8] = static_cast<std::uint8_t>((out[8] & 0x3F) | 0x80);
  return out;
}

// ===========================================================================
// BuildPrintlnSequenceMachO  (companion of pe::BuildPrintlnSequencePE)
// ===========================================================================
//
// We synthesise a self-contained `__text` payload that talks directly to
// the BSD syscall interface (`write`, `exit`).  The payload is split into
// two regions packed back-to-back:
//
//   [0]                    code block: per-message write(2) call(s) +
//                          exit(0) epilogue.
//   [code_size]            inlined message bytes, concatenated in call
//                          order (deduplicated to preserve the IR-layer
//                          interning contract).
//
// Per-message addressing uses ADR (arm64) or `lea rsi, [rip+disp32]`
// (x86_64), both fully PC-relative — the resulting image is position-
// independent and survives ASLR without any runtime fixup.

namespace {

// AArch64 instruction encoders ----------------------------------------------

// MOVZ Xd, #imm16, LSL #0
std::uint32_t Arm64MovzX(unsigned rd, std::uint16_t imm16) {
  return 0xD2800000u | (static_cast<std::uint32_t>(imm16) << 5) | (rd & 0x1Fu);
}

// MOVK Xd, #imm16, LSL #(shift*16) — used only when a value exceeds 16 bits.
std::uint32_t Arm64MovkX(unsigned rd, std::uint16_t imm16, unsigned shift) {
  return 0xF2800000u | ((shift & 0x3u) << 21) |
         (static_cast<std::uint32_t>(imm16) << 5) | (rd & 0x1Fu);
}

// ADR Xd, #imm21 (signed byte offset).  imm21 must fit in [-2^20, 2^20-1].
std::uint32_t Arm64Adr(unsigned rd, std::int32_t imm21) {
  std::uint32_t u   = static_cast<std::uint32_t>(imm21) & 0x1FFFFFu;
  std::uint32_t lo  = u & 0x3u;
  std::uint32_t hi  = (u >> 2) & 0x7FFFFu;
  return 0x10000000u | (lo << 29) | (hi << 5) | (rd & 0x1Fu);
}

// SVC #0x80
std::uint32_t Arm64Svc80() { return 0xD4001001u; }

void Emit32LE(std::vector<std::uint8_t> &out, std::uint32_t v) {
  for (int i = 0; i < 4; ++i)
    out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
}

// Load a 64-bit immediate into Xd using up to four MOVZ/MOVK pieces.
// Generates the minimum number of instructions for the value at hand
// (a single MOVZ for values ≤ 0xFFFF, etc.) but always pads with NOPs
// to the maximum width so the per-call code-block size is constant.
void EmitArm64MovImm64(std::vector<std::uint8_t> &out, unsigned rd,
                       std::uint64_t value, unsigned width_words) {
  bool emitted = false;
  for (unsigned w = 0; w < width_words; ++w) {
    std::uint16_t piece = static_cast<std::uint16_t>((value >> (w * 16)) & 0xFFFFu);
    if (w == 0) {
      Emit32LE(out, Arm64MovzX(rd, piece));
      emitted = true;
    } else if (piece != 0) {
      Emit32LE(out, Arm64MovkX(rd, piece, w));
    } else {
      Emit32LE(out, 0xD503201Fu); // NOP — keep block size constant
    }
    (void)emitted;
  }
}

// Per-message arm64 code-block size (in bytes).  Layout:
//   ADR x1, #msg     ; 4
//   MOVZ x0, #1      ; 4   fd = stdout
//   MOVZ x2, #len    ; 4   length (assumes len ≤ 0xFFFF, plenty for PRINTLN)
//   MOVZ x16, #4     ; 4   syscall number = SYS_write
//   SVC  #0x80       ; 4
constexpr std::size_t kArm64PerMsgSize = 5 * 4;

// arm64 exit(0) epilogue size:
//   MOVZ x0, #0
//   MOVZ x16, #1
//   SVC  #0x80
constexpr std::size_t kArm64EpilogueSize = 3 * 4;

// x86_64 instruction encoders ----------------------------------------------
//   mov edi, 1                 5  bf 01 00 00 00
//   lea rsi, [rip + disp32]    7  48 8d 35 dd dd dd dd
//   mov edx, len32             5  ba ll ll ll ll
//   mov eax, 0x2000004         5  b8 04 00 00 02
//   syscall                    2  0f 05
constexpr std::size_t kX86_64PerMsgSize = 5 + 7 + 5 + 5 + 2;
//   xor edi, edi               2  31 ff
//   mov eax, 0x2000001         5  b8 01 00 00 02
//   syscall                    2  0f 05
constexpr std::size_t kX86_64EpilogueSize = 2 + 5 + 2;

void EmitX86MovEdiImm(std::vector<std::uint8_t> &out, std::uint32_t v) {
  out.push_back(0xBF); // mov edi, imm32
  for (int i = 0; i < 4; ++i)
    out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
}

void EmitX86MovEdxImm(std::vector<std::uint8_t> &out, std::uint32_t v) {
  out.push_back(0xBA); // mov edx, imm32
  for (int i = 0; i < 4; ++i)
    out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
}

void EmitX86MovEaxImm(std::vector<std::uint8_t> &out, std::uint32_t v) {
  out.push_back(0xB8); // mov eax, imm32
  for (int i = 0; i < 4; ++i)
    out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
}

void EmitX86Syscall(std::vector<std::uint8_t> &out) {
  out.push_back(0x0F);
  out.push_back(0x05);
}

void EmitX86LeaRsiRipDisp32(std::vector<std::uint8_t> &out, std::int32_t disp) {
  // REX.W + 8D /6 — `lea rsi, [rip + disp32]`
  out.push_back(0x48);
  out.push_back(0x8D);
  out.push_back(0x35);
  for (int i = 0; i < 4; ++i)
    out.push_back(static_cast<std::uint8_t>((disp >> (i * 8)) & 0xFF));
}

void EmitX86XorEdiEdi(std::vector<std::uint8_t> &out) {
  out.push_back(0x31);
  out.push_back(0xFF);
}

} // namespace

std::vector<std::uint8_t>
BuildPrintlnSequenceMachO(MachOArch arch,
                          const std::vector<std::string> &messages) {
  // Defensive cap: any single PRINTLN payload longer than 64 KiB does not
  // fit in the single-MOVZ length encoding we emit on arm64.  Returning
  // an empty payload signals "synthesis declined" to the caller, which
  // then falls back to the generic exit-zero path.
  for (const auto &m : messages) {
    if (m.size() > 0xFFFFu) return {};
  }

  // Dedupe message payloads so identical PRINTLNs share a single inline
  // copy.  `unique_offsets[content] = offset within the inline message
  // blob`.  Linear scan is fine — PRINTLN counts in real programs are
  // tractable and this avoids dragging in <unordered_map>.
  std::vector<std::uint8_t> blob;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> per_call; // (off, len)
  per_call.reserve(messages.size());
  std::vector<std::pair<std::string, std::uint32_t>> unique_table;
  for (const auto &raw : messages) {
    // PRINTLN semantics: append a newline to every message — the IR
    // interner stores raw payload only and the runtime helper
    // `polyrt_println` would normally tack on `\n` at call time.
    // Synthesising a syscall-direct path bypasses the helper, so we
    // bake the newline straight into the inlined blob.
    std::string m = raw;
    if (m.empty() || m.back() != '\n') m.push_back('\n');
    bool found = false;
    std::uint32_t off = 0;
    for (const auto &kv : unique_table) {
      if (kv.first == m) { off = kv.second; found = true; break; }
    }
    if (!found) {
      off = static_cast<std::uint32_t>(blob.size());
      blob.insert(blob.end(), m.begin(), m.end());
      unique_table.emplace_back(m, off);
    }
    per_call.emplace_back(off, static_cast<std::uint32_t>(m.size()));
  }

  std::vector<std::uint8_t> text;

  if (arch == MachOArch::kArm64) {
    const std::size_t code_size =
        kArm64PerMsgSize * per_call.size() + kArm64EpilogueSize;
    text.reserve(code_size + blob.size());

    // Per-message blocks — addressing uses ADR with a signed 21-bit
    // byte offset, so each message must lie within ±1 MiB of its ADR
    // instruction.  PRINTLN-only programs never come close to that.
    for (std::size_t i = 0; i < per_call.size(); ++i) {
      const std::size_t adr_pos = i * kArm64PerMsgSize;
      const std::size_t msg_abs = code_size + per_call[i].first;
      const std::int32_t delta  = static_cast<std::int32_t>(msg_abs - adr_pos);
      Emit32LE(text, Arm64Adr(/*rd=*/1, delta));
      EmitArm64MovImm64(text, /*rd=*/0, /*value=*/1, /*width=*/1);
      EmitArm64MovImm64(text, /*rd=*/2, per_call[i].second, /*width=*/1);
      EmitArm64MovImm64(text, /*rd=*/16, /*value=*/4, /*width=*/1);
      Emit32LE(text, Arm64Svc80());
    }
    // exit(0) epilogue
    EmitArm64MovImm64(text, /*rd=*/0, /*value=*/0, /*width=*/1);
    EmitArm64MovImm64(text, /*rd=*/16, /*value=*/1, /*width=*/1);
    Emit32LE(text, Arm64Svc80());
  } else {
    const std::size_t code_size =
        kX86_64PerMsgSize * per_call.size() + kX86_64EpilogueSize;
    text.reserve(code_size + blob.size());

    for (std::size_t i = 0; i < per_call.size(); ++i) {
      const std::size_t block_pos = i * kX86_64PerMsgSize;
      // The displacement encoded in `lea rsi, [rip+disp32]` is relative
      // to the *next* instruction, i.e. the address right after the
      // 7-byte LEA.  The LEA itself sits 5 bytes into the per-call
      // block (after `mov edi, 1`).
      const std::size_t lea_end_pos = block_pos + 5 + 7;
      const std::size_t msg_abs     = code_size + per_call[i].first;
      const std::int32_t disp       = static_cast<std::int32_t>(msg_abs - lea_end_pos);
      EmitX86MovEdiImm(text, 1);          // fd = stdout
      EmitX86LeaRsiRipDisp32(text, disp); // buf = &msg
      EmitX86MovEdxImm(text, per_call[i].second);
      EmitX86MovEaxImm(text, 0x2000004u); // SYS_write
      EmitX86Syscall(text);
    }
    // exit(0) epilogue
    EmitX86XorEdiEdi(text);
    EmitX86MovEaxImm(text, 0x2000001u);   // SYS_exit
    EmitX86Syscall(text);
  }

  // Tail: inlined message bytes (raw, no terminator — the syscall is
  // bounded by the explicit length register).
  text.insert(text.end(), blob.begin(), blob.end());
  return text;
}

bool TranslateRelocations(MachOArch arch,
                          const std::vector<PendingRelocation> &input,
                          std::vector<OnDiskRelocation> &out,
                          std::vector<std::string> &errors_out) {
  bool ok = true;
  for (const auto &r : input) {
    bool type_valid = false;
    if (arch == MachOArch::kX86_64) {
      switch (static_cast<X86_64Reloc>(r.type)) {
        case X86_64Reloc::kUnsigned:
        case X86_64Reloc::kSigned:
        case X86_64Reloc::kBranch:
        case X86_64Reloc::kGotLoad:
        case X86_64Reloc::kGot:
        case X86_64Reloc::kSubtractor:
        case X86_64Reloc::kSigned1:
        case X86_64Reloc::kSigned2:
        case X86_64Reloc::kSigned4:
        case X86_64Reloc::kTlv:
          type_valid = true;
          break;
      }
    } else { // kArm64
      switch (static_cast<Arm64Reloc>(r.type)) {
        case Arm64Reloc::kUnsigned:
        case Arm64Reloc::kSubtractor:
        case Arm64Reloc::kBranch26:
        case Arm64Reloc::kPage21:
        case Arm64Reloc::kPageoff12:
        case Arm64Reloc::kGotLoadPage21:
        case Arm64Reloc::kGotLoadPageoff12:
        case Arm64Reloc::kPointerToGot:
        case Arm64Reloc::kTlvpPage21:
        case Arm64Reloc::kTlvpPageoff12:
        case Arm64Reloc::kAddend:
          type_valid = true;
          break;
      }
    }
    if (!type_valid) {
      std::ostringstream os;
      os << "polyld-err-E3220: unknown Mach-O relocation type "
         << static_cast<unsigned>(r.type)
         << " at section offset 0x" << std::hex << r.address;
      errors_out.push_back(os.str());
      ok = false;
      continue;
    }
    OnDiskRelocation d{};
    d.r_address = r.address;
    // Packed 32-bit field layout (little-endian on disk):
    //   bits  0..23 : r_symbolnum (24)
    //   bit  24     : r_pcrel
    //   bits 25..26 : r_length
    //   bit  27     : r_extern
    //   bits 28..31 : r_type
    std::uint32_t packed = 0;
    packed |= (r.symbol_index & 0x00FFFFFFu);
    packed |= (r.is_pc_relative ? 1u : 0u) << 24;
    packed |= (static_cast<std::uint32_t>(r.length & 0x3u)) << 25;
    packed |= (r.is_extern ? 1u : 0u) << 27;
    packed |= (static_cast<std::uint32_t>(r.type & 0xFu)) << 28;
    d.r_packed = packed;
    out.push_back(d);
  }
  return ok;
}

// ===========================================================================
// BuildMachOImage
// ===========================================================================

namespace {

// Compute the size in bytes of a single LC_SEGMENT_64 command including
// its trailing section headers (one segment header is 72 bytes + 80 per
// section).
std::uint32_t Lc64Size(std::size_t nsections) {
  return 72u + 80u * static_cast<std::uint32_t>(nsections);
}

void EmitSegmentCommand(std::vector<std::uint8_t> &cmds,
                        const std::string &segname,
                        std::uint64_t vmaddr, std::uint64_t vmsize,
                        std::uint64_t fileoff, std::uint64_t filesize,
                        std::uint32_t maxprot, std::uint32_t initprot,
                        std::uint32_t nsects, std::uint32_t flags) {
  PutU32(cmds, kLcSegment64);
  PutU32(cmds, Lc64Size(nsects));
  PutFixedString(cmds, segname, 16);
  PutU64(cmds, vmaddr);
  PutU64(cmds, vmsize);
  PutU64(cmds, fileoff);
  PutU64(cmds, filesize);
  PutU32(cmds, maxprot);
  PutU32(cmds, initprot);
  PutU32(cmds, nsects);
  PutU32(cmds, flags);
}

void EmitSectionHeader(std::vector<std::uint8_t> &cmds,
                       const SectionDesc &s,
                       std::uint64_t addr, std::uint64_t size,
                       std::uint32_t fileoff) {
  PutFixedString(cmds, s.sectname, 16);
  PutFixedString(cmds, s.segname, 16);
  PutU64(cmds, addr);
  PutU64(cmds, size);
  PutU32(cmds, fileoff);
  PutU32(cmds, s.alignment_log2);
  PutU32(cmds, 0); // reloff (no per-section relocs in the linked image)
  PutU32(cmds, 0); // nreloc
  PutU32(cmds, s.flags);
  PutU32(cmds, 0); // reserved1
  PutU32(cmds, 0); // reserved2
  PutU32(cmds, 0); // reserved3
}

void EmitDylinkerCommand(std::vector<std::uint8_t> &cmds,
                         const std::string &path) {
  // LC layout: cmd, cmdsize, name_off, then name bytes (NUL-terminated,
  // padded so the *whole* load command is a multiple of 8 bytes).
  // cmdsize must therefore be computed as AlignUp(12 + path + NUL, 8)
  // and match the precomputed `cmds_size` accounting in the layout
  // phase; computing the pad against `path.size()+1` alone leaves the
  // 12-byte preamble out of the alignment and produces a cmdsize four
  // bytes shy of the actually-written length, which de-syncs the
  // dynamic loader's load-command walker for every subsequent LC.
  const std::uint32_t name_off = 12;
  const std::uint32_t cmdsize  = static_cast<std::uint32_t>(
      AlignUp(12 + path.size() + 1, 8));
  PutU32(cmds, kLcLoadDylinker);
  PutU32(cmds, cmdsize);
  PutU32(cmds, name_off);
  for (char c : path) cmds.push_back(static_cast<std::uint8_t>(c));
  cmds.push_back(0);
  while (cmds.size() % 8 != 0) cmds.push_back(0);
}

void EmitDylibCommand(std::vector<std::uint8_t> &cmds, std::uint32_t cmd,
                      const std::string &path,
                      std::uint32_t timestamp,
                      std::uint32_t current_version,
                      std::uint32_t compat_version) {
  // Layout: cmd, cmdsize, name_off, timestamp, current, compat, name.
  std::uint32_t name_off = 24;
  std::uint32_t name_pad =
      static_cast<std::uint32_t>(AlignUp(path.size() + 1, 8));
  std::uint32_t cmdsize = 24 + name_pad;
  PutU32(cmds, cmd);
  PutU32(cmds, cmdsize);
  PutU32(cmds, name_off);
  PutU32(cmds, timestamp);
  PutU32(cmds, current_version);
  PutU32(cmds, compat_version);
  for (char c : path) cmds.push_back(static_cast<std::uint8_t>(c));
  cmds.push_back(0);
  while (cmds.size() % 8 != 0) cmds.push_back(0);
}

void EmitSymtabCommand(std::vector<std::uint8_t> &cmds,
                       std::uint32_t symoff, std::uint32_t nsyms,
                       std::uint32_t stroff, std::uint32_t strsize) {
  PutU32(cmds, kLcSymtab);
  PutU32(cmds, 24);
  PutU32(cmds, symoff);
  PutU32(cmds, nsyms);
  PutU32(cmds, stroff);
  PutU32(cmds, strsize);
}

void EmitDysymtabCommand(std::vector<std::uint8_t> &cmds,
                         std::uint32_t ilocalsym, std::uint32_t nlocalsym,
                         std::uint32_t iextdefsym, std::uint32_t nextdefsym,
                         std::uint32_t iundefsym, std::uint32_t nundefsym,
                         std::uint32_t indirectsymoff,
                         std::uint32_t nindirectsyms) {
  PutU32(cmds, kLcDysymtab);
  PutU32(cmds, 80); // cmdsize
  PutU32(cmds, ilocalsym);
  PutU32(cmds, nlocalsym);
  PutU32(cmds, iextdefsym);
  PutU32(cmds, nextdefsym);
  PutU32(cmds, iundefsym);
  PutU32(cmds, nundefsym);
  // tocoff / ntoc / modtaboff / nmodtab / extrefsymoff / nextrefsyms
  for (int i = 0; i < 6; ++i) PutU32(cmds, 0);
  PutU32(cmds, indirectsymoff);
  PutU32(cmds, nindirectsyms);
  PutU32(cmds, 0); // extreloff
  PutU32(cmds, 0); // nextrel
  PutU32(cmds, 0); // locreloff
  PutU32(cmds, 0); // nlocrel
}

void EmitBuildVersionCommand(std::vector<std::uint8_t> &cmds,
                             std::uint32_t platform,
                             std::array<std::uint8_t, 3> minos,
                             std::array<std::uint8_t, 3> sdk) {
  PutU32(cmds, kLcBuildVer);
  PutU32(cmds, 24); // cmdsize, ntools=0
  PutU32(cmds, platform);
  PutU32(cmds, (static_cast<std::uint32_t>(minos[0]) << 16) |
               (static_cast<std::uint32_t>(minos[1]) <<  8) |
                static_cast<std::uint32_t>(minos[2]));
  PutU32(cmds, (static_cast<std::uint32_t>(sdk[0]) << 16) |
               (static_cast<std::uint32_t>(sdk[1]) <<  8) |
                static_cast<std::uint32_t>(sdk[2]));
  PutU32(cmds, 0); // ntools
}

void EmitSourceVersionCommand(std::vector<std::uint8_t> &cmds,
                              std::uint64_t version) {
  PutU32(cmds, kLcSourceVer);
  PutU32(cmds, 16);
  PutU64(cmds, version);
}

void EmitUuidCommand(std::vector<std::uint8_t> &cmds,
                     const std::array<std::uint8_t, 16> &uuid) {
  PutU32(cmds, kLcUuid);
  PutU32(cmds, 24);
  for (auto b : uuid) cmds.push_back(b);
}

void EmitMainCommand(std::vector<std::uint8_t> &cmds,
                     std::uint64_t entryoff, std::uint64_t stack_size) {
  PutU32(cmds, kLcMain);
  PutU32(cmds, 24);
  PutU64(cmds, entryoff);
  PutU64(cmds, stack_size);
}

void EmitCodeSignatureCommand(std::vector<std::uint8_t> &cmds,
                              std::uint32_t dataoff,
                              std::uint32_t datasize) {
  PutU32(cmds, kLcCodeSig);
  PutU32(cmds, 16);
  PutU32(cmds, dataoff);
  PutU32(cmds, datasize);
}

} // namespace

BuildResult BuildMachOImage(const BuildRequest &req) {
  BuildResult out;
  if (req.segments.empty() && req.filetype == kFileTypeExecute) {
    return out; // executable with no code is meaningless
  }
  if (req.filetype == kFileTypeDylib && req.install_name.empty()) {
    return out; // dylibs must carry an install name
  }

  // -------------------------------------------------------------------------
  // Layout phase 1: compute load-command size so we know where the header
  // padding ends and the first segment's raw bytes begin.
  // -------------------------------------------------------------------------

  // Always-emitted load command count baseline:
  //   __PAGEZERO (executables only) + per-user-segment LC_SEGMENT_64
  //   + __LINKEDIT segment + LC_DYLD_INFO_ONLY skipped (we do classic syms)
  //   + LC_SYMTAB + LC_DYSYMTAB
  //   + LC_LOAD_DYLINKER + LC_UUID + LC_BUILD_VERSION + LC_SOURCE_VERSION
  //   + LC_MAIN (executable) or LC_ID_DYLIB (dylib)
  //   + LC_LOAD_DYLIB (libSystem) when emit_dyld_info
  //   + LC_CODE_SIGNATURE (optional)
  std::uint32_t cmds_size = 0;
  std::uint32_t ncmds = 0;
  const bool emit_pagezero = req.emit_pagezero &&
                             req.filetype == kFileTypeExecute;
  if (emit_pagezero) {
    cmds_size += Lc64Size(0); // __PAGEZERO has no sections
    ++ncmds;
  }
  for (const auto &seg : req.segments) {
    cmds_size += Lc64Size(seg.sections.size());
    ++ncmds;
  }
  cmds_size += Lc64Size(0); // __LINKEDIT
  ++ncmds;
  // LC_DYLD_CHAINED_FIXUPS — modern macOS / Apple Silicon kernels reject
  // executables that lack a chained-fixups blob even when the binary has
  // no actual imports.  Emit an empty blob (zero imports, zero per-page
  // fixup records) just so the load-command is present.
  cmds_size += 16; ++ncmds;             // LC_DYLD_CHAINED_FIXUPS
  cmds_size += 16; ++ncmds;             // LC_DYLD_EXPORTS_TRIE
  cmds_size += 16; ++ncmds;             // LC_FUNCTION_STARTS
  cmds_size += 16; ++ncmds;             // LC_DATA_IN_CODE
  cmds_size += 24; ++ncmds;             // LC_SYMTAB
  cmds_size += 80; ++ncmds;             // LC_DYSYMTAB
  if (req.emit_dyld_info) {
    cmds_size += static_cast<std::uint32_t>(
        AlignUp(12 + std::strlen("/usr/lib/dyld") + 1, 8));
    ++ncmds; // LC_LOAD_DYLINKER
    cmds_size += static_cast<std::uint32_t>(
        AlignUp(24 + std::strlen("/usr/lib/libSystem.B.dylib") + 1, 8));
    ++ncmds; // LC_LOAD_DYLIB libSystem
  }
  cmds_size += 24; ++ncmds;             // LC_UUID
  cmds_size += 24; ++ncmds;             // LC_BUILD_VERSION (ntools=0)
  cmds_size += 16; ++ncmds;             // LC_SOURCE_VERSION
  if (req.filetype == kFileTypeExecute) {
    cmds_size += 24; ++ncmds;           // LC_MAIN
  } else if (req.filetype == kFileTypeDylib) {
    cmds_size += static_cast<std::uint32_t>(
        AlignUp(24 + req.install_name.size() + 1, 8));
    ++ncmds; // LC_ID_DYLIB
  }
  if (req.emit_code_signature) {
    cmds_size += 16; ++ncmds;           // LC_CODE_SIGNATURE
  }

  const std::uint64_t kPageSize     = PageSizeForArch(req.arch);
  const std::uint64_t header_size   = 32 + cmds_size;
  const std::uint64_t text_file_off = AlignUp(header_size, kPageSize);

  // -------------------------------------------------------------------------
  // Layout phase 2: assign file offsets / vmaddrs to each user segment.
  // -------------------------------------------------------------------------

  std::uint64_t base = req.base_address;
  if (base == 0) {
    base = (req.filetype == kFileTypeExecute) ? kDefaultExeBase : 0;
  }

  struct SegLayout {
    std::uint64_t vmaddr;
    std::uint64_t vmsize;
    std::uint64_t fileoff;
    std::uint64_t filesize;
    std::vector<std::uint64_t> sect_addr;
    std::vector<std::uint64_t> sect_size;
    std::vector<std::uint32_t> sect_fileoff;
  };

  // Place __PAGEZERO at vmaddr 0 spanning the whole low 4GB on
  // executables; it carries no file bytes.
  std::uint64_t cur_vm = base;
  std::uint64_t cur_file = text_file_off;

  std::vector<SegLayout> layouts;
  for (std::size_t si = 0; si < req.segments.size(); ++si) {
    const auto &seg = req.segments[si];
    SegLayout L{};
    L.vmaddr  = cur_vm;
    L.fileoff = (si == 0) ? 0 : cur_file; // first segment covers header
    std::uint64_t in_seg = (si == 0) ? header_size : 0;
    for (const auto &sec : seg.sections) {
      const std::uint64_t a = std::uint64_t{1} << sec.alignment_log2;
      in_seg = AlignUp(in_seg, a);
      L.sect_fileoff.push_back(static_cast<std::uint32_t>(L.fileoff + in_seg));
      L.sect_addr.push_back(L.vmaddr + in_seg);
      L.sect_size.push_back(sec.data.size());
      in_seg += sec.data.size();
    }
    L.filesize = (seg.filesize_override != 0) ? seg.filesize_override
                                              : AlignUp(in_seg, kPageSize);
    L.vmsize   = (seg.vmsize_override != 0)   ? seg.vmsize_override
                                              : L.filesize;
    layouts.push_back(L);
    cur_vm   += L.vmsize;
    cur_file += L.filesize;
    if (si == 0) cur_file = L.fileoff + L.filesize; // ensure no gap
  }

  // __LINKEDIT comes after every user segment.  Its content is built
  // below; for now we just reserve the file offset.
  const std::uint64_t linkedit_fileoff = cur_file;
  const std::uint64_t linkedit_vmaddr  = cur_vm;

  // -------------------------------------------------------------------------
  // Build __LINKEDIT bytes: nlist_64 array, then string table, then
  // (optional) ad-hoc code-signature superblob placeholder.
  // -------------------------------------------------------------------------

  std::vector<std::uint8_t> nlist_bytes;
  std::vector<std::uint8_t> string_bytes;
  string_bytes.push_back(0); // index 0 reserved for the empty string
  std::vector<std::uint32_t> name_offsets;
  name_offsets.reserve(req.symbols.size());
  for (const auto &sym : req.symbols) {
    name_offsets.push_back(static_cast<std::uint32_t>(string_bytes.size()));
    for (char c : sym.name) string_bytes.push_back(static_cast<std::uint8_t>(c));
    string_bytes.push_back(0);
  }
  // Pad string table to 8-byte boundary so following data stays aligned.
  while (string_bytes.size() % 8 != 0) string_bytes.push_back(0);

  for (std::size_t i = 0; i < req.symbols.size(); ++i) {
    const auto &sym = req.symbols[i];
    PutU32(nlist_bytes, name_offsets[i]); // n_strx
    nlist_bytes.push_back(sym.n_type);
    nlist_bytes.push_back(sym.n_sect);
    PutU16(nlist_bytes, sym.n_desc);
    PutU64(nlist_bytes, sym.n_value);
  }

  // ---------------------------------------------------------------------
  // LC_DYLD_CHAINED_FIXUPS payload — a minimal but well-formed
  // dyld_chained_fixups_header followed by an empty
  // dyld_chained_starts_in_image (one zeroed seg_info_offset per total
  // segment in the image).  Modern macOS / Apple Silicon kernels reject
  // executables that lack this blob, even when the binary has zero
  // imports.  Layout: header (32 B) + starts_in_image (4 B seg_count +
  // 4 B per segment) padded to 8 B; imports / symbols sub-blobs are
  // empty (count=0) and share the trailing offset.
  // ---------------------------------------------------------------------
  std::vector<std::uint8_t> chained_bytes;
  std::uint32_t chained_size = 0;
  {
    const std::uint32_t total_segs =
        static_cast<std::uint32_t>((emit_pagezero ? 1u : 0u) +
                                    req.segments.size() + 1u /*__LINKEDIT*/);
    const std::uint32_t header_sz = 32;
    std::uint32_t starts_sz = 4 + 4 * total_segs;
    starts_sz = static_cast<std::uint32_t>(AlignUp(starts_sz, 8));
    chained_size = header_sz + starts_sz;
    chained_size = static_cast<std::uint32_t>(AlignUp(chained_size, 8));

    chained_bytes.reserve(chained_size);
    PutU32(chained_bytes, 0);                  // fixups_version
    PutU32(chained_bytes, header_sz);          // starts_offset
    PutU32(chained_bytes, header_sz + starts_sz); // imports_offset
    PutU32(chained_bytes, header_sz + starts_sz); // symbols_offset
    PutU32(chained_bytes, 0);                  // imports_count
    PutU32(chained_bytes, 1);                  // imports_format = DYLD_CHAINED_IMPORT
    PutU32(chained_bytes, 0);                  // symbols_format
    PutU32(chained_bytes, 0);                  // padding (header is 32 B)
    PutU32(chained_bytes, total_segs);         // seg_count
    for (std::uint32_t i = 0; i < total_segs; ++i)
      PutU32(chained_bytes, 0);                // seg_info_offset[i] = 0
    while (chained_bytes.size() < chained_size) chained_bytes.push_back(0);
  }

  const std::uint32_t chained_fileoff =
      static_cast<std::uint32_t>(linkedit_fileoff);
  // ----- LC_DYLD_EXPORTS_TRIE payload ------------------------------------
  // For executables we materialise a real trie carrying `_main` (regular
  // export, address = LC_MAIN.entryoff + __TEXT.fileoff) and
  // `_mh_execute_header` (regular export, address = 0).  AMFI /
  // CoreTrust / AppleSystemPolicy on macOS 26 (Tahoe) refuse to map an
  // image whose exports trie does not yield a real `_main`.  Dylibs and
  // bundles still get the minimal placeholder used pre-1.43.0.
  std::uint64_t lc_main_entryoff_value = 0;
  std::uint64_t main_export_address    = 0;
  if (req.filetype == kFileTypeExecute) {
    const std::uint64_t section_off_in_segment =
        layouts.empty() || layouts.front().sect_fileoff.empty()
            ? text_file_off
            : layouts.front().sect_fileoff.front();
    lc_main_entryoff_value = section_off_in_segment + req.entry_offset;
    // __TEXT.fileoff is 0 for the first user segment (it covers the
    // mach-header pages), so the spec formula `LC_MAIN.entryoff +
    // __TEXT.fileoff` collapses to `lc_main_entryoff_value`.
    const std::uint64_t text_fileoff =
        layouts.empty() ? 0u : layouts.front().fileoff;
    main_export_address = lc_main_entryoff_value + text_fileoff;
  }

  std::vector<std::uint8_t> exports_trie_bytes;
  if (req.filetype == kFileTypeExecute) {
    exports_trie_bytes = BuildExecutableExportsTrie(main_export_address);
  } else {
    exports_trie_bytes.push_back(0);
    while (exports_trie_bytes.size() % 8 != 0) exports_trie_bytes.push_back(0);
  }
  const std::uint32_t exports_trie_size =
      static_cast<std::uint32_t>(exports_trie_bytes.size());
  const std::uint32_t exports_trie_fileoff = chained_fileoff + chained_size;
  // ----- LC_FUNCTION_STARTS payload --------------------------------------
  // Single-function MH_EXECUTE: payload = ULEB128(LC_MAIN.entryoff) +
  // 0x00 terminator, padded to 8 bytes.  Multi-function support is
  // already plumbed through the underlying helper via the
  // `starts_in_order` vector.
  std::vector<std::uint8_t> function_starts_bytes;
  if (req.filetype == kFileTypeExecute) {
    function_starts_bytes =
        BuildFunctionStartsBlob({lc_main_entryoff_value});
  } else {
    function_starts_bytes.push_back(0);
    while (function_starts_bytes.size() % 8 != 0)
      function_starts_bytes.push_back(0);
  }
  const std::uint32_t function_starts_size =
      static_cast<std::uint32_t>(function_starts_bytes.size());
  const std::uint32_t function_starts_fileoff =
      exports_trie_fileoff + exports_trie_size;
  // LC_DATA_IN_CODE payload is empty (datasize == 0) — there is no
  // mid-text constant-pool literal that would require a marker entry.
  const std::uint32_t data_in_code_size = 0;
  const std::uint32_t data_in_code_fileoff =
      function_starts_fileoff + function_starts_size;
  const std::uint32_t symtab_fileoff = data_in_code_fileoff + data_in_code_size;
  const std::uint32_t symtab_size =
      static_cast<std::uint32_t>(nlist_bytes.size());
  const std::uint32_t strtab_fileoff = symtab_fileoff + symtab_size;
  const std::uint32_t strtab_size =
      static_cast<std::uint32_t>(string_bytes.size());

  // Optional ad-hoc code-signature region.  We reserve a 4096-byte
  // window and emit a CodeDirectory-shaped header so `codesign` can
  // overwrite it in-place without resizing the image.
  std::vector<std::uint8_t> codesig_bytes;
  std::uint32_t codesig_fileoff = 0;
  std::uint32_t codesig_size = 0;
  if (req.emit_code_signature) {
    codesig_fileoff = strtab_fileoff + strtab_size;
    codesig_fileoff = static_cast<std::uint32_t>(AlignUp(codesig_fileoff, 16));
    codesig_size    = 4096; // headroom for an ad-hoc CMS blob
    codesig_bytes.resize(codesig_size, 0);
    // SuperBlob magic = 0xfade0cc0, length = 8 (just the header for
    // now).  This is the bare minimum that `codesign --display` will
    // accept as a "this image has *something* called a signature"
    // marker; a real signature is injected post-link.
    codesig_bytes[0] = 0xC0; codesig_bytes[1] = 0x0C;
    codesig_bytes[2] = 0xDE; codesig_bytes[3] = 0xFA;
    codesig_bytes[4] = 0; codesig_bytes[5] = 0;
    codesig_bytes[6] = 0; codesig_bytes[7] = 8;
    codesig_bytes[8] = 0; codesig_bytes[9] = 0;
    codesig_bytes[10] = 0; codesig_bytes[11] = 0; // count = 0
  }

  const std::uint64_t linkedit_filesize_raw =
      chained_size + exports_trie_size + function_starts_size +
      data_in_code_size + symtab_size + strtab_size +
      (req.emit_code_signature ? (codesig_fileoff - (strtab_fileoff + strtab_size) +
                                   codesig_size)
                                : 0);
  // The signed image must end exactly where LC_CODE_SIGNATURE ends —
  // AMFI/CT rejects binaries with trailing padding past the signature.
  // VM mapping still requires a page-aligned vmsize, but filesize can be
  // the exact tail offset.
  const std::uint64_t linkedit_filesize = std::max<std::uint64_t>(linkedit_filesize_raw, 1);
  const std::uint64_t linkedit_vmsize =
      AlignUp(linkedit_filesize, kPageSize);

  // -------------------------------------------------------------------------
  // Layout phase 3: emit load commands now that all offsets are known.
  // -------------------------------------------------------------------------

  std::vector<std::uint8_t> cmds;
  cmds.reserve(cmds_size);

  if (emit_pagezero) {
    EmitSegmentCommand(cmds, "__PAGEZERO",
                       0, kPageZeroSize64, 0, 0,
                       /*maxprot*/ 0, /*initprot*/ 0,
                       /*nsects*/ 0, /*flags*/ 0);
  }

  for (std::size_t si = 0; si < req.segments.size(); ++si) {
    const auto &seg = req.segments[si];
    const auto &L   = layouts[si];
    EmitSegmentCommand(cmds, seg.segname,
                       L.vmaddr, L.vmsize, L.fileoff, L.filesize,
                       seg.maxprot, seg.initprot,
                       static_cast<std::uint32_t>(seg.sections.size()),
                       seg.flags);
    for (std::size_t ki = 0; ki < seg.sections.size(); ++ki) {
      EmitSectionHeader(cmds, seg.sections[ki],
                        L.sect_addr[ki], L.sect_size[ki],
                        L.sect_fileoff[ki]);
    }
  }

  EmitSegmentCommand(cmds, "__LINKEDIT",
                     linkedit_vmaddr, linkedit_vmsize,
                     linkedit_fileoff, linkedit_filesize,
                     kVmProtRead, kVmProtRead,
                     0, 0);

  // LC_DYLD_CHAINED_FIXUPS — emitted before LC_SYMTAB so dyld picks up
  // the chained-fixups blob first; required by Apple Silicon kernels
  // even when the binary has no actual imports.
  {
    PutU32(cmds, kLcDyldChainedFixups);
    PutU32(cmds, 16);
    PutU32(cmds, chained_fileoff);
    PutU32(cmds, chained_size);
  }
  // LC_DYLD_EXPORTS_TRIE — empty trie blob; CoreTrust expects this LC
  // alongside LC_DYLD_CHAINED_FIXUPS for modern static-style binaries.
  {
    PutU32(cmds, kLcDyldExportsTrie);
    PutU32(cmds, 16);
    PutU32(cmds, exports_trie_fileoff);
    PutU32(cmds, exports_trie_size);
  }
  // LC_FUNCTION_STARTS — single ULEB terminator; matches what Apple's
  // linker emits for binaries with no per-function start records.
  {
    PutU32(cmds, kLcFunctionStarts);
    PutU32(cmds, 16);
    PutU32(cmds, function_starts_fileoff);
    PutU32(cmds, function_starts_size);
  }
  // LC_DATA_IN_CODE — empty payload; no inline literal regions to mark.
  {
    PutU32(cmds, kLcDataInCode);
    PutU32(cmds, 16);
    PutU32(cmds, data_in_code_fileoff);
    PutU32(cmds, data_in_code_size);
  }

  EmitSymtabCommand(cmds, symtab_fileoff,
                    static_cast<std::uint32_t>(req.symbols.size()),
                    strtab_fileoff, strtab_size);

  EmitDysymtabCommand(cmds,
                      0, req.local_count,
                      req.local_count, req.extdef_count,
                      req.local_count + req.extdef_count, req.undef_count,
                      0, 0);

  if (req.emit_dyld_info) {
    EmitDylinkerCommand(cmds, "/usr/lib/dyld");
    // libSystem.B.dylib: timestamp 2, current_version 1.0.0,
    // compat_version 1.0.0 mirrors what `clang -v` produces for
    // toolchain-emitted Mach-O.
    EmitDylibCommand(cmds, kLcLoadDylib,
                     "/usr/lib/libSystem.B.dylib",
                     /*timestamp*/ 2,
                     /*current*/ PackVersion32("1.0.0"),
                     /*compat*/  PackVersion32("1.0.0"));
  }

  // UUID derived from concatenated user-segment bytes so that two builds
  // with identical content get identical UUIDs (deterministic builds).
  std::vector<std::uint8_t> uuid_seed;
  for (const auto &seg : req.segments)
    for (const auto &sec : seg.sections)
      uuid_seed.insert(uuid_seed.end(), sec.data.begin(), sec.data.end());
  EmitUuidCommand(cmds, Uuid16FromContent(uuid_seed));

  EmitBuildVersionCommand(cmds, kPlatformMacOS, req.minos, req.sdk);
  EmitSourceVersionCommand(cmds, PackVersion64(req.source_version));

  if (req.filetype == kFileTypeExecute) {
    // entry_offset is given as offset from the start of the first user
    // section's bytes; LC_MAIN's `entryoff` is measured from the start
    // of the __TEXT *segment* (which is file offset 0 and includes the
    // Mach-O header pages).  Add the first section's segment-relative
    // file offset back in so dyld jumps to real instructions instead
    // of the magic-number bytes at the head of the image.
    const std::uint64_t section_off_in_segment =
        layouts.empty() || layouts.front().sect_fileoff.empty()
            ? text_file_off
            : layouts.front().sect_fileoff.front();
    EmitMainCommand(cmds, section_off_in_segment + req.entry_offset,
                    req.stack_size);
  } else if (req.filetype == kFileTypeDylib) {
    EmitDylibCommand(cmds, kLcIdDylib, req.install_name,
                     /*timestamp*/ 1,
                     /*current*/ PackVersion32("1.0.0"),
                     /*compat*/  PackVersion32("1.0.0"));
  }
  // MH_BUNDLE has no LC_MAIN and no LC_ID_DYLIB; the loader uses
  // dlopen()-style lookup of named symbols only.

  if (req.emit_code_signature) {
    EmitCodeSignatureCommand(cmds, codesig_fileoff, codesig_size);
  }

  // Some accountancy: cmds.size() must equal the precomputed cmds_size.
  // If they ever diverge the writer has miscounted and we must fail
  // loudly rather than emit a corrupt header.
  if (cmds.size() != cmds_size) {
    return BuildResult{}; // fail closed
  }

  // -------------------------------------------------------------------------
  // Layout phase 4: assemble the final image bytes.
  // -------------------------------------------------------------------------

  std::vector<std::uint8_t> image;
  image.reserve(linkedit_fileoff + linkedit_filesize);

  // mach_header_64
  PutU32(image, kMachOMagic64);
  PutU32(image, req.arch == MachOArch::kArm64 ? kCpuTypeArm64 : kCpuTypeX86_64);
  // CPU_SUBTYPE: arm64 wants CPU_SUBTYPE_ARM64_ALL (0); x86_64 wants
  // CPU_SUBTYPE_X86_ALL (3).  Picking the wrong family-subtype tuple is
  // a hard reject in dyld ("bad CPU type in executable") even when the
  // primary cputype is correct, so the two must be paired explicitly.
  PutU32(image, req.arch == MachOArch::kArm64 ? 0u : kCpuSubtypeAll);
  PutU32(image, req.filetype);
  PutU32(image, ncmds);
  PutU32(image, cmds_size);
  PutU32(image, req.header_flags);
  PutU32(image, 0); // reserved

  // load commands
  image.insert(image.end(), cmds.begin(), cmds.end());

  // The first user segment starts at file offset 0 and includes the
  // Mach-O header pages.  Its sections are placed at `sect_fileoff`
  // (which respects each section's own alignment, not page alignment),
  // so we must NOT pre-pad up to `text_file_off` — doing so would push
  // the actual section bytes past the offset recorded in the section
  // header, leaving sect_fileoff pointing at zero filler.

  // user segment payloads
  for (std::size_t si = 0; si < req.segments.size(); ++si) {
    const auto &seg = req.segments[si];
    const auto &L   = layouts[si];
    PadTo(image, L.fileoff + (si == 0 ? header_size : 0)); // first segment includes header
    for (std::size_t ki = 0; ki < seg.sections.size(); ++ki) {
      PadTo(image, L.sect_fileoff[ki]);
      const auto &data = seg.sections[ki].data;
      image.insert(image.end(), data.begin(), data.end());
    }
    PadTo(image, L.fileoff + L.filesize);
  }

  // __LINKEDIT contents
  PadTo(image, linkedit_fileoff);
  image.insert(image.end(), chained_bytes.begin(), chained_bytes.end());
  image.insert(image.end(), exports_trie_bytes.begin(), exports_trie_bytes.end());
  image.insert(image.end(), function_starts_bytes.begin(), function_starts_bytes.end());
  // data_in_code payload is empty (datasize == 0); nothing to insert.
  image.insert(image.end(), nlist_bytes.begin(), nlist_bytes.end());
  image.insert(image.end(), string_bytes.begin(), string_bytes.end());
  if (req.emit_code_signature) {
    PadTo(image, codesig_fileoff);
    image.insert(image.end(), codesig_bytes.begin(), codesig_bytes.end());
  }
  PadTo(image, linkedit_fileoff + linkedit_filesize);

  // -------------------------------------------------------------------------
  // Layout phase 5: linker-signed code-signature finalisation.  The blob
  // hashes the bytes of `image` up to `codesig_fileoff` (codeLimit) page by
  // page (4096 bytes per slot, SHA-256) and replaces the placeholder bytes
  // emitted in phase 4.  Marking the CodeDirectory `linker-signed` is what
  // tells AMFI on Apple Silicon to accept the resulting ad-hoc signature
  // without a separate `codesign --force --sign -` pass.
  // -------------------------------------------------------------------------
  if (req.emit_code_signature) {
    const std::uint64_t exec_seg_base  = layouts.front().fileoff;
    const std::uint64_t exec_seg_limit = layouts.front().filesize;
    std::string ident = req.identifier.empty() ? std::string{"polyld"} : req.identifier;
    auto sig = BuildLinkerSignedSignature(image, codesig_fileoff,
                                          exec_seg_base, exec_seg_limit,
                                          ident, codesig_size);
    if (sig.size() <= codesig_size &&
        codesig_fileoff + sig.size() <= image.size()) {
      std::copy(sig.begin(), sig.end(), image.begin() + codesig_fileoff);
    }
  }

  out.image = std::move(image);
  out.text_segment_vmaddr =
      req.segments.empty() ? 0 : layouts.front().vmaddr;
  out.entry_vmaddr = out.text_segment_vmaddr + req.entry_offset;
  out.load_commands_size = cmds_size;
  out.num_load_commands  = ncmds;
  return out;
}

} // namespace polyglot::linker::macho

// ===========================================================================
// Linker:: methods (the actual BIN-5 entry points)
// ===========================================================================

namespace polyglot::linker {

namespace {

namespace ma = ::polyglot::linker::macho;

// Translate the linker's TargetArch into the Mach-O writer's enum.
ma::MachOArch ArchFromConfig(TargetArch a) {
  return a == TargetArch::kAArch64 ? ma::MachOArch::kArm64
                                    : ma::MachOArch::kX86_64;
}

// Build the per-architecture default segment layout from the linker's
// merged `output_sections_`.  We collapse everything executable into
// `__TEXT/__text`, everything read-only-data into `__DATA_CONST/__const`,
// and everything writable into `__DATA/__data`.  This mirrors what
// Apple's static toolchain produces for small programs.
struct PartitionedBytes {
  std::vector<std::uint8_t> text;
  std::vector<std::uint8_t> rdata;
  std::vector<std::uint8_t> data;
};

PartitionedBytes PartitionSections(
    const std::vector<OutputSection> &sections) {
  PartitionedBytes p;
  for (const auto &sec : sections) {
    const bool is_exec =
        (static_cast<std::uint64_t>(sec.flags) &
         static_cast<std::uint64_t>(SectionFlags::kExecInstr)) != 0;
    const bool is_write =
        (static_cast<std::uint64_t>(sec.flags) &
         static_cast<std::uint64_t>(SectionFlags::kWrite)) != 0;
    if (sec.type == SectionType::kNobits) continue;
    if (is_exec) {
      p.text.insert(p.text.end(), sec.data.begin(), sec.data.end());
    } else if (is_write) {
      p.data.insert(p.data.end(), sec.data.begin(), sec.data.end());
    } else {
      p.rdata.insert(p.rdata.end(), sec.data.begin(), sec.data.end());
    }
  }
  return p;
}

// Common driver shared by the executable / dylib / bundle entry points.
// Operates only on the linker state pieces it actually needs so it can
// stay free of any private-member dependency.
struct EmitInputs {
  const LinkerConfig &cfg;
  const std::vector<OutputSection> &sections;
  const std::unordered_map<std::string, Symbol> &symbols;
  const Symbol *entry_symbol; // may be null
};

bool EmitMachOImage(const EmitInputs &in, std::uint32_t filetype,
                    bool emit_pagezero, bool emit_main,
                    const std::string &install_name,
                    std::vector<std::uint8_t> &image_out,
                    std::string &error_out) {
  const auto &cfg = in.cfg;

  ma::BuildRequest req;
  req.arch     = ArchFromConfig(cfg.target_arch);
  req.filetype = filetype;
  req.emit_pagezero = emit_pagezero;
  req.emit_dyld_info = true;
  // BIN-9: emit a linker-signed ad-hoc CodeDirectory inline for every
  // Mach-O artefact.  Without this the kernel's AMFI policy on Apple
  // Silicon rejects any process whose binary lacks a "linker-signed"
  // signature ("amfid: ... not valid: Code=-423"), even when the file
  // is otherwise structurally correct and `codesign -vv` reports
  // "valid on disk".  See BuildLinkerSignedSignature() above.
  req.emit_code_signature = true;
  req.install_name = install_name;
  // Derive the CodeDirectory identifier from the output filename's
  // basename so otool / codesign report a stable, recognisable name
  // (matches what ld64 does for ad-hoc signed images).
  {
    const std::string &out = cfg.output_file;
    auto slash = out.find_last_of("/\\");
    req.identifier = (slash == std::string::npos) ? out : out.substr(slash + 1);
    if (req.identifier.empty()) req.identifier = "polyld";
  }
  req.base_address = (filetype == ma::kFileTypeExecute)
                         ? ma::kDefaultExeBase
                         : 0;

  // Header flags vary by filetype: dylibs and bundles must clear MH_PIE
  // (it is meaningful only for main executables) but should set
  // MH_NO_REEXPORTED_DYLIBS to stay compatible with the modern loader.
  if (filetype == ma::kFileTypeExecute) {
    req.header_flags = ma::kMhNoUndefs | ma::kMhDyldLink |
                       ma::kMhTwoLevel | ma::kMhPie;
  } else {
    req.header_flags = ma::kMhNoUndefs | ma::kMhDyldLink |
                       ma::kMhTwoLevel | ma::kMhNoReexp;
  }

  // ----- Section partitioning -------------------------------------------
  PartitionedBytes parts = PartitionSections(in.sections);

  // ----- Build segments -------------------------------------------------
  ma::SegmentDesc text_seg;
  text_seg.segname  = "__TEXT";
  text_seg.maxprot  = ma::kVmProtRead | ma::kVmProtExecute;
  text_seg.initprot = ma::kVmProtRead | ma::kVmProtExecute;
  ma::SectionDesc text_sec;
  text_sec.sectname = "__text";
  text_sec.segname  = "__TEXT";
  text_sec.flags    = ma::kSectionTypeRegular |
                      ma::kSectionAttrPureInstr |
                      ma::kSectionAttrSomeInstr;
  text_sec.alignment_log2 = 4; // 16 bytes
  text_sec.data = parts.text;
  // Mach-O requires __TEXT to be non-empty.  When the user code is
  // empty we synthesise a single 'ret' instruction so the resulting
  // image is still a valid bootable Mach-O — this matches what
  // `clang -nostdlib -e _start` does for an otherwise-empty source.
  if (text_sec.data.empty()) {
    if (cfg.target_arch == TargetArch::kAArch64) {
      // ret = 0xD65F03C0 (little-endian)
      text_sec.data = {0xC0, 0x03, 0x5F, 0xD6};
    } else {
      // ret = 0xC3
      text_sec.data = {0xC3};
    }
  }
  text_seg.sections.push_back(std::move(text_sec));

  if (!parts.rdata.empty()) {
    ma::SegmentDesc rd;
    rd.segname  = "__DATA_CONST";
    // dyld requires __DATA_CONST to map as rw- so it can apply chained
    // fixups, then re-protects the segment to r-- via SG_READ_ONLY at
    // the end of `Loader::applyFixupsToImage`.  Refusing rw- causes the
    // hard-abort  "__DATA_CONST segment permissions is not 'rw-'".
    rd.maxprot  = ma::kVmProtRead | ma::kVmProtWrite;
    rd.initprot = ma::kVmProtRead | ma::kVmProtWrite;
    // SG_READ_ONLY (0x10): tells dyld to mprotect the segment back to r--
    // after applying chained fixups.  dyld asserts the flag is present on
    // any segment named __DATA_CONST.
    rd.flags    = 0x10;
    ma::SectionDesc s;
    s.sectname = "__const";
    s.segname  = "__DATA_CONST";
    s.alignment_log2 = 3;
    s.data = parts.rdata;
    rd.sections.push_back(std::move(s));
    req.segments.push_back(std::move(rd));
  }
  // __TEXT goes first so its file offset 0 covers the header pages.
  req.segments.insert(req.segments.begin(), std::move(text_seg));

  if (!parts.data.empty()) {
    ma::SegmentDesc d;
    d.segname  = "__DATA";
    d.maxprot  = ma::kVmProtRead | ma::kVmProtWrite;
    d.initprot = ma::kVmProtRead | ma::kVmProtWrite;
    ma::SectionDesc s;
    s.sectname = "__data";
    s.segname  = "__DATA";
    s.alignment_log2 = 3;
    s.data = parts.data;
    d.sections.push_back(std::move(s));
    req.segments.push_back(std::move(d));
  }

  // ----- Symbols -------------------------------------------------------
  // For BIN-5 we emit one external definition per global defined symbol
  // anchored to the __TEXT segment.  Undefined symbols come last so the
  // dysymtab indices remain monotonic, matching Apple's expected layout.
  const Symbol *entry = in.entry_symbol;
  std::uint64_t entry_off = 0;
  if (entry && entry->is_defined) {
    // Symbol values are recorded relative to the linker's base address;
    // converting to an offset within the __TEXT segment is enough for
    // LC_MAIN's `entryoff`.
    entry_off = entry->value > cfg.base_address
                    ? entry->value - cfg.base_address
                    : entry->value;
  }
  // Ensure the entry offset stays within the __TEXT segment (which
  // includes the header pages).  Fall back to "first instruction" when
  // the linker has not yet learned the entry symbol's address.
  if (entry_off >= req.segments.front().sections.front().data.size())
    entry_off = 0;
  // LC_MAIN's `entryoff` is measured from the start of the __TEXT
  // *segment* (which begins at file offset 0 and covers the Mach-O
  // header pages), not from the start of the __text *section*.  The
  // entry symbol's value above is relative to the section's first byte,
  // so add the section's segment-relative offset back in to get a
  // file-coherent entry point.  Without this, dyld jumps into the
  // Mach-O header itself (`feedfacf...`) and the process crashes
  // before the first user instruction executes.
  // Note: the layout phase has already populated `text_file_off`
  // (= AlignUp(header_size, kPageSize)) which is exactly that offset
  // because the first user segment starts at file offset 0 and its
  // first section is placed after the header padding.

  // Add a header-marker symbol so otool / dsymutil can identify the
  // image — Apple's tools expect "__mh_execute_header" (executable),
  // "__mh_dylib_header" (dylib), or "__mh_bundle_header" (bundle).
  const char *marker = filetype == ma::kFileTypeExecute ? "__mh_execute_header"
                       : filetype == ma::kFileTypeDylib ? "__mh_dylib_header"
                                                        : "__mh_bundle_header";
  ma::SymbolDesc m{};
  m.name    = marker;
  m.n_type  = ma::kNTypeSect | ma::kNTypeExt;
  m.n_sect  = 1;
  m.n_value = req.base_address;
  req.symbols.push_back(m);
  ++req.extdef_count;

  // Walk the linker's symbol table for additional defined globals.
  for (const auto &kv : in.symbols) {
    const Symbol &s = kv.second;
    if (!s.is_defined || !s.is_global() || s.name == marker) continue;
    if (s.name.empty()) continue;
    ma::SymbolDesc d{};
    d.name    = s.name;
    d.n_type  = ma::kNTypeSect | ma::kNTypeExt;
    d.n_sect  = 1; // approximation: every global lives in __text for now
    d.n_value = req.base_address + (s.value > cfg.base_address
                                        ? s.value - cfg.base_address
                                        : s.value);
    req.symbols.push_back(d);
    ++req.extdef_count;
  }

  // The req.local_count / req.undef_count remain 0 in this BIN-5 cut.
  // Future linker work (incremental relocs, weak refs) can extend the
  // partition without changing the writer contract.

  req.entry_offset = emit_main ? entry_off : 0;

  ma::BuildResult br = ma::BuildMachOImage(req);
  if (br.image.empty()) {
    error_out = "polyld-err-E3230: Mach-O writer rejected build "
                "request (empty image returned)";
    return false;
  }
  image_out = std::move(br.image);
  return true;
}

// Drive `EmitMachOImage` and write the produced bytes to disk.  Pure
// helper — error reporting and stats accounting live in the Linker
// member methods so this routine has no Linker dependency.
bool BuildAndWrite(const LinkerConfig &cfg,
                   const std::vector<OutputSection> &sections,
                   const std::unordered_map<std::string, Symbol> &symbols,
                   const Symbol *entry,
                   std::uint32_t filetype, bool emit_pagezero, bool emit_main,
                   const std::string &install_name,
                   std::uint64_t &written_size,
                   std::string &error_out) {
  std::vector<std::uint8_t> image;
  EmitInputs in{cfg, sections, symbols, entry};
  if (!EmitMachOImage(in, filetype, emit_pagezero, emit_main, install_name,
                       image, error_out)) {
    return false;
  }
  std::ofstream out(cfg.output_file, std::ios::binary);
  if (!out) {
    error_out = "Cannot create output file: " + cfg.output_file;
    return false;
  }
  out.write(reinterpret_cast<const char *>(image.data()),
            static_cast<std::streamsize>(image.size()));
  if (!out) {
    error_out = "Failed writing output file: " + cfg.output_file;
    return false;
  }
  written_size = image.size();
  return true;
}

} // namespace

bool Linker::GenerateMachOExecutable() {
  Trace("Generating Mach-O executable: " + config_.output_file);

  // PE-7 parity: when input objects carry any `polyrt_println` call
  // sites we recover the ordered message bytes and synthesise a
  // self-contained syscall-driven `__text` payload — the Mach-O
  // companion of `pe::BuildPrintlnSequencePE`.  The legacy code path
  // (merge user objects, partition into __TEXT/__DATA) is preserved
  // bit-for-bit when the input contains no PRINTLN calls, so non-
  // PRINTLN programs keep their existing layout.
  const std::vector<std::string> println_messages =
      CollectPolyrtPrintlnSequence(objects_);

  std::uint64_t written = 0;
  std::string err;

  if (!println_messages.empty()) {
    std::vector<std::uint8_t> text =
        ::polyglot::linker::macho::BuildPrintlnSequenceMachO(
            ArchFromConfig(config_.target_arch), println_messages);
    if (text.empty()) {
      ReportError(
          "Mach-O writer rejected synthesised PRINTLN sequence "
          "(message too long or unsupported architecture)");
      return false;
    }
    // Bake the synthesised bytes into a single executable OutputSection
    // and route through the standard EmitMachOImage path.  The fresh
    // sections vector is local to this call so the linker's merged
    // `output_sections_` is not disturbed (other reporting / stats
    // continues to reflect what was actually linked).
    OutputSection synth;
    synth.name             = "__text";
    synth.segment          = "__TEXT";
    synth.flags            = SectionFlags::kAlloc | SectionFlags::kExecInstr;
    synth.alignment        = 16;
    synth.virtual_address  = 0;
    synth.data             = std::move(text);
    std::vector<OutputSection> synth_sections;
    synth_sections.push_back(std::move(synth));
    if (!BuildAndWrite(config_, synth_sections, symbol_table_,
                        /*entry=*/nullptr,
                        ::polyglot::linker::macho::kFileTypeExecute,
                        /*emit_pagezero*/ true, /*emit_main*/ true,
                        /*install_name*/ "", written, err)) {
      ReportError(err);
      return false;
    }
    Trace("Synthesised " + std::to_string(println_messages.size()) +
          " polyrt_println call site(s) into Mach-O __text");
    stats_.total_output_size = written;
    return true;
  }

  if (!BuildAndWrite(config_, output_sections_, symbol_table_,
                      LookupSymbol(config_.entry_point),
                      ::polyglot::linker::macho::kFileTypeExecute,
                      /*emit_pagezero*/ true, /*emit_main*/ true,
                      /*install_name*/ "", written, err)) {
    ReportError(err);
    return false;
  }
  stats_.total_output_size = written;
  return true;
}

bool Linker::GenerateMachODylib() {
  Trace("Generating Mach-O dylib: " + config_.output_file);
  // Install name defaults to "@rpath/<basename>" when the user did not
  // configure one explicitly — mirrors what `ld64 -install_name`
  // would write for a bare `-dynamiclib` invocation.
  std::string install = config_.dll_name;
  if (install.empty()) {
    auto slash = config_.output_file.find_last_of("/\\");
    install = "@rpath/" +
              (slash == std::string::npos ? config_.output_file
                                          : config_.output_file.substr(slash + 1));
  }
  std::uint64_t written = 0;
  std::string err;
  if (!BuildAndWrite(config_, output_sections_, symbol_table_,
                      LookupSymbol(config_.entry_point),
                      ::polyglot::linker::macho::kFileTypeDylib,
                      /*emit_pagezero*/ false, /*emit_main*/ false,
                      install, written, err)) {
    ReportError(err);
    return false;
  }
  stats_.total_output_size = written;
  return true;
}

bool Linker::GenerateMachOBundle() {
  Trace("Generating Mach-O bundle: " + config_.output_file);
  std::uint64_t written = 0;
  std::string err;
  if (!BuildAndWrite(config_, output_sections_, symbol_table_,
                      LookupSymbol(config_.entry_point),
                      ::polyglot::linker::macho::kFileTypeBundle,
                      /*emit_pagezero*/ false, /*emit_main*/ false,
                      /*install_name*/ "", written, err)) {
    ReportError(err);
    return false;
  }
  stats_.total_output_size = written;
  return true;
}

} // namespace polyglot::linker
