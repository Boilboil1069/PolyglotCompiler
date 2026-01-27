#include "runtime/include/services/threading.h"

namespace polyglot::runtime::services {

thread_local std::unordered_map<std::string, void *> ThreadLocalStorage::storage_;

}  // namespace polyglot::runtime::services
