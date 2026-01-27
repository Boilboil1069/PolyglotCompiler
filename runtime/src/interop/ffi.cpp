#include "runtime/include/interop/ffi.h"

using namespace polyglot::runtime::interop;

namespace polyglot::runtime::interop {

ForeignFunction Bind(const std::string &name, void *address) {
  ForeignFunction fn{name, address};
  fn.signature.convention = {CallingConventionKind::kCDecl, Endianness::kLittle, false};
  return fn;
}

ForeignObject *BindBorrowed(const std::string &name, void *address, size_t size) {
  (void)name;  // name retained for future lookup; unused for now
  return AcquireBorrowed(address, size);
}

ForeignObject *BindOwned(const std::string &name, void *address, size_t size,
                         std::function<void(void *)> deleter) {
  (void)name;
  return AcquireOwned(address, size, std::move(deleter));
}

ForeignObject *BindShared(const std::string &name, void *address, size_t size,
                          std::function<void(void *)> deleter) {
  (void)name;
  return AcquireShared(address, size, std::move(deleter));
}

ForeignFunction BindWithSignature(const std::string &name, void *address, const ForeignSignature &sig,
                                  Ownership ownership) {
  (void)ownership;  // ownership is currently tracked on the handle rather than symbol, placeholder
  ForeignFunction fn{name, address, sig};
  return fn;
}

}  // namespace polyglot::runtime::interop
