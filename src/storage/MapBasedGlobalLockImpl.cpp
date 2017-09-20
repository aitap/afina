#include "MapBasedGlobalLockImpl.h"

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
const std::string &MapBasedGlobalLockImpl::Put(const std::string &key, const std::string &value) {
    return backend[key] = value;
}

// See MapBasedGlobalLockImpl.h
const std::string &MapBasedGlobalLockImpl::PutIfAbsent(const std::string &key, const std::string &value) {
    try { // might be grossly inefficient
        return backend.at(key);
    } catch (std::out_of_range &e) {
        backend[key] = value;
        return value;
    }
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Get(const std::string &key, std::string &value) {
    try {
        value = backend.at(key);
        return true;
    } catch (std::out_of_range &e) {
        return false;
    }
}

} // namespace MapBasedGlobalLockImpl
} // namespace Afina
