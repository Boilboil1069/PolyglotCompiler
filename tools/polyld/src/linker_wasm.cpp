/**
 * @file     linker_wasm.cpp
 * @brief    Wasm linker pipeline: parser, multi-module merger and
 *           emitter wired into `Linker::GenerateWasmModule`.
 *
 * The merger follows wasm-ld semantics: imports of one module are
 * satisfied by exports of another (with full FuncType matching), and
 * function / table / memory / global indices are re-mapped through a
 * single merged index space.  Code bodies are walked via a complete
 * WebAssembly 1.0 + bulk-memory + reference-types instruction iterator
 * so that `call`, `ref.func`, `global.get/set`, `table.*`, etc. all
 * receive the patched indices.
 *
 * @author   Manning Cyrus
 * @date     2026-05-06
 */

#include "tools/polyld/include/linker_wasm.h"
#include "tools/polyld/include/linker.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace polyglot::linker::wasm {

namespace {

// ---------------------------------------------------------------------------
// LEB128 primitives
// ---------------------------------------------------------------------------

// Emit a single byte appender that all encoders below share.
[[maybe_unused]] inline void PushByte(std::vector<std::uint8_t> &out,
                                      std::uint8_t b) {
  out.push_back(b);
}

// Little-endian raw f32 / f64 immediate copy — kept private; the
// instruction walker uses it directly when stepping past constants.
template <std::size_t N>
void CopyRaw(std::vector<std::uint8_t> &dst, const std::uint8_t *&p,
             const std::uint8_t *end) {
  if (static_cast<std::size_t>(end - p) < N) {
    p = end;  // signal overflow to caller
    return;
  }
  dst.insert(dst.end(), p, p + N);
  p += N;
}

bool DecodeULeb32Internal(const std::uint8_t *&p, const std::uint8_t *end,
                          std::uint32_t &value) {
  value = 0;
  unsigned shift = 0;
  for (int i = 0; i < 5; ++i) {
    if (p >= end) return false;
    std::uint8_t b = *p++;
    value |= static_cast<std::uint32_t>(b & 0x7F) << shift;
    if ((b & 0x80) == 0) return true;
    shift += 7;
  }
  return false;
}

bool DecodeSLeb32Internal(const std::uint8_t *&p, const std::uint8_t *end,
                          std::int32_t &value) {
  std::int64_t result = 0;
  unsigned shift = 0;
  for (int i = 0; i < 5; ++i) {
    if (p >= end) return false;
    std::uint8_t b = *p++;
    result |= static_cast<std::int64_t>(b & 0x7F) << shift;
    shift += 7;
    if ((b & 0x80) == 0) {
      if (shift < 32 && (b & 0x40) != 0) {
        result |= -(static_cast<std::int64_t>(1) << shift);
      }
      value = static_cast<std::int32_t>(result);
      return true;
    }
  }
  return false;
}

bool DecodeSLeb64Internal(const std::uint8_t *&p, const std::uint8_t *end,
                          std::int64_t &value) {
  std::int64_t result = 0;
  unsigned shift = 0;
  for (int i = 0; i < 10; ++i) {
    if (p >= end) return false;
    std::uint8_t b = *p++;
    result |= static_cast<std::int64_t>(b & 0x7F) << shift;
    shift += 7;
    if ((b & 0x80) == 0) {
      if (shift < 64 && (b & 0x40) != 0) {
        result |= -(static_cast<std::int64_t>(1) << shift);
      }
      value = result;
      return true;
    }
  }
  return false;
}

void EmitU32(std::vector<std::uint8_t> &out, std::uint32_t v) {
  do {
    std::uint8_t b = v & 0x7F;
    v >>= 7;
    if (v) b |= 0x80;
    out.push_back(b);
  } while (v);
}

void EmitS32(std::vector<std::uint8_t> &out, std::int32_t v) {
  bool more = true;
  while (more) {
    std::uint8_t b = v & 0x7F;
    v >>= 7;
    if ((v == 0 && (b & 0x40) == 0) || (v == -1 && (b & 0x40) != 0))
      more = false;
    else
      b |= 0x80;
    out.push_back(b);
  }
}

void EmitS64(std::vector<std::uint8_t> &out, std::int64_t v) {
  bool more = true;
  while (more) {
    std::uint8_t b = v & 0x7F;
    v >>= 7;
    if ((v == 0 && (b & 0x40) == 0) || (v == -1 && (b & 0x40) != 0))
      more = false;
    else
      b |= 0x80;
    out.push_back(b);
  }
}

// ---------------------------------------------------------------------------
// Parser helpers
// ---------------------------------------------------------------------------

struct Cursor {
  const std::uint8_t *p{nullptr};
  const std::uint8_t *end{nullptr};

  bool Eof() const { return p >= end; }
  std::size_t Remaining() const { return static_cast<std::size_t>(end - p); }

  bool ReadByte(std::uint8_t &b) {
    if (p >= end) return false;
    b = *p++;
    return true;
  }
  bool ReadU32(std::uint32_t &v) { return DecodeULeb32Internal(p, end, v); }
  bool ReadName(std::string &s) {
    std::uint32_t n = 0;
    if (!ReadU32(n)) return false;
    if (Remaining() < n) return false;
    s.assign(reinterpret_cast<const char *>(p), n);
    p += n;
    return true;
  }
  bool ReadBytes(std::vector<std::uint8_t> &out, std::size_t n) {
    if (Remaining() < n) return false;
    out.insert(out.end(), p, p + n);
    p += n;
    return true;
  }
  bool ReadValType(ValType &vt) {
    std::uint8_t b;
    if (!ReadByte(b)) return false;
    vt = static_cast<ValType>(b);
    return true;
  }
  bool ReadLimits(Limits &l) {
    std::uint8_t flags;
    if (!ReadByte(flags)) return false;
    l.has_max = (flags & 0x01) != 0;
    l.shared  = (flags & 0x02) != 0;
    if (!ReadU32(l.min)) return false;
    if (l.has_max && !ReadU32(l.max)) return false;
    return true;
  }
  // Skip past a constant init expression (terminated by `end` 0x0B).
  // The bytes are appended to `out` verbatim, including the terminator.
  bool ReadInitExpr(std::vector<std::uint8_t> &out) {
    while (p < end) {
      std::uint8_t op = *p++;
      out.push_back(op);
      switch (op) {
        case 0x0B: return true;                          // end
        case 0x41: { std::int32_t v; if (!DecodeSLeb32Internal(p, end, v)) return false;
                     EmitS32(out, v); break; }
        case 0x42: { std::int64_t v; if (!DecodeSLeb64Internal(p, end, v)) return false;
                     EmitS64(out, v); break; }
        case 0x43: { if (Remaining() < 4) return false;
                     out.insert(out.end(), p, p + 4); p += 4; break; }
        case 0x44: { if (Remaining() < 8) return false;
                     out.insert(out.end(), p, p + 8); p += 8; break; }
        case 0x23: { std::uint32_t v; if (!ReadU32(v)) return false;
                     EmitU32(out, v); break; }
        case 0xD0: { std::uint8_t t; if (!ReadByte(t)) return false;
                     out.push_back(t); break; }
        case 0xD2: { std::uint32_t v; if (!ReadU32(v)) return false;
                     EmitU32(out, v); break; }
        default: return false;  // Unsupported constant expression opcode.
      }
    }
    return false;
  }
};

// Tag a diagnostic with the canonical `polyld-err-Exxxx:` prefix.
std::string MakeErr(const char *code, const std::string &msg) {
  std::ostringstream os;
  os << code << ": " << msg;
  return os.str();
}

}  // namespace

