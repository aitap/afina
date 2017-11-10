#include "MapBasedStripedLockImpl.h"
#include <functional>

namespace Afina {
namespace Backend {

MapBasedStripedLockImpl::MapBasedStripedLockImpl(size_t num_buckets, size_t max_size) {
    for (size_t i = 0; i < num_buckets; i++) {
        bucket_access.push_back(buckets.emplace(buckets.end(), max_size));
    }
}

// See MapBasedStripedLockImpl.h
bool MapBasedStripedLockImpl::Put(const std::string &key, const std::string &value) {
    return bucket_access[std::hash<std::string>()(key) % buckets.size()]->Put(key, value);
}

// See MapBasedStripedLockImpl.h
bool MapBasedStripedLockImpl::PutIfAbsent(const std::string &key, const std::string &value) {
    return bucket_access[std::hash<std::string>()(key) % buckets.size()]->PutIfAbsent(key, value);
}

// See MapBasedStripedLockImpl.h
bool MapBasedStripedLockImpl::Get(const std::string &key, std::string &value) const {
    return bucket_access[std::hash<std::string>()(key) % buckets.size()]->Get(key, value);
}

// See MapBasedStripedLockImpl.h
bool MapBasedStripedLockImpl::Set(const std::string &key, const std::string &value) {
    return bucket_access[std::hash<std::string>()(key) % buckets.size()]->Set(key, value);
}

// See MapBasedStripedLockImpl.h
bool MapBasedStripedLockImpl::Delete(const std::string &key) {
    return bucket_access[std::hash<std::string>()(key) % buckets.size()]->Delete(key);
}

} // namespace Backend
} // namespace Afina
