#include "MapBasedGlobalLockImpl.h"
#include <algorithm>

namespace Afina {
namespace Backend {

// hash complexities given are average, not worst (worst for hashes is O(n))
// list is simple enough not to have complex worst case scenarios

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Put(const std::string &key, const std::string &value) {
    std::lock_guard<std::recursive_mutex> lock(_lock);
    // was there an older item?
    bool ret = false;
    auto old_hash_it = hash.find(key); // O(1)
    // store the new item
    auto new_storage_it = storage.insert(storage.begin(), std::make_pair(key, value)); // O(1)

    if (old_hash_it != hash.end()) { // there was an item with same key
        ret = true;
        // destroy the old item in both containers
        storage.erase(old_hash_it->second); // O(1)
        hash.erase(old_hash_it);            // O(1)
    }
    hash[key] = new_storage_it; // remember the new position, O(1)

    // clean up
    if (hash.size() > _max_size) {
        auto last = storage.end();
        last--;                  // end is past-the-end, O(1)
        hash.erase(last->first); // O(key count) = O(1)
        storage.erase(last);     // O(1)
    }

    return ret; // return whether there was an element before
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::PutIfAbsent(const std::string &key, const std::string &value) {
    std::lock_guard<std::recursive_mutex> lock(_lock);
    auto it = hash.find(key); // O(1)
    if (it != hash.end())
        return false;
    // this acquires the mutex again => must be recursive
    /* return ! */ Put(key, value);
    return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Get(const std::string &key, std::string &value) const {
    std::lock_guard<std::recursive_mutex> lock(_lock);
    auto it = hash.find(key); // O(1)
    if (it == hash.end())
        return false;
    // hash::iterator points to pair<hash key, hash value>
    // and hash value is in turn an iterator to a pair<our key, our value>
    value = it->second->second; // what a mess
    return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Set(const std::string &key, const std::string &value) {
    std::lock_guard<std::recursive_mutex> lock(_lock);
    auto it = hash.find(key); // O(1)
    if (it != hash.end())
        return false;
    // "Set" refreshes the key, like PutIfAbsent but otherwise
    /* return */ Put(key, value);
    return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Delete(const std::string &key) {
    std::lock_guard<std::recursive_mutex> lock(_lock);
    auto it = hash.find(key); // O(1)
    if (it == hash.end())
        return false;
    storage.erase(it->second); // list iterator is second in returned pair
    hash.erase(it);
    return true; // return if the key was present in the first place
}

} // namespace Backend
} // namespace Afina
