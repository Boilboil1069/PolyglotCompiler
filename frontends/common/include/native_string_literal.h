#pragma once

#include <cstdint>
#include <string>

namespace polyglot::frontends {
// Decode only literal bytes at the explicit native C-string API boundary.
// Ordinary Go strings and C++ wide strings retain their own language semantics.
inline bool DecodeNativeString(const std::string &body, bool raw, bool go,
                               std::string &out, std::string &error) {
  out.clear();
  auto fail = [&](const std::string &message) { error = message; return false; };
  auto digit = [](char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < body.size(); ++i) {
    char c = body[i];
    if (raw || c != '\\') {
      if (!(go && raw && c == '\r')) out.push_back(c);
      continue;
    }
    if (++i == body.size()) return fail("unfinished string escape");
    c = body[i];
    switch (c) {
    case 'n': out.push_back('\n'); break;
    case 'r': out.push_back('\r'); break;
    case 't': out.push_back('\t'); break;
    case 'a': out.push_back('\a'); break;
    case 'b': out.push_back('\b'); break;
    case 'f': out.push_back('\f'); break;
    case 'v': out.push_back('\v'); break;
    case '\\': case '"': out.push_back(c); break;
    case '\'': case '?':
      if (go) return fail("unsupported Go string escape");
      out.push_back(c); break;
    case 'u': case 'U': {
      const size_t count = c == 'u' ? 4 : 8;
      uint32_t value = 0;
      for (size_t n = 0; n < count; ++n) {
        if (++i == body.size() || digit(body[i]) < 0) return fail("invalid Unicode string escape");
        value = (value << 4) | static_cast<uint32_t>(digit(body[i]));
      }
      if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return fail("invalid Unicode scalar");
      if (value <= 0x7f) out.push_back(static_cast<char>(value));
      else if (value <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (value >> 6)));
        out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
      } else if (value <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (value >> 12)));
        out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
      } else {
        out.push_back(static_cast<char>(0xf0 | (value >> 18)));
        out.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
      }
      break;
    }
    default: {
      const bool hex = c == 'x';
      if (!hex && (c < '0' || c > '7')) return fail("unsupported string escape");
      unsigned value = hex ? 0 : static_cast<unsigned>(c - '0');
      size_t count = hex ? 0 : 1;
      const size_t limit = hex ? (go ? 2 : body.size()) : 3;
      while (count < limit && i + 1 < body.size()) {
        const int d = digit(body[i + 1]);
        if (d < 0 || d >= (hex ? 16 : 8)) break;
        if (value > 255u / (hex ? 16 : 8)) return fail("string byte escape exceeds 255");
        value = value * (hex ? 16 : 8) + static_cast<unsigned>(d);
        ++i; ++count;
      }
      if ((hex && count == 0) || (go && count != (hex ? 2 : 3)) || value > 255)
        return fail("invalid byte string escape");
      out.push_back(static_cast<char>(value));
    }
    }
  }
  if (out.find('\0') != std::string::npos)
    return fail("native string APIs do not accept embedded NUL bytes");
  return true;
}
} // namespace polyglot::frontends
