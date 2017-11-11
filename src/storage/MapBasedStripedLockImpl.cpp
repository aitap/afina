#include "MapBasedStripedLockImpl.h"
#include <functional>

namespace Afina {
namespace Backend {

MapBasedStripedLockImpl::MapBasedStripedLockImpl(size_t num_buckets, size_t max_size) {
    for (size_t i = 0; i < num_buckets; i++) {
        buckets.push_back(std::unique_ptr<MapBasedGlobalLockImpl>(new MapBasedGlobalLockImpl{max_size}));
    }
}

// See MapBasedStripedLockImpl.h
bool MapBasedStripedLockImpl::Put(const std::string &key, const std::string &value) {
    return buckets[std::hash<std::string>()(key) % buckets.size()]->Put(key, value);
}

// See MapBasedStripedLockImpl.h
bool MapBasedStripedLockImpl::PutIfAbsent(const std::string &key, const std::string &value) {
    return buckets[std::hash<std::string>()(key) % buckets.size()]->PutIfAbsent(key, value);
}

// See MapBasedStripedLockImpl.h
bool MapBasedStripedLockImpl::Get(const std::string &key, std::string &value) const {
    return buckets[std::hash<std::string>()(key) % buckets.size()]->Get(key, value);
}

// See MapBasedStripedLockImpl.h
bool MapBasedStripedLockImpl::Set(const std::string &key, const std::string &value) {
    return buckets[std::hash<std::string>()(key) % buckets.size()]->Set(key, value);
}

// See MapBasedStripedLockImpl.h
bool MapBasedStripedLockImpl::Delete(const std::string &key) {
    return buckets[std::hash<std::string>()(key) % buckets.size()]->Delete(key);
}

} // namespace Backend
} // namespace Afina
