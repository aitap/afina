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
    size_t cur_size = count.load();
    do {
        if (cur_size >= max_size) {
            if (!buckets[idx].evict_oldest())
                return false;                    // nope, can't add elements
            return buckets[idx].Put(key, value); // this won't change the number of elements because the bucket is full
        }
    } while (!count.compare_exchange_strong(cur_size, cur_size + 1));
    // we have "allocated" a place for a new element, but maybe we won't use it
    size_t bucket_size = buckets[idx].size();
    bool ret = buckets[idx].Put(key, value);
    count += buckets[idx].size() - bucket_size - 1;
    return ret;
}

// See MapBasedStripedLockImpl.h
bool MapBasedStripedLockImpl::PutIfAbsent(const std::string &key, const std::string &value) {
    auto idx = std::hash<std::string>()(key) % num_buckets;
    std::lock_guard<std::mutex> lock{locks[idx]};
    size_t cur_size = count.load();
    do {
        if (cur_size >= max_size) {
            if (!buckets[idx].evict_oldest())
                return false; // nope, can't add elements
            return buckets[idx].PutIfAbsent(
                key, value); // this won't change the number of elements because the bucket is full
        }
    } while (!count.compare_exchange_strong(cur_size, cur_size + 1));
    return buckets[idx].PutIfAbsent(key, value);
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