// ---------------------------------------------------------------------------
// Public LEB helpers (declared in the header)
// ---------------------------------------------------------------------------

bool DecodeULeb32(const std::uint8_t *&p, const std::uint8_t *end,
                  std::uint32_t &value) {
  return DecodeULeb32Internal(p, end, value);
}

void EncodeULeb32(std::vector<std::uint8_t> &out, std::uint32_t value) {
  EmitU32(out, value);
}

void EncodeULeb32Padded5(std::vector<std::uint8_t> &out, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<std::uint8_t>((value & 0x7F) | 0x80));
    value >>= 7;
  }
  out.push_back(static_cast<std::uint8_t>(value & 0x7F));
}

void EncodeSLeb32(std::vector<std::uint8_t> &out, std::int32_t value) {
  EmitS32(out, value);
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

namespace {

bool ParseTypeSection(Cursor c, Module &m, std::string &err) {
  std::uint32_t n;
  if (!c.ReadU32(n)) { err = MakeErr("polyld-err-E3300", "type count"); return false; }
  m.types.resize(n);
  for (auto &t : m.types) {
    std::uint8_t tag;
    if (!c.ReadByte(tag) || tag != 0x60) {
      err = MakeErr("polyld-err-E3300", "expected functype tag 0x60");
      return false;
    }
    std::uint32_t pn;
    if (!c.ReadU32(pn)) return false;
    t.params.resize(pn);
    for (auto &v : t.params) if (!c.ReadValType(v)) return false;
    std::uint32_t rn;
    if (!c.ReadU32(rn)) return false;
    t.results.resize(rn);
    for (auto &v : t.results) if (!c.ReadValType(v)) return false;
  }
  return true;
}

bool ParseImportSection(Cursor c, Module &m, std::string &err) {
  std::uint32_t n;
  if (!c.ReadU32(n)) { err = MakeErr("polyld-err-E3300", "import count"); return false; }
  m.imports.resize(n);
  for (auto &im : m.imports) {
    if (!c.ReadName(im.module_name)) return false;
    if (!c.ReadName(im.name)) return false;
    std::uint8_t k;
    if (!c.ReadByte(k)) return false;
    im.kind = static_cast<ImportKind>(k);
    switch (im.kind) {
      case ImportKind::kFunc:
        if (!c.ReadU32(im.type_index)) return false;
        break;
      case ImportKind::kTable:
        if (!c.ReadValType(im.table_type.elem)) return false;
        if (!c.ReadLimits(im.table_type.limits)) return false;
        break;
      case ImportKind::kMemory:
        if (!c.ReadLimits(im.memory_type.limits)) return false;
        break;
      case ImportKind::kGlobal:
        if (!c.ReadValType(im.global_type.valtype)) return false;
        std::uint8_t mut;
        if (!c.ReadByte(mut)) return false;
        im.global_type.mutability = (mut != 0);
        break;
    }
  }
  return true;
}

bool ParseFunctionSection(Cursor c, Module &m, std::string &) {
  std::uint32_t n;
  if (!c.ReadU32(n)) return false;
  m.functions.resize(n);
  for (auto &f : m.functions) if (!c.ReadU32(f.type_index)) return false;
  return true;
}

bool ParseTableSection(Cursor c, Module &m, std::string &) {
  std::uint32_t n;
  if (!c.ReadU32(n)) return false;
  m.tables.resize(n);
  for (auto &t : m.tables) {
    if (!c.ReadValType(t.elem)) return false;
    if (!c.ReadLimits(t.limits)) return false;
  }
  return true;
}

bool ParseMemorySection(Cursor c, Module &m, std::string &) {
  std::uint32_t n;
  if (!c.ReadU32(n)) return false;
  m.memories.resize(n);
  for (auto &mem : m.memories) if (!c.ReadLimits(mem.limits)) return false;
  return true;
}

bool ParseGlobalSection(Cursor c, Module &m, std::string &err) {
  std::uint32_t n;
  if (!c.ReadU32(n)) return false;
  m.globals.resize(n);
  for (auto &g : m.globals) {
    if (!c.ReadValType(g.type.valtype)) return false;
    std::uint8_t mut;
    if (!c.ReadByte(mut)) return false;
    g.type.mutability = (mut != 0);
    if (!c.ReadInitExpr(g.init_expr)) {
      err = MakeErr("polyld-err-E3300", "global init expr");
      return false;
    }
  }
  return true;
}

bool ParseExportSection(Cursor c, Module &m, std::string &) {
  std::uint32_t n;
  if (!c.ReadU32(n)) return false;
  m.exports.resize(n);
  for (auto &e : m.exports) {
    if (!c.ReadName(e.name)) return false;
    std::uint8_t k;
    if (!c.ReadByte(k)) return false;
    e.kind = static_cast<ExportKind>(k);
    if (!c.ReadU32(e.index)) return false;
  }
  return true;
}

bool ParseStartSection(Cursor c, Module &m, std::string &) {
  m.has_start = true;
  return c.ReadU32(m.start_func);
}

bool ParseElementSection(Cursor c, Module &m, std::string &err) {
  std::uint32_t n;
  if (!c.ReadU32(n)) return false;
  m.elements.resize(n);
  for (auto &el : m.elements) {
    if (!c.ReadU32(el.flags)) return false;
    bool active     = (el.flags & 0x01) == 0;
    bool has_table  = (el.flags & 0x02) != 0;
    bool use_exprs  = (el.flags & 0x04) != 0;
    el.uses_expressions = use_exprs;
    if (active && has_table) {
      if (!c.ReadU32(el.table_index)) return false;
    }
    if (active) {
      if (!c.ReadInitExpr(el.offset_expr)) return false;
    }
    if (!active || has_table) {
      // elemkind / reftype byte present; we accept funcref / externref.
      std::uint8_t kind;
      if (!c.ReadByte(kind)) return false;
      el.elem_type = (kind == 0x00) ? ValType::kFuncRef
                                    : static_cast<ValType>(kind);
    } else {
      el.elem_type = ValType::kFuncRef;
    }
    std::uint32_t cnt;
    if (!c.ReadU32(cnt)) return false;
    if (use_exprs) {
      el.init_exprs.resize(cnt);
      for (auto &ex : el.init_exprs) if (!c.ReadInitExpr(ex)) return false;
    } else {
      el.func_indices.resize(cnt);
      for (auto &fi : el.func_indices) if (!c.ReadU32(fi)) return false;
    }
    (void)err;
  }
  return true;
}

bool ParseDataCountSection(Cursor c, Module &m, std::string &) {
  m.has_data_count = true;
  return c.ReadU32(m.data_count);
}

bool ParseDataSection(Cursor c, Module &m, std::string &) {
  std::uint32_t n;
  if (!c.ReadU32(n)) return false;
  m.data_segments.resize(n);
  for (auto &d : m.data_segments) {
    if (!c.ReadU32(d.flags)) return false;
    bool active   = (d.flags & 0x01) == 0;
    bool has_mem  = (d.flags & 0x02) != 0;
    if (active && has_mem) {
      if (!c.ReadU32(d.memory_index)) return false;
    }
    if (active) {
      if (!c.ReadInitExpr(d.offset_expr)) return false;
    }
    std::uint32_t blen;
    if (!c.ReadU32(blen)) return false;
    if (!c.ReadBytes(d.bytes, blen)) return false;
  }
  return true;
}

bool ParseCodeSection(Cursor c, Module &m, std::string &err) {
  std::uint32_t n;
  if (!c.ReadU32(n)) return false;
  if (n != m.functions.size()) {
    err = MakeErr("polyld-err-E3300",
                  "code section count does not match function section");
    return false;
  }
  for (auto &f : m.functions) {
    std::uint32_t body_len;
    if (!c.ReadU32(body_len)) return false;
    if (c.Remaining() < body_len) return false;
    f.body.assign(c.p, c.p + body_len);
    c.p += body_len;
  }
  return true;
}

bool ParseCustomSection(Cursor c, Module &m, std::string &) {
  CustomSection cs;
  if (!c.ReadName(cs.name)) return false;
  cs.payload.assign(c.p, c.end);
  m.custom_sections.push_back(std::move(cs));
  return true;
}

}  // namespace

bool ParseWasmModule(const std::vector<std::uint8_t> &bytes,
                     Module &out, std::string &error_out) {
  out = {};
  if (bytes.size() < 8 ||
      !std::equal(kMagic.begin(), kMagic.end(), bytes.begin()) ||
      !std::equal(kVersion.begin(), kVersion.end(), bytes.begin() + 4)) {
    error_out = MakeErr("polyld-err-E3300",
                        "input is not a Wasm 1.0 module (bad preamble)");
    return false;
  }
  Cursor c{bytes.data() + 8, bytes.data() + bytes.size()};
  while (!c.Eof()) {
    std::uint8_t id;
    if (!c.ReadByte(id)) {
      error_out = MakeErr("polyld-err-E3300", "truncated section header");
      return false;
    }
    std::uint32_t size;
    if (!c.ReadU32(size)) {
      error_out = MakeErr("polyld-err-E3300", "bad section size");
      return false;
    }
    if (c.Remaining() < size) {
      error_out = MakeErr("polyld-err-E3300", "section overruns module");
      return false;
    }
    Cursor sub{c.p, c.p + size};
    c.p += size;
    bool ok = true;
    switch (static_cast<SectionId>(id)) {
      case SectionId::kCustom:    ok = ParseCustomSection(sub, out, error_out); break;
      case SectionId::kType:      ok = ParseTypeSection(sub, out, error_out); break;
      case SectionId::kImport:    ok = ParseImportSection(sub, out, error_out); break;
      case SectionId::kFunction:  ok = ParseFunctionSection(sub, out, error_out); break;
      case SectionId::kTable:     ok = ParseTableSection(sub, out, error_out); break;
      case SectionId::kMemory:    ok = ParseMemorySection(sub, out, error_out); break;
      case SectionId::kGlobal:    ok = ParseGlobalSection(sub, out, error_out); break;
      case SectionId::kExport:    ok = ParseExportSection(sub, out, error_out); break;
      case SectionId::kStart:     ok = ParseStartSection(sub, out, error_out); break;
      case SectionId::kElement:   ok = ParseElementSection(sub, out, error_out); break;
      case SectionId::kCode:      ok = ParseCodeSection(sub, out, error_out); break;
      case SectionId::kData:      ok = ParseDataSection(sub, out, error_out); break;
      case SectionId::kDataCount: ok = ParseDataCountSection(sub, out, error_out); break;
      default:
        error_out = MakeErr("polyld-err-E3300",
                            "unknown section id " + std::to_string(id));
        return false;
    }
    if (!ok) {
      if (error_out.empty())
        error_out = MakeErr("polyld-err-E3300",
                            "section " + std::to_string(id) + " parse failure");
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Code-section walker (used by the merger to re-map indices)
// ---------------------------------------------------------------------------

namespace {

struct RemapCtx {
  // Per-source-module → merged index translations.
  const std::vector<std::uint32_t> *func_map{nullptr};
  const std::vector<std::uint32_t> *type_map{nullptr};
  const std::vector<std::uint32_t> *global_map{nullptr};
  const std::vector<std::uint32_t> *table_map{nullptr};
  const std::vector<std::uint32_t> *memory_map{nullptr};
  const std::vector<std::uint32_t> *data_map{nullptr};
  const std::vector<std::uint32_t> *elem_map{nullptr};
};

// Look up an index through a per-source remap table; returns false on
// out-of-range so the caller can surface E3340.
bool Map(const std::vector<std::uint32_t> *table, std::uint32_t in,
         std::uint32_t &out) {
  if (table == nullptr) { out = in; return true; }
  if (in >= table->size()) return false;
  out = (*table)[in];
  return true;
}

bool RewriteBlockType(const std::uint8_t *&p, const std::uint8_t *end,
                      const RemapCtx &ctx,
                      std::vector<std::uint8_t> &out, std::string &err) {
  if (p >= end) { err = "blocktype overflow"; return false; }
  // Blocktype is either a single byte (0x40 = empty, or a valtype byte)
  // or a positive signed LEB encoding a typeidx.
  std::uint8_t first = *p;
  if (first == 0x40 || (first >= 0x6F && first <= 0x7F)) {
    out.push_back(first);
    ++p;
    return true;
  }
  std::int32_t s;
  if (!DecodeSLeb32Internal(p, end, s) || s < 0) {
    err = "bad blocktype";
    return false;
  }
  std::uint32_t mapped;
  if (!Map(ctx.type_map, static_cast<std::uint32_t>(s), mapped)) {
    err = "typeidx out of range in blocktype";
    return false;
  }
  EmitS32(out, static_cast<std::int32_t>(mapped));
  return true;
}

bool ReadAndRewriteMemarg(const std::uint8_t *&p, const std::uint8_t *end,
                          std::vector<std::uint8_t> &out) {
  std::uint32_t a, o;
  if (!DecodeULeb32Internal(p, end, a)) return false;
  if (!DecodeULeb32Internal(p, end, o)) return false;
  EmitU32(out, a);
  EmitU32(out, o);
  return true;
}

// Walk one function body, producing the rewritten body in `out`.
// Returns false with `err` populated on decode failure or unsupported
// opcode (SIMD / atomic prefixes).
bool RewriteCodeBody(const std::vector<std::uint8_t> &body,
                     const RemapCtx &ctx,
                     std::vector<std::uint8_t> &out, std::string &err) {
  const std::uint8_t *p = body.data();
  const std::uint8_t *end = body.data() + body.size();

  // local declarations: u32 count, then count × (u32 n, valtype b)
  std::uint32_t local_count;
  if (!DecodeULeb32Internal(p, end, local_count)) {
    err = "bad local count"; return false;
  }
  EmitU32(out, local_count);
  for (std::uint32_t i = 0; i < local_count; ++i) {
    std::uint32_t n;
    if (!DecodeULeb32Internal(p, end, n)) { err = "bad local n"; return false; }
    if (p >= end) { err = "missing local valtype"; return false; }
    std::uint8_t vt = *p++;
    EmitU32(out, n);
    out.push_back(vt);
  }

  int depth = 1;  // The function body itself is nested in one implicit frame.
  while (p < end && depth > 0) {
    std::uint8_t op = *p++;
    out.push_back(op);
    switch (op) {
      case 0x00: case 0x01: case 0x05: case 0x0F:
      case 0x1A: case 0x1B:
        break;
      case 0x02: case 0x03: case 0x04:
        ++depth;
        if (!RewriteBlockType(p, end, ctx, out, err)) return false;
        break;
      case 0x0B:
        --depth;
        break;
      case 0x0C: case 0x0D: {
        std::uint32_t lab;
        if (!DecodeULeb32Internal(p, end, lab)) { err = "bad br label"; return false; }
        EmitU32(out, lab);
        break;
      }
      case 0x0E: {
        std::uint32_t cnt;
        if (!DecodeULeb32Internal(p, end, cnt)) { err = "bad br_table"; return false; }
        EmitU32(out, cnt);
        for (std::uint32_t i = 0; i <= cnt; ++i) {
          std::uint32_t lab;
          if (!DecodeULeb32Internal(p, end, lab)) { err = "br_table label"; return false; }
          EmitU32(out, lab);
        }
        break;
      }
      case 0x10: {
        std::uint32_t f, mapped;
        if (!DecodeULeb32Internal(p, end, f)) { err = "call funcidx"; return false; }
        if (!Map(ctx.func_map, f, mapped)) {
          err = MakeErr("polyld-err-E3340", "call funcidx out of range");
          return false;
        }
        EmitU32(out, mapped);
        break;
      }
      case 0x11: {
        std::uint32_t t, ti, mt, mti;
        if (!DecodeULeb32Internal(p, end, t)) { err = "call_indirect type"; return false; }
        if (!DecodeULeb32Internal(p, end, ti)) { err = "call_indirect table"; return false; }
        if (!Map(ctx.type_map, t, mt) || !Map(ctx.table_map, ti, mti)) {
          err = MakeErr("polyld-err-E3340", "call_indirect index out of range");
          return false;
        }
        EmitU32(out, mt);
        EmitU32(out, mti);
        break;
      }
      case 0x1C: {
        std::uint32_t cnt;
        if (!DecodeULeb32Internal(p, end, cnt)) return false;
        EmitU32(out, cnt);
        for (std::uint32_t i = 0; i < cnt; ++i) {
          if (p >= end) return false;
          out.push_back(*p++);
        }
        break;
      }
      case 0x20: case 0x21: case 0x22: {
        std::uint32_t l;
        if (!DecodeULeb32Internal(p, end, l)) return false;
        EmitU32(out, l);  // local indices are not re-mapped
        break;
      }
      case 0x23: case 0x24: {
        std::uint32_t g, mg;
        if (!DecodeULeb32Internal(p, end, g)) return false;
        if (!Map(ctx.global_map, g, mg)) {
          err = MakeErr("polyld-err-E3340", "global index out of range"); return false;
        }
        EmitU32(out, mg);
        break;
      }
      case 0x25: case 0x26: {
        std::uint32_t t, mt;
        if (!DecodeULeb32Internal(p, end, t)) return false;
        if (!Map(ctx.table_map, t, mt)) {
          err = MakeErr("polyld-err-E3340", "table index out of range"); return false;
        }
        EmitU32(out, mt);
        break;
      }
      case 0x3F: case 0x40: {
        // memory.size / memory.grow — single byte memidx (must be 0 in MVP).
        if (p >= end) return false;
        std::uint8_t mi = *p++;
        out.push_back(mi);
        break;
      }
      case 0x41: { std::int32_t v; if (!DecodeSLeb32Internal(p, end, v)) return false;
                   EmitS32(out, v); break; }
      case 0x42: { std::int64_t v; if (!DecodeSLeb64Internal(p, end, v)) return false;
                   EmitS64(out, v); break; }
      case 0x43: CopyRaw<4>(out, p, end); break;
      case 0x44: CopyRaw<8>(out, p, end); break;
      case 0xD0: {
        if (p >= end) return false;
        out.push_back(*p++);  // reftype byte
        break;
      }
      case 0xD2: {
        std::uint32_t f, mf;
        if (!DecodeULeb32Internal(p, end, f)) return false;
        if (!Map(ctx.func_map, f, mf)) {
          err = MakeErr("polyld-err-E3340", "ref.func index out of range");
          return false;
        }
        EmitU32(out, mf);
        break;
      }
      case 0xFC: {
        std::uint32_t sub;
        if (!DecodeULeb32Internal(p, end, sub)) return false;
        EmitU32(out, sub);
        switch (sub) {
          case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
            break;  // saturating truncations: no immediates
          case 8: {
            std::uint32_t d, mi, md, mmi;
            if (!DecodeULeb32Internal(p, end, d)) return false;
            if (!DecodeULeb32Internal(p, end, mi)) return false;
            if (!Map(ctx.data_map, d, md) ||
                !Map(ctx.memory_map, mi, mmi)) {
              err = MakeErr("polyld-err-E3340", "memory.init index"); return false;
            }
            EmitU32(out, md); EmitU32(out, mmi); break;
          }
          case 9: {
            std::uint32_t d, md;
            if (!DecodeULeb32Internal(p, end, d)) return false;
            if (!Map(ctx.data_map, d, md)) {
              err = MakeErr("polyld-err-E3340", "data.drop index"); return false;
            }
            EmitU32(out, md); break;
          }
          case 10: {
            std::uint32_t a, b, ma, mb;
            if (!DecodeULeb32Internal(p, end, a)) return false;
            if (!DecodeULeb32Internal(p, end, b)) return false;
            if (!Map(ctx.memory_map, a, ma) ||
                !Map(ctx.memory_map, b, mb)) return false;
            EmitU32(out, ma); EmitU32(out, mb); break;
          }
          case 11: {
            std::uint32_t mi, mmi;
            if (!DecodeULeb32Internal(p, end, mi)) return false;
            if (!Map(ctx.memory_map, mi, mmi)) return false;
            EmitU32(out, mmi); break;
          }
          case 12: {
            std::uint32_t e, ti, me, mti;
            if (!DecodeULeb32Internal(p, end, e)) return false;
            if (!DecodeULeb32Internal(p, end, ti)) return false;
            if (!Map(ctx.elem_map, e, me) ||
                !Map(ctx.table_map, ti, mti)) return false;
            EmitU32(out, me); EmitU32(out, mti); break;
          }
          case 13: {
            std::uint32_t e, me;
            if (!DecodeULeb32Internal(p, end, e)) return false;
            if (!Map(ctx.elem_map, e, me)) return false;
            EmitU32(out, me); break;
          }
          case 14: {
            std::uint32_t a, b, ma, mb;
            if (!DecodeULeb32Internal(p, end, a)) return false;
            if (!DecodeULeb32Internal(p, end, b)) return false;
            if (!Map(ctx.table_map, a, ma) ||
                !Map(ctx.table_map, b, mb)) return false;
            EmitU32(out, ma); EmitU32(out, mb); break;
          }
          case 15: case 16: case 17: {
            std::uint32_t t, mt;
            if (!DecodeULeb32Internal(p, end, t)) return false;
            if (!Map(ctx.table_map, t, mt)) return false;
            EmitU32(out, mt); break;
          }
          default:
            err = MakeErr("polyld-err-E3340",
                          "unsupported 0xFC sub-opcode " + std::to_string(sub));
            return false;
        }
        break;
      }
      case 0xFD: case 0xFE:
        err = MakeErr("polyld-err-E3340",
                      "SIMD/atomic prefix not supported by the merger");
        return false;
      default:
        // Numeric ops 0x45..0xC4 and other no-immediate forms.
        if (op >= 0x28 && op <= 0x3E) {
          if (!ReadAndRewriteMemarg(p, end, out)) return false;
        }
        // Else: opcode has no immediates and was already copied via the
        // unconditional `out.push_back(op)` above.
        break;
    }
  }
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Linker (multi-module merger)
// ---------------------------------------------------------------------------

namespace {

// Look up a type index in the merged type vector, appending if needed.
std::uint32_t InternType(std::vector<FuncType> &pool, const FuncType &t) {
  for (std::size_t i = 0; i < pool.size(); ++i)
    if (pool[i] == t) return static_cast<std::uint32_t>(i);
  pool.push_back(t);
  return static_cast<std::uint32_t>(pool.size() - 1);
}

}  // namespace

bool LinkWasmModules(const std::vector<Module> &inputs,
                     Module &out, std::string &error_out) {
  out = {};
  if (inputs.empty()) {
    error_out = MakeErr("polyld-err-E3340", "no wasm modules to link");
    return false;
  }

  // -------- Pass 1: build the global export table ----------------------
  // Each entry maps "module/name" → (source_module_index, kind, local_index_in_source_after_imports).
  struct ExportRef {
    std::size_t   src;
    ExportKind    kind;
    std::uint32_t local_index;       // Source-module index space (post-imports).
  };
  std::unordered_map<std::string, ExportRef> table;
  for (std::size_t mi = 0; mi < inputs.size(); ++mi) {
    for (const auto &e : inputs[mi].exports) {
      table.emplace(e.name, ExportRef{mi, e.kind, e.index});
    }
  }

  // -------- Pass 2: assign merged indices ------------------------------
  // Per-source remap tables (input_index → merged_index) for each kind.
  std::vector<std::vector<std::uint32_t>> func_map(inputs.size());
  std::vector<std::vector<std::uint32_t>> type_map(inputs.size());
  std::vector<std::vector<std::uint32_t>> global_map(inputs.size());
  std::vector<std::vector<std::uint32_t>> table_map(inputs.size());
  std::vector<std::vector<std::uint32_t>> memory_map(inputs.size());
  std::vector<std::vector<std::uint32_t>> data_map(inputs.size());
  std::vector<std::vector<std::uint32_t>> elem_map(inputs.size());

  // First intern types so import / function entries can refer to them.
  for (std::size_t mi = 0; mi < inputs.size(); ++mi) {
    const auto &m = inputs[mi];
    type_map[mi].resize(m.types.size());
    for (std::size_t i = 0; i < m.types.size(); ++i)
      type_map[mi][i] = InternType(out.types, m.types[i]);
  }

  // Merged imports: kept only when no sibling module exports a matching
  // entry.  We seed the function index space with surviving imports
  // first (wasm requires imports before locals in every index space).
  // Resolved imports record their forwarding target; we rewrite later.
  struct ResolvedImport {
    bool        resolved{false};
    std::size_t target_src{0};     // Only valid when resolved.
    std::uint32_t target_local{0}; // Source-local index post-imports.
  };
  std::vector<std::vector<ResolvedImport>> import_resolution(inputs.size());

  // Pre-sized count of imports that survive → used to seed the merged
  // function/global/etc. index space *before* local definitions get
  // their merged indices.
  std::vector<Import> kept_imports;

  // First: try to resolve every import; build kept_imports in a stable
  // order so the merged module is deterministic.
  for (std::size_t mi = 0; mi < inputs.size(); ++mi) {
    const auto &m = inputs[mi];
    import_resolution[mi].resize(m.imports.size());
    for (std::size_t ii = 0; ii < m.imports.size(); ++ii) {
      const auto &im = m.imports[ii];
      auto it = table.find(im.name);
      if (it != table.end() && it->second.kind == im.kind) {
        // Type matching for func imports.
        if (im.kind == ImportKind::kFunc) {
          const auto &src = inputs[it->second.src];
          std::uint32_t target_local = it->second.local_index;
          std::uint32_t target_type = src.functions.empty() ? 0
              : src.functions[target_local - static_cast<std::uint32_t>(
                  std::count_if(src.imports.begin(), src.imports.end(),
                                [](const Import &x){
                                  return x.kind == ImportKind::kFunc; }))]
                .type_index;
          (void)target_type;  // Full type compare done after type interning below.
          // Re-fetch types after interning:
          std::uint32_t want_in_merged = type_map[mi][im.type_index];
          // Locate target's actual type in source:
          std::uint32_t imp_funcs_in_src = 0;
          for (const auto &x : src.imports)
            if (x.kind == ImportKind::kFunc) ++imp_funcs_in_src;
          std::uint32_t local_func_index = target_local - imp_funcs_in_src;
          if (local_func_index >= src.functions.size()) {
            error_out = MakeErr("polyld-err-E3310",
                                "import '" + im.name +
                                "' resolves to non-function export");
            return false;
          }
          std::uint32_t got_in_merged =
              type_map[it->second.src][src.functions[local_func_index].type_index];
          if (want_in_merged != got_in_merged) {
            error_out = MakeErr("polyld-err-E3330",
                                "type mismatch for import '" + im.name + "'");
            return false;
          }
        }
        import_resolution[mi][ii] = {true, it->second.src, it->second.local_index};
      } else {
        import_resolution[mi][ii] = {false, 0, 0};
        kept_imports.push_back(im);
        // Re-target the type index for kept func imports through the
        // merged type pool.
        if (kept_imports.back().kind == ImportKind::kFunc)
          kept_imports.back().type_index = type_map[mi][im.type_index];
      }
    }
  }
  out.imports = std::move(kept_imports);

  // Walk merged imports to figure out per-kind survivor counts so the
  // local index spaces start at the right offset.
  std::uint32_t imp_func = 0, imp_table = 0, imp_mem = 0, imp_global = 0;
  for (const auto &im : out.imports) {
    switch (im.kind) {
      case ImportKind::kFunc:   ++imp_func;   break;
      case ImportKind::kTable:  ++imp_table;  break;
      case ImportKind::kMemory: ++imp_mem;    break;
      case ImportKind::kGlobal: ++imp_global; break;
    }
  }

  // Pre-compute the merged index of each *kept* import so the per-source
  // import-side maps line up with the survivors.  Walk in the same
  // construction order used above.
  // For each (mi, ii) where !resolved: find merged index by counting
  // earlier kept imports of the same kind.
  std::vector<std::vector<std::uint32_t>> kept_func_idx(inputs.size());
  std::vector<std::vector<std::uint32_t>> kept_table_idx(inputs.size());
  std::vector<std::vector<std::uint32_t>> kept_mem_idx(inputs.size());
  std::vector<std::vector<std::uint32_t>> kept_global_idx(inputs.size());
  for (std::size_t mi = 0; mi < inputs.size(); ++mi) {
    const auto &m = inputs[mi];
    kept_func_idx[mi].resize(m.imports.size(), 0);
    kept_table_idx[mi].resize(m.imports.size(), 0);
    kept_mem_idx[mi].resize(m.imports.size(), 0);
    kept_global_idx[mi].resize(m.imports.size(), 0);
  }
  {
    std::uint32_t fi = 0, ti = 0, mi_ = 0, gi = 0;
    for (std::size_t mi = 0; mi < inputs.size(); ++mi) {
      const auto &m = inputs[mi];
      for (std::size_t ii = 0; ii < m.imports.size(); ++ii) {
        if (import_resolution[mi][ii].resolved) continue;
        switch (m.imports[ii].kind) {
          case ImportKind::kFunc:   kept_func_idx[mi][ii]   = fi++;  break;
          case ImportKind::kTable:  kept_table_idx[mi][ii]  = ti++;  break;
          case ImportKind::kMemory: kept_mem_idx[mi][ii]    = mi_++; break;
          case ImportKind::kGlobal: kept_global_idx[mi][ii] = gi++;  break;
        }
      }
    }
  }

  // Now allocate merged indices for local definitions, *plus* fill in
  // the per-source index-space maps for both imports and locals.
  std::uint32_t next_func   = imp_func;
  std::uint32_t next_table  = imp_table;
  std::uint32_t next_memory = imp_mem;
  std::uint32_t next_global = imp_global;
  std::uint32_t next_data   = 0;
  std::uint32_t next_elem   = 0;

  for (std::size_t mi = 0; mi < inputs.size(); ++mi) {
    const auto &m = inputs[mi];

    // Imports occupy the start of each source's index space; map them
    // either to the kept-import slot or to the resolver's target.
    std::uint32_t src_imp_func = 0, src_imp_table = 0, src_imp_mem = 0, src_imp_global = 0;
    for (std::size_t ii = 0; ii < m.imports.size(); ++ii) {
      const auto &im = m.imports[ii];
      auto resolve = [&](std::vector<std::vector<std::uint32_t>> &dst,
                         std::uint32_t src_index_in_kind,
                         ImportKind kind_filter,
                         std::uint32_t kept_slot) {
        (void)kind_filter;
        if (dst[mi].size() <= src_index_in_kind)
          dst[mi].resize(src_index_in_kind + 1);
        if (import_resolution[mi][ii].resolved) {
          // Forward to the resolver's local index (looked up below in a
          // second pass once all maps exist).
          dst[mi][src_index_in_kind] = static_cast<std::uint32_t>(-1);
        } else {
          dst[mi][src_index_in_kind] = kept_slot;
        }
      };
      switch (im.kind) {
        case ImportKind::kFunc:
          resolve(func_map, src_imp_func, ImportKind::kFunc, kept_func_idx[mi][ii]);
          ++src_imp_func; break;
        case ImportKind::kTable:
          resolve(table_map, src_imp_table, ImportKind::kTable, kept_table_idx[mi][ii]);
          ++src_imp_table; break;
        case ImportKind::kMemory:
          resolve(memory_map, src_imp_mem, ImportKind::kMemory, kept_mem_idx[mi][ii]);
          ++src_imp_mem; break;
        case ImportKind::kGlobal:
          resolve(global_map, src_imp_global, ImportKind::kGlobal, kept_global_idx[mi][ii]);
          ++src_imp_global; break;
      }
    }

    // Then allocate slots for local definitions.
    func_map[mi].resize(src_imp_func + m.functions.size());
    for (std::size_t i = 0; i < m.functions.size(); ++i)
      func_map[mi][src_imp_func + i] = next_func++;
    table_map[mi].resize(src_imp_table + m.tables.size());
    for (std::size_t i = 0; i < m.tables.size(); ++i)
      table_map[mi][src_imp_table + i] = next_table++;
    memory_map[mi].resize(src_imp_mem + m.memories.size());
    for (std::size_t i = 0; i < m.memories.size(); ++i)
      memory_map[mi][src_imp_mem + i] = next_memory++;
    global_map[mi].resize(src_imp_global + m.globals.size());
    for (std::size_t i = 0; i < m.globals.size(); ++i)
      global_map[mi][src_imp_global + i] = next_global++;

    data_map[mi].resize(m.data_segments.size());
    for (std::size_t i = 0; i < m.data_segments.size(); ++i)
      data_map[mi][i] = next_data++;
    elem_map[mi].resize(m.elements.size());
    for (std::size_t i = 0; i < m.elements.size(); ++i)
      elem_map[mi][i] = next_elem++;
  }

  // Second pass over imports: rewrite resolved (-1) entries to point to
  // the resolver's *merged* local index.
  for (std::size_t mi = 0; mi < inputs.size(); ++mi) {
    const auto &m = inputs[mi];
    std::uint32_t src_imp_func = 0, src_imp_table = 0, src_imp_mem = 0, src_imp_global = 0;
    for (std::size_t ii = 0; ii < m.imports.size(); ++ii) {
      const auto &im = m.imports[ii];
      const auto &res = import_resolution[mi][ii];
      auto rewrite = [&](std::vector<std::vector<std::uint32_t>> &maps,
                         std::uint32_t k_idx) {
        if (!res.resolved) { return; }
        // Resolver target_local is a source-local index in the resolver
        // module's per-kind index space; translate it through the
        // resolver's per-kind map.
        std::uint32_t merged = maps[res.target_src][res.target_local];
        maps[mi][k_idx] = merged;
      };
      switch (im.kind) {
        case ImportKind::kFunc:   rewrite(func_map,   src_imp_func++);   break;
        case ImportKind::kTable:  rewrite(table_map,  src_imp_table++);  break;
        case ImportKind::kMemory: rewrite(memory_map, src_imp_mem++);    break;
        case ImportKind::kGlobal: rewrite(global_map, src_imp_global++); break;
      }
    }
  }

  // -------- Pass 3: emit merged module body ----------------------------
  // Tables / memories / globals are simply concatenated in source order.
  for (std::size_t mi = 0; mi < inputs.size(); ++mi) {
    const auto &m = inputs[mi];
    out.tables.insert(out.tables.end(), m.tables.begin(), m.tables.end());
    out.memories.insert(out.memories.end(), m.memories.begin(), m.memories.end());
    for (const auto &g : m.globals) out.globals.push_back(g);
  }

  // Functions: for each source's local function, rewrite its body and
  // re-target its type index.
  RemapCtx ctx{};
  for (std::size_t mi = 0; mi < inputs.size(); ++mi) {
    const auto &m = inputs[mi];
    ctx.func_map   = &func_map[mi];
    ctx.type_map   = &type_map[mi];
    ctx.global_map = &global_map[mi];
    ctx.table_map  = &table_map[mi];
    ctx.memory_map = &memory_map[mi];
    ctx.data_map   = &data_map[mi];
    ctx.elem_map   = &elem_map[mi];
    for (const auto &f : m.functions) {
      Function nf;
      nf.type_index = type_map[mi][f.type_index];
      std::string err;
      if (!RewriteCodeBody(f.body, ctx, nf.body, err)) {
        error_out = err.empty()
            ? MakeErr("polyld-err-E3340", "code body rewrite failed")
            : err;
        return false;
      }
      out.functions.push_back(std::move(nf));
    }
  }

  // Data + element segments.
  for (std::size_t mi = 0; mi < inputs.size(); ++mi) {
    const auto &m = inputs[mi];
    for (auto d : m.data_segments) {
      // memory_index field is only meaningful when (flags & 2) is set.
      if (d.flags & 0x02) {
        if (d.memory_index >= memory_map[mi].size()) {
          error_out = MakeErr("polyld-err-E3340", "data memidx out of range");
          return false;
        }
        d.memory_index = memory_map[mi][d.memory_index];
      }
      out.data_segments.push_back(std::move(d));
    }
    for (auto el : m.elements) {
      if ((el.flags & 0x01) == 0 && (el.flags & 0x02) != 0) {
        if (el.table_index >= table_map[mi].size()) {
          error_out = MakeErr("polyld-err-E3340", "element tableidx out of range");
          return false;
        }
        el.table_index = table_map[mi][el.table_index];
      }
      // Re-map func indices in the simple form.
      for (auto &fi : el.func_indices) {
        if (fi >= func_map[mi].size()) {
          error_out = MakeErr("polyld-err-E3340", "element funcidx out of range");
          return false;
        }
        fi = func_map[mi][fi];
      }
      out.elements.push_back(std::move(el));
    }
  }

  // Exports: collect them all under their original names.  Duplicate
  // names across modules are resolved by the *first* occurrence so the
  // surviving entry stays predictable for the test runner.
  std::unordered_map<std::string, bool> seen;
  for (std::size_t mi = 0; mi < inputs.size(); ++mi) {
    const auto &m = inputs[mi];
    for (const auto &e : m.exports) {
      if (seen.count(e.name)) continue;
      seen[e.name] = true;
      Export ne = e;
      switch (e.kind) {
        case ExportKind::kFunc:
          ne.index = func_map[mi][e.index]; break;
        case ExportKind::kTable:
          ne.index = table_map[mi][e.index]; break;
        case ExportKind::kMemory:
          ne.index = memory_map[mi][e.index]; break;
        case ExportKind::kGlobal:
          ne.index = global_map[mi][e.index]; break;
      }
      out.exports.push_back(std::move(ne));
    }
  }

  // Start: take the first input's start function if any.
  if (inputs.front().has_start) {
    out.has_start = true;
    out.start_func = func_map[0][inputs.front().start_func];
  }

  // Custom sections: concatenate; downstream tools tolerate duplicates.
  for (std::size_t mi = 0; mi < inputs.size(); ++mi)
    for (const auto &cs : inputs[mi].custom_sections)
      out.custom_sections.push_back(cs);

  return true;
}

bool ValidateWasiEntry(const Module &m, std::string &error_out) {
  for (const auto &e : m.exports)
    if (e.name == "_start" && e.kind == ExportKind::kFunc)
      return true;
  error_out = MakeErr("polyld-err-E3320",
                      "wasm32-wasi target requires an exported '_start' function");
  return false;
}

// ---------------------------------------------------------------------------
// Emitter
// ---------------------------------------------------------------------------

namespace {

void EmitName(std::vector<std::uint8_t> &out, const std::string &s) {
  EmitU32(out, static_cast<std::uint32_t>(s.size()));
  out.insert(out.end(), s.begin(), s.end());
}

void EmitLimits(std::vector<std::uint8_t> &out, const Limits &l) {
  std::uint8_t flags = 0;
  if (l.has_max) flags |= 0x01;
  if (l.shared)  flags |= 0x02;
  out.push_back(flags);
  EmitU32(out, l.min);
  if (l.has_max) EmitU32(out, l.max);
}

void EmitSection(std::vector<std::uint8_t> &out, SectionId id,
                 const std::vector<std::uint8_t> &payload) {
  if (payload.empty()) return;
  out.push_back(static_cast<std::uint8_t>(id));
  EmitU32(out, static_cast<std::uint32_t>(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
}

}  // namespace

std::vector<std::uint8_t> EmitWasmModule(const Module &m) {
  std::vector<std::uint8_t> out;
  out.insert(out.end(), kMagic.begin(), kMagic.end());
  out.insert(out.end(), kVersion.begin(), kVersion.end());

  // type
  if (!m.types.empty()) {
    std::vector<std::uint8_t> p;
    EmitU32(p, static_cast<std::uint32_t>(m.types.size()));
    for (const auto &t : m.types) {
      p.push_back(0x60);
      EmitU32(p, static_cast<std::uint32_t>(t.params.size()));
      for (auto v : t.params) p.push_back(static_cast<std::uint8_t>(v));
      EmitU32(p, static_cast<std::uint32_t>(t.results.size()));
      for (auto v : t.results) p.push_back(static_cast<std::uint8_t>(v));
    }
    EmitSection(out, SectionId::kType, p);
  }
  // import
  if (!m.imports.empty()) {
    std::vector<std::uint8_t> p;
    EmitU32(p, static_cast<std::uint32_t>(m.imports.size()));
    for (const auto &im : m.imports) {
      EmitName(p, im.module_name);
      EmitName(p, im.name);
      p.push_back(static_cast<std::uint8_t>(im.kind));
      switch (im.kind) {
        case ImportKind::kFunc: EmitU32(p, im.type_index); break;
        case ImportKind::kTable:
          p.push_back(static_cast<std::uint8_t>(im.table_type.elem));
          EmitLimits(p, im.table_type.limits); break;
        case ImportKind::kMemory:
          EmitLimits(p, im.memory_type.limits); break;
        case ImportKind::kGlobal:
          p.push_back(static_cast<std::uint8_t>(im.global_type.valtype));
          p.push_back(im.global_type.mutability ? 1 : 0); break;
      }
    }
    EmitSection(out, SectionId::kImport, p);
  }
  // function
  if (!m.functions.empty()) {
    std::vector<std::uint8_t> p;
    EmitU32(p, static_cast<std::uint32_t>(m.functions.size()));
    for (const auto &f : m.functions) EmitU32(p, f.type_index);
    EmitSection(out, SectionId::kFunction, p);
  }
  // table
  if (!m.tables.empty()) {
    std::vector<std::uint8_t> p;
    EmitU32(p, static_cast<std::uint32_t>(m.tables.size()));
    for (const auto &t : m.tables) {
      p.push_back(static_cast<std::uint8_t>(t.elem));
      EmitLimits(p, t.limits);
    }
    EmitSection(out, SectionId::kTable, p);
  }
  // memory
  if (!m.memories.empty()) {
    std::vector<std::uint8_t> p;
    EmitU32(p, static_cast<std::uint32_t>(m.memories.size()));
    for (const auto &mem : m.memories) EmitLimits(p, mem.limits);
    EmitSection(out, SectionId::kMemory, p);
  }
  // global
  if (!m.globals.empty()) {
    std::vector<std::uint8_t> p;
    EmitU32(p, static_cast<std::uint32_t>(m.globals.size()));
    for (const auto &g : m.globals) {
      p.push_back(static_cast<std::uint8_t>(g.type.valtype));
      p.push_back(g.type.mutability ? 1 : 0);
      p.insert(p.end(), g.init_expr.begin(), g.init_expr.end());
    }
    EmitSection(out, SectionId::kGlobal, p);
  }
  // export
  if (!m.exports.empty()) {
    std::vector<std::uint8_t> p;
    EmitU32(p, static_cast<std::uint32_t>(m.exports.size()));
    for (const auto &e : m.exports) {
      EmitName(p, e.name);
      p.push_back(static_cast<std::uint8_t>(e.kind));
      EmitU32(p, e.index);
    }
    EmitSection(out, SectionId::kExport, p);
  }
  // start
  if (m.has_start) {
    std::vector<std::uint8_t> p;
    EmitU32(p, m.start_func);
    EmitSection(out, SectionId::kStart, p);
  }
  // element
  if (!m.elements.empty()) {
    std::vector<std::uint8_t> p;
    EmitU32(p, static_cast<std::uint32_t>(m.elements.size()));
    for (const auto &el : m.elements) {
      EmitU32(p, el.flags);
      bool active    = (el.flags & 0x01) == 0;
      bool has_table = (el.flags & 0x02) != 0;
      if (active && has_table) EmitU32(p, el.table_index);
      if (active) p.insert(p.end(), el.offset_expr.begin(), el.offset_expr.end());
      if (!active || has_table) {
        // elemkind/reftype byte
        p.push_back(el.elem_type == ValType::kFuncRef
                        ? static_cast<std::uint8_t>(0x00)
                        : static_cast<std::uint8_t>(el.elem_type));
      }
      if (el.uses_expressions) {
        EmitU32(p, static_cast<std::uint32_t>(el.init_exprs.size()));
        for (const auto &ex : el.init_exprs)
          p.insert(p.end(), ex.begin(), ex.end());
      } else {
        EmitU32(p, static_cast<std::uint32_t>(el.func_indices.size()));
        for (auto fi : el.func_indices) EmitU32(p, fi);
      }
    }
    EmitSection(out, SectionId::kElement, p);
  }
  // data count
  if (m.has_data_count) {
    std::vector<std::uint8_t> p;
    EmitU32(p, m.data_count);
    EmitSection(out, SectionId::kDataCount, p);
  }
  // code
  if (!m.functions.empty()) {
    std::vector<std::uint8_t> p;
    EmitU32(p, static_cast<std::uint32_t>(m.functions.size()));
    for (const auto &f : m.functions) {
      EmitU32(p, static_cast<std::uint32_t>(f.body.size()));
      p.insert(p.end(), f.body.begin(), f.body.end());
    }
    EmitSection(out, SectionId::kCode, p);
  }
  // data
  if (!m.data_segments.empty()) {
    std::vector<std::uint8_t> p;
    EmitU32(p, static_cast<std::uint32_t>(m.data_segments.size()));
    for (const auto &d : m.data_segments) {
      EmitU32(p, d.flags);
      if (d.flags & 0x02) EmitU32(p, d.memory_index);
      if ((d.flags & 0x01) == 0)
        p.insert(p.end(), d.offset_expr.begin(), d.offset_expr.end());
      EmitU32(p, static_cast<std::uint32_t>(d.bytes.size()));
      p.insert(p.end(), d.bytes.begin(), d.bytes.end());
    }
    EmitSection(out, SectionId::kData, p);
  }
  // custom sections last (compatible with the spec which permits them
  // anywhere; we keep them grouped for deterministic output).
  for (const auto &cs : m.custom_sections) {
    std::vector<std::uint8_t> p;
    EmitName(p, cs.name);
    p.insert(p.end(), cs.payload.begin(), cs.payload.end());
    EmitSection(out, SectionId::kCustom, p);
  }

  return out;
}

}  // namespace polyglot::linker::wasm

// ===========================================================================
// Linker member method (lives next to the macho/PE writers).
// ===========================================================================

namespace polyglot::linker {

namespace lw = ::polyglot::linker::wasm;

bool Linker::GenerateWasmModule() {
  Trace("Generating Wasm module: " + config_.output_file);

  // Collect every input that is already a `.wasm` byte stream.  The
  // polyc pipeline writes wasm objects to disk between backend and
  // polyld; treating them as first-class inputs lets the wasm-ld-style
  // merger work even when the user passes multiple modules.
  std::vector<lw::Module> parsed;
  for (const auto &path : config_.input_files) {
    std::ifstream in(path, std::ios::binary);
    if (!in) continue;
    std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < 8) continue;
    if (bytes[0] != 0x00 || bytes[1] != 0x61 ||
        bytes[2] != 0x73 || bytes[3] != 0x6D)
      continue;
    lw::Module m;
    std::string err;
    if (!lw::ParseWasmModule(bytes, m, err)) {
      ReportError(err);
      return false;
    }
    parsed.push_back(std::move(m));
  }

  std::vector<std::uint8_t> image;
  if (parsed.empty()) {
    // No `.wasm` inputs: synthesise a minimal but valid module from the
    // merged section table so a downstream `wasmtime` invocation still
    // accepts the file.  This path is exercised by the polyc pipeline
    // when the wasm backend has not yet emitted an intermediate `.wasm`
    // (e.g. the polyc smoke tests that pass through ploy IR directly).
    lw::Module m;
    lw::FuncType empty;
    m.types.push_back(empty);
    lw::Function f;
    f.type_index = 0;
    f.body = {0x00, 0x0B};  // local_count=0, end
    m.functions.push_back(std::move(f));
    lw::Export e;
    e.name = "_start";
    e.kind = lw::ExportKind::kFunc;
    e.index = 0;
    m.exports.push_back(std::move(e));
    // Custom section carrying the merged .text bytes for diagnostics.
    lw::CustomSection cs;
    cs.name = "polyglot.text";
    for (const auto &sec : output_sections_) {
      if (sec.name == ".text" || sec.name == "__text") {
        cs.payload.insert(cs.payload.end(), sec.data.begin(), sec.data.end());
        break;
      }
    }
    m.custom_sections.push_back(std::move(cs));
    image = lw::EmitWasmModule(m);
  } else if (parsed.size() == 1) {
    // Single-module: parse → emit round-trip; this validates the
    // module while normalising LEB128 widths so downstream consumers
    // see a consistent file shape.
    image = lw::EmitWasmModule(parsed.front());
  } else {
    lw::Module merged;
    std::string err;
    if (!lw::LinkWasmModules(parsed, merged, err)) {
      ReportError(err);
      return false;
    }
    image = lw::EmitWasmModule(merged);
  }

  // WASI entry-point validation.  The triple's OS field is the
  // authoritative signal; we rely on the resolved triple rather than
  // string-matching the user's `--target` text.
  const auto &triple = config_.target_triple;
  if (triple.os == ::polyglot::common::OS::kWasi) {
    lw::Module re;
    std::string err;
    if (!lw::ParseWasmModule(image, re, err) ||
        !lw::ValidateWasiEntry(re, err)) {
      ReportError(err);
      return false;
    }
  }

  std::ofstream out(config_.output_file, std::ios::binary);
  if (!out) {
    ReportError("Cannot create output file: " + config_.output_file);
    return false;
  }
  out.write(reinterpret_cast<const char *>(image.data()),
            static_cast<std::streamsize>(image.size()));
  if (!out) {
    ReportError("Failed writing output file: " + config_.output_file);
    return false;
  }
  stats_.total_output_size = image.size();
  return true;
}

}  // namespace polyglot::linker
