#include "MapBasedStripedLockImpl.h"
#include <functional> // hash

namespace Afina {
namespace Backend {

MapBasedStripedLockImpl::MapBasedStripedLockImpl(size_t num_buckets_, size_t max_size_)
    : num_buckets(num_buckets_), max_size(max_size_), count(0) {
    locks = new std::mutex[num_buckets];
    for (size_t i = 0; i < num_buckets; i++) {
        buckets.emplace(buckets.end(), max_size);
    }
}

// See MapBasedStripedLockImpl.h
bool MapBasedStripedLockImpl::Put(const std::string &key, const std::string &value) {
    auto idx = std::hash<std::string>()(key) % num_buckets;
    std::lock_guard<std::mutex> lock{locks[idx]};
    if (count.load() >= max_size) {          // uh oh, can't allow adding elements
        return buckets[idx].Set(key, value); // Set will return false if value doesn't exist
                                             // either way, the count didn't change
    }
    size_t old = buckets[idx].size();
    bool ret = buckets[idx].Put(key, value);
    count += buckets[idx].size() - old;
    return ret;
}

// See MapBasedStripedLockImpl.h
bool MapBasedStripedLockImpl::PutIfAbsent(const std::string &key, const std::string &value) {
    auto idx = std::hash<std::string>()(key) % num_buckets;
    std::lock_guard<std::mutex> lock{locks[idx]};
    if (count.load() >= max_size) {
        // if there was a value, we don't allow a Put
        // if there wasn't, we can't grow storage anyway
        return false;
    }
    size_t old = buckets[idx].size();
    bool ret = buckets[idx].PutIfAbsent(key, value);
    count += buckets[idx].size() - old;
    return ret;
}

// See MapBasedStripedLockImpl.h
bool MapBasedStripedLockImpl::Get(const std::string &key, std::string &value) const {
    auto idx = std::hash<std::string>()(key) % num_buckets;
    std::lock_guard<std::mutex> lock{locks[idx]};
    // Guaranteed not to change the count
    return buckets[idx].Get(key, value);
}

// See MapBasedStripedLockImpl.h
bool MapBasedStripedLockImpl::Set(const std::string &key, const std::string &value) {
    auto idx = std::hash<std::string>()(key) % num_buckets;
    std::lock_guard<std::mutex> lock{locks[idx]};
    // Guaranteed not to change the count
    return buckets[idx].Set(key, value);
}

// See MapBasedStripedLockImpl.h
bool MapBasedStripedLockImpl::Delete(const std::string &key) {
    auto idx = std::hash<std::string>()(key) % num_buckets;
    std::lock_guard<std::mutex> lock{locks[idx]};
    size_t old = buckets[idx].size();
    bool ret = buckets[idx].Delete(key);
    count += buckets[idx].size() - old;
    return ret;
}

MapBasedStripedLockImpl::~MapBasedStripedLockImpl() { delete[] locks; }

} // namespace Backend
} // namespace Afina
