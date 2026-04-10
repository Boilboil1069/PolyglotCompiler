/**
 * @file     abi.h
 * @brief    Shared backend infrastructure
 *
 * @ingroup  Backend / Common
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <string>

namespace polyglot::backends {

/** @brief ABI data structure. */
struct ABI {
  std::string name;
  size_t pointer_size{8};
};

}  // namespace polyglot::backends
