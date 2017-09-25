#include "MapBasedGlobalLockImpl.h"

namespace Afina {
namespace Backend {

// TODO: implement the max size logic

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Put(const std::string &key, const std::string &value) {
    std::lock_guard<std::mutex> lock(_lock);
    bool ret = _backend.count(key);
    _backend[key] = value;
    return ret; // I *guess* it should return whether there was an element?
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::PutIfAbsent(const std::string &key, const std::string &value) {
    std::lock_guard<std::mutex> lock(_lock);
    auto it = _backend.find(key);
    if (it == _backend.end()) {
        _backend[key] = value;
        return true;
    } else {
        return false;
    }
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Get(const std::string &key, std::string &value) const {
    std::lock_guard<std::mutex> lock(_lock);
    try {
        value = _backend.at(key);
        return true;
    } catch (std::out_of_range &e) {
        return false;
    }
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Set(const std::string &key, const std::string &value) {
    std::lock_guard<std::mutex> lock(_lock);
    auto it = _backend.find(key);
    if (it == _backend.end())
        return false;
    _backend[key] = value;
    return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Delete(const std::string &key) {
    std::lock_guard<std::mutex> lock(_lock);
    // I *guess* it should return if the key was present in the first place?
    return _backend.erase(key);
}

} // namespace Backend
} // namespace Afina
