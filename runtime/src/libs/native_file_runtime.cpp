/**
 * @file     native_file_runtime.cpp
 * @brief    Relocation-free native implementation of the file integer API.
 */

#include "runtime/include/libs/native_file_runtime.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace polyglot::runtime {
namespace {

#include "runtime/src/libs/native/arm64_runtime.inc"

NativeFileRuntimeBlob BuildArm64(bool darwin) {
  NativeFileRuntimeBlob blob;
  const auto *words = darwin ? kArm64_darwin : kArm64_linux;
  const auto count = darwin ? sizeof(kArm64_darwin) / 4 : sizeof(kArm64_linux) / 4;
  for (std::size_t i = 0; i < count; ++i)
    for (unsigned shift = 0; shift < 32; shift += 8)
      blob.text.push_back((words[i] >> shift) & 255);
  if (darwin) blob.symbols.assign(std::begin(kArm64_darwin_symbols), std::end(kArm64_darwin_symbols));
  else blob.symbols.assign(std::begin(kArm64_linux_symbols), std::end(kArm64_linux_symbols));
  return blob;
}

enum class FixupKind { kRel32 };

struct Fixup {
  std::size_t displacement_offset{0};
  std::string label;
  FixupKind kind{FixupKind::kRel32};
};

// Tiny purpose-built encoder.  Keeping labels symbolic makes the parser body
// reviewable while still shipping literal machine code rather than invoking an
// assembler during compilation.
class X86CodeBuilder {
public:
  std::size_t Offset() const { return bytes_.size(); }

  void Byte(std::uint8_t value) { bytes_.push_back(value); }

  void Bytes(std::initializer_list<std::uint8_t> values) {
    bytes_.insert(bytes_.end(), values.begin(), values.end());
  }

  void U32(std::uint32_t value) {
    for (unsigned shift = 0; shift != 32; shift += 8)
      Byte(static_cast<std::uint8_t>((value >> shift) & 0xffu));
  }

  void Label(const std::string &name) { labels_[name] = Offset(); }

  void Jmp(const std::string &label) {
    Byte(0xe9);
    Rel32(label);
  }

  // Near conditional branch: 0F 8<condition> rel32.
  void Jcc(std::uint8_t condition, const std::string &label) {
    Bytes({0x0f, static_cast<std::uint8_t>(0x80u | condition)});
    Rel32(label);
  }

  bool Finish(std::vector<std::uint8_t> &out, std::string *error) {
    for (const auto &fixup : fixups_) {
      const auto it = labels_.find(fixup.label);
      if (it == labels_.end()) {
        if (error)
          *error = "native file runtime has an unresolved internal label '" + fixup.label + "'";
        return false;
      }
      const std::int64_t from = static_cast<std::int64_t>(fixup.displacement_offset + 4);
      const std::int64_t to = static_cast<std::int64_t>(it->second);
      const std::int64_t delta = to - from;
      if (delta < INT32_MIN || delta > INT32_MAX) {
        if (error)
          *error = "native file runtime internal branch is out of range";
        return false;
      }
      const std::uint32_t encoded = static_cast<std::uint32_t>(static_cast<std::int32_t>(delta));
      for (unsigned i = 0; i != 4; ++i)
        bytes_[fixup.displacement_offset + i] =
            static_cast<std::uint8_t>((encoded >> (i * 8)) & 0xffu);
    }
    out = std::move(bytes_);
    return true;
  }

private:
  void Rel32(const std::string &label) {
    const std::size_t at = Offset();
    U32(0);
    fixups_.push_back({at, label, FixupKind::kRel32});
  }

