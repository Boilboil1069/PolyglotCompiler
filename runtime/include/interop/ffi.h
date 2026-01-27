#pragma once

#include <functional>
#include <string>
#include <vector>

#include "runtime/include/interop/calling_convention.h"
#include "runtime/include/interop/memory.h"

namespace polyglot::runtime::interop {

struct ForeignFunction {
  std::string name;
  void *address{nullptr};
  ForeignSignature signature;
};

ForeignFunction Bind(const std::string &name, void *address);
ForeignObject *BindBorrowed(const std::string &name, void *address, size_t size);
ForeignObject *BindOwned(const std::string &name, void *address, size_t size,
                         std::function<void(void *)> deleter);
ForeignObject *BindShared(const std::string &name, void *address, size_t size,
                          std::function<void(void *)> deleter);

// Build a ForeignFunction with a signature and address, tagging ownership of the symbol handle.
ForeignFunction BindWithSignature(const std::string &name, void *address, const ForeignSignature &sig,
                                  Ownership ownership = Ownership::kBorrowed);

}  // namespace polyglot::runtime::interop
