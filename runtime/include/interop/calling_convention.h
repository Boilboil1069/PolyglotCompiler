/**
 * @file     calling_convention.h
 * @brief    Cross-language interoperability
 *
 * @ingroup  Runtime / Interop
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <string>
#include <vector>

#include "runtime/include/interop/marshalling.h"
#include "runtime/include/interop/type_mapping.h"

namespace polyglot::runtime::interop {

/** @brief CallingConventionKind enumeration. */
enum class CallingConventionKind { kCDecl, kStdCall, kFastCall, kSysV, kWin64 };

/** @brief CallingConvention data structure. */
struct CallingConvention {
  CallingConventionKind kind{CallingConventionKind::kCDecl};
  Endianness endianness{Endianness::kLittle};
  bool returns_via_stack{false};
};

/** @brief ForeignSignature data structure. */
struct ForeignSignature {
  CallingConvention convention;
  std::vector<ABIType> args;
  ABIType result;
};

// Minimal validation ensuring sizes are non-zero and alignments are sane.
bool ValidateSignature(const ForeignSignature &sig);

}  // namespace polyglot::runtime::interop
