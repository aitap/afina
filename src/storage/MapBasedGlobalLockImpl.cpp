#include "MapBasedGlobalLockImpl.h"
#include <algorithm>

#include <mutex>

namespace Afina {
namespace Backend {

// hash complexities given are average, not worst (worst for hashes is O(n))
// list is simple enough not to have complex worst case scenarios

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Put(const std::string &key, const std::string &value) {
    std::lock_guard<std::mutex> lock(_lock);
    return MapBasedNoLockImpl::Put(key, value);
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::PutIfAbsent(const std::string &key, const std::string &value) {
    std::lock_guard<std::mutex> lock(_lock);
    return MapBasedNoLockImpl::PutIfAbsent(key, value);
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Get(const std::string &key, std::string &value) const {
    std::lock_guard<std::mutex> lock(_lock);
    return MapBasedNoLockImpl::Get(key, value);
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Set(const std::string &key, const std::string &value) {
    std::lock_guard<std::mutex> lock(_lock);
    return MapBasedNoLockImpl::Set(key, value);
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Delete(const std::string &key) {
    std::lock_guard<std::mutex> lock(_lock);
    return MapBasedNoLockImpl::Delete(key);
}

} // namespace Backend
} // namespace Afina