  std::vector<std::uint8_t> bytes_;
  std::unordered_map<std::string, std::size_t> labels_;
  std::vector<Fixup> fixups_;
};

std::string Fold(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

void EmitMovEaxImm32(X86CodeBuilder &b, std::uint32_t value) {
  b.Byte(0xb8);
  b.U32(value);
}

// Emit read(fd=r8, buffer=rsp, count=1).  On success EAX is reloaded with
// the unsigned input byte.  Error/EOF branches to `failure`.  Darwin reports
// syscall errors through CF, while Linux returns a negative errno.
void EmitReadByte(X86CodeBuilder &b, std::uint32_t read_nr, bool darwin,
                  const std::string &failure) {
  EmitMovEaxImm32(b, read_nr);        // mov eax, read_nr
  b.Bytes({0x4c, 0x89, 0xc7});        // mov rdi, r8
  b.Bytes({0x48, 0x89, 0xe6});        // mov rsi, rsp
  b.Bytes({0xba, 0x01, 0, 0, 0});    // mov edx, 1
  b.Bytes({0x0f, 0x05});              // syscall
  if (darwin)
    b.Jcc(0x2, failure);              // jc failure
  b.Bytes({0x48, 0x83, 0xf8, 0x01});  // cmp rax, 1
  b.Jcc(0x5, failure);                // jne failure
  b.Bytes({0x0f, 0xb6, 0x04, 0x24});  // movzx eax, byte ptr [rsp]
}

NativeFileRuntimeBlob BuildX86(bool darwin, std::string *error) {
  // Darwin's UNIX syscall class is 0x02000000.  Linux x86_64 uses the
  // traditional open/read/write/close syscall numbers 2/0/1/3.
  const std::uint32_t open_nr = darwin ? 0x02000005u : 2u;
  const std::uint32_t read_nr = darwin ? 0x02000003u : 0u;
  const std::uint32_t write_nr = darwin ? 0x02000004u : 1u;
  const std::uint32_t close_nr = darwin ? 0x02000006u : 3u;
  // O_WRONLY | O_CREAT | O_TRUNC differs between Darwin and Linux.
  const std::uint32_t write_open_flags = darwin ? 0x0601u : 0x0241u;
  constexpr std::uint32_t kCreateMode0644 = 0644u;

  X86CodeBuilder b;
  NativeFileRuntimeBlob blob;

  const auto begin_symbol = [&](const char *name) {
    blob.symbols.push_back({name, b.Offset(), 0});
  };
  const auto end_symbol = [&]() {
    auto &symbol = blob.symbols.back();
    symbol.size = b.Offset() - symbol.offset;
  };

  // long polyrt_open_read(const char *path)
  begin_symbol(kFileOpenReadSymbol);
  EmitMovEaxImm32(b, open_nr);
  b.Bytes({0x31, 0xf6});               // xor esi, esi  (O_RDONLY)
  b.Bytes({0x31, 0xd2});               // xor edx, edx  (mode = 0)
  b.Bytes({0x0f, 0x05});               // syscall
  if (darwin) {
    b.Jcc(0x3, "open_ok");            // jnc open_ok
    b.Bytes({0x48, 0xc7, 0xc0, 0xff, 0xff, 0xff, 0xff}); // mov rax, -1
    b.Label("open_ok");
  }
  b.Byte(0xc3);                        // ret
  end_symbol();

  // long polyrt_read_i64_or(long fd, long eof_value)
  begin_symbol(kFileNextIntSymbol);
  b.Bytes({0x48, 0x83, 0xec, 0x08});   // sub rsp, 8 (byte + sign flag)
  b.Bytes({0x49, 0x89, 0xf8});         // mov r8, rdi (fd)
  b.Bytes({0x49, 0x89, 0xf1});         // mov r9, rsi (fallback)

  b.Label("scan_read");
  EmitReadByte(b, read_nr, darwin, "return_fallback");
  b.Label("classify");
  b.Bytes({0x3c, 0x2d});               // cmp al, '-'
  b.Jcc(0x4, "minus");                // je minus
  b.Bytes({0x3c, 0x30});               // cmp al, '0'
  b.Jcc(0x2, "scan_read");            // jb scan_read
  b.Bytes({0x3c, 0x39});               // cmp al, '9'
  b.Jcc(0x7, "scan_read");            // ja scan_read
  b.Bytes({0xc6, 0x44, 0x24, 0x01, 0x00}); // mov byte [rsp+1], 0
  b.Bytes({0x45, 0x31, 0xd2});         // xor r10d, r10d
  b.Jmp("accumulate");

  b.Label("minus");
  b.Bytes({0xc6, 0x44, 0x24, 0x01, 0x01}); // mov byte [rsp+1], 1
  EmitReadByte(b, read_nr, darwin, "return_fallback");
  b.Bytes({0x3c, 0x30});               // cmp al, '0'
  b.Jcc(0x2, "classify");             // jb classify
  b.Bytes({0x3c, 0x39});               // cmp al, '9'
  b.Jcc(0x7, "classify");             // ja classify
  b.Bytes({0x45, 0x31, 0xd2});         // xor r10d, r10d

  b.Label("accumulate");
  b.Bytes({0x4d, 0x6b, 0xd2, 0x0a});   // imul r10, r10, 10
  b.Bytes({0x83, 0xe8, 0x30});         // sub eax, '0'
  b.Bytes({0x49, 0x01, 0xc2});         // add r10, rax
  EmitReadByte(b, read_nr, darwin, "finish_value");
  b.Bytes({0x3c, 0x30});               // cmp al, '0'
  b.Jcc(0x2, "finish_value");          // jb finish_value
  b.Bytes({0x3c, 0x39});               // cmp al, '9'
  b.Jcc(0x6, "accumulate");            // jbe accumulate

  b.Label("finish_value");
  b.Bytes({0x4c, 0x89, 0xd0});         // mov rax, r10
  b.Bytes({0x80, 0x7c, 0x24, 0x01, 0x00}); // cmp byte [rsp+1], 0
  b.Jcc(0x4, "return_value");          // je return_value
  b.Bytes({0x48, 0xf7, 0xd8});         // neg rax
  b.Label("return_value");
  b.Bytes({0x48, 0x83, 0xc4, 0x08});   // add rsp, 8
  b.Byte(0xc3);                        // ret

  b.Label("return_fallback");
  b.Bytes({0x4c, 0x89, 0xc8});         // mov rax, r9
  b.Bytes({0x48, 0x83, 0xc4, 0x08});   // add rsp, 8
  b.Byte(0xc3);                        // ret
  end_symbol();

  // long polyrt_close_read(long fd)
  begin_symbol(kFileCloseReadSymbol);
  EmitMovEaxImm32(b, close_nr);
  b.Bytes({0x0f, 0x05});               // syscall
  if (darwin) {
    b.Jcc(0x3, "close_ok");           // jnc close_ok
    b.Bytes({0x48, 0xc7, 0xc0, 0xff, 0xff, 0xff, 0xff}); // mov rax, -1
    b.Label("close_ok");
  }
  b.Byte(0xc3);                        // ret
  end_symbol();

  // long polyrt_open_write(const char *path)
  begin_symbol(kFileOpenWriteSymbol);
  EmitMovEaxImm32(b, open_nr);
  b.Byte(0xbe);                         // mov esi, O_WRONLY|O_CREAT|O_TRUNC
  b.U32(write_open_flags);
  b.Byte(0xba);                         // mov edx, 0644
  b.U32(kCreateMode0644);
  b.Bytes({0x0f, 0x05});               // syscall
  if (darwin) {
    b.Jcc(0x3, "open_write_ok");      // jnc open_write_ok
    b.Bytes({0x48, 0xc7, 0xc0, 0xff, 0xff, 0xff, 0xff}); // mov rax, -1
    b.Label("open_write_ok");
  }
  b.Byte(0xc3);                        // ret
  end_symbol();

  // long polyrt_write_text(long fd, const char *text)
  // Scan the terminating NUL in-process, then issue one write syscall.
  begin_symbol(kFileWriteTextSymbol);
  b.Bytes({0x48, 0x85, 0xf6});         // test rsi, rsi
  b.Jcc(0x4, "write_text_error");     // je write_text_error
  b.Bytes({0x49, 0x89, 0xf8});         // mov r8, rdi (fd)
  b.Bytes({0x48, 0x89, 0xf1});         // mov rcx, rsi (scan cursor)
  b.Label("write_text_scan");
  b.Bytes({0x80, 0x39, 0x00});         // cmp byte ptr [rcx], 0
  b.Jcc(0x4, "write_text_ready");     // je write_text_ready
  b.Bytes({0x48, 0xff, 0xc1});         // inc rcx
  b.Jmp("write_text_scan");
  b.Label("write_text_ready");
  b.Bytes({0x48, 0x29, 0xf1});         // sub rcx, rsi (length)
  b.Bytes({0x48, 0x89, 0xca});         // mov rdx, rcx
  EmitMovEaxImm32(b, write_nr);
  b.Bytes({0x4c, 0x89, 0xc7});         // mov rdi, r8
  b.Bytes({0x0f, 0x05});               // syscall
  if (darwin)
    b.Jcc(0x2, "write_text_error");   // jc write_text_error
  b.Byte(0xc3);                        // ret
  b.Label("write_text_error");
  b.Bytes({0x48, 0xc7, 0xc0, 0xff, 0xff, 0xff, 0xff}); // mov rax, -1
  b.Byte(0xc3);                        // ret
  end_symbol();

  // long polyrt_write_i64(long fd, long value, long separator)
  // Build the decimal representation backwards in a stack buffer.  Unsigned
  // division after two's-complement negation deliberately handles INT64_MIN.
  begin_symbol(kFileWriteIntSymbol);
  b.Bytes({0x48, 0x83, 0xec, 0x28});   // sub rsp, 40
  b.Bytes({0x49, 0x89, 0xf8});         // mov r8, rdi (fd)
  b.Bytes({0x48, 0x89, 0xf0});         // mov rax, rsi (value/magnitude)
  b.Bytes({0x49, 0x89, 0xd1});         // mov r9, rdx (separator)
  b.Bytes({0x4c, 0x8d, 0x54, 0x24, 0x28}); // lea r10, [rsp+40] (buffer end)
  b.Bytes({0x45, 0x31, 0xdb});         // xor r11d, r11d (length)
  b.Bytes({0x4d, 0x85, 0xc9});         // test r9, r9
  b.Jcc(0x4, "write_i64_separator_done"); // je separator_done
  b.Bytes({0x49, 0xff, 0xca});         // dec r10
  b.Bytes({0x45, 0x88, 0x0a});         // mov byte ptr [r10], r9b
  b.Bytes({0x49, 0xff, 0xc3});         // inc r11
  b.Label("write_i64_separator_done");

  b.Bytes({0x31, 0xc9});               // xor ecx, ecx (negative flag)
  b.Bytes({0x48, 0x85, 0xc0});         // test rax, rax
  b.Jcc(0x9, "write_i64_magnitude_ready"); // jns magnitude_ready
  b.Bytes({0xb1, 0x01});               // mov cl, 1
  b.Bytes({0x48, 0xf7, 0xd8});         // neg rax
  b.Label("write_i64_magnitude_ready");
  b.Bytes({0x48, 0x85, 0xc0});         // test rax, rax
  b.Jcc(0x5, "write_i64_nonzero");    // jne nonzero
  b.Bytes({0x49, 0xff, 0xca});         // dec r10
  b.Bytes({0x41, 0xc6, 0x02, 0x30});   // mov byte ptr [r10], '0'
  b.Bytes({0x49, 0xff, 0xc3});         // inc r11
  b.Jmp("write_i64_sign");

  b.Label("write_i64_nonzero");
  b.Byte(0xbe);                         // mov esi, 10
  b.U32(10u);
  b.Label("write_i64_digits");
  b.Bytes({0x31, 0xd2});               // xor edx, edx
  b.Bytes({0x48, 0xf7, 0xf6});         // div rsi
  b.Bytes({0x80, 0xc2, 0x30});         // add dl, '0'
  b.Bytes({0x49, 0xff, 0xca});         // dec r10
  b.Bytes({0x41, 0x88, 0x12});         // mov byte ptr [r10], dl
  b.Bytes({0x49, 0xff, 0xc3});         // inc r11
  b.Bytes({0x48, 0x85, 0xc0});         // test rax, rax
  b.Jcc(0x5, "write_i64_digits");     // jne digits

  b.Label("write_i64_sign");
  b.Bytes({0x84, 0xc9});               // test cl, cl
  b.Jcc(0x4, "write_i64_ready");      // je ready
  b.Bytes({0x49, 0xff, 0xca});         // dec r10
  b.Bytes({0x41, 0xc6, 0x02, 0x2d});   // mov byte ptr [r10], '-'
  b.Bytes({0x49, 0xff, 0xc3});         // inc r11

  b.Label("write_i64_ready");
  EmitMovEaxImm32(b, write_nr);
  b.Bytes({0x4c, 0x89, 0xc7});         // mov rdi, r8
  b.Bytes({0x4c, 0x89, 0xd6});         // mov rsi, r10
  b.Bytes({0x4c, 0x89, 0xda});         // mov rdx, r11
  b.Bytes({0x0f, 0x05});               // syscall
  if (darwin)
    b.Jcc(0x2, "write_i64_error");    // jc write_i64_error
  b.Bytes({0x48, 0x83, 0xc4, 0x28});   // add rsp, 40
  b.Byte(0xc3);                        // ret
  b.Label("write_i64_error");
  b.Bytes({0x48, 0xc7, 0xc0, 0xff, 0xff, 0xff, 0xff}); // mov rax, -1
  b.Bytes({0x48, 0x83, 0xc4, 0x28});   // add rsp, 40
  b.Byte(0xc3);                        // ret
  end_symbol();

  if (!b.Finish(blob.text, error))
    return {};
  return blob;
}

} // namespace

NativeFileRuntimeBlob BuildNativeFileRuntime(const std::string &target_arch,
                                             const std::string &target_os,
                                             std::string *error) {
  if (error)
    error->clear();
  const std::string arch = Fold(target_arch);
  const std::string os = Fold(target_os);
  const bool x86 = arch == "x86_64" || arch == "x64" || arch == "amd64";
  const bool darwin = os == "darwin" || os == "macos" || os == "mac" || os == "osx";
  const bool linux = os == "linux" || os == "gnu";
  if ((arch == "arm64" || arch == "aarch64") && (darwin || linux))
    return BuildArm64(darwin);
  if (x86 && (darwin || linux))
    return BuildX86(darwin, error);

  if (error) {
    *error = "automatic file runtime is not available for target '" + target_arch + "-" +
             target_os + "' (supported: x86_64/arm64 on Linux and Darwin)";
  }
  return {};
}

bool IsNativeFileRuntimeSymbol(const std::string &name) {
  return name == kFileOpenReadSymbol || name == kFileNextIntSymbol ||
         name == kFileCloseReadSymbol || name == kFileOpenWriteSymbol ||
         name == kFileWriteTextSymbol || name == kFileWriteIntSymbol ||
         name == "polyrt_array_new" || name == "polyrt_array_len" ||
         name == "polyrt_array_get" || name == "polyrt_array_set" ||
         name == "polyrt_array_free" || name == "polyrt_arg_text" || name == "polyrt_arg_int";
}

} // namespace polyglot::runtime
