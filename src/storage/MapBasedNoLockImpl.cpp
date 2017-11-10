#include "MapBasedNoLockImpl.h"
#include <algorithm>

namespace Afina {
namespace Backend {

// hash complexities given are average, not worst (worst for hashes is O(n))
// list is simple enough not to have complex worst case scenarios

// See MapBasedNoLockImpl.h
bool MapBasedNoLockImpl::Put(const std::string &key, const std::string &value) {
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

// See MapBasedNoLockImpl.h
bool MapBasedNoLockImpl::PutIfAbsent(const std::string &key, const std::string &value) {
    auto it = hash.find(key); // O(1)
    if (it != hash.end())
        return false;
    // this acquires the mutex again => must be recursive
    /* return ! */ MapBasedNoLockImpl::Put(key, value); // do NOT call any locking overrides -- that's not what we want
    return true;
}

// See MapBasedNoLockImpl.h
bool MapBasedNoLockImpl::Get(const std::string &key, std::string &value) const {
    auto it = hash.find(key); // O(1)
    if (it == hash.end())
        return false;
    // hash::iterator points to pair<hash key, hash value>
    // and hash value is in turn an iterator to a pair<our key, our value>
    value = it->second->second; // what a mess
    return true;
}

// See MapBasedNoLockImpl.h
bool MapBasedNoLockImpl::Set(const std::string &key, const std::string &value) {
    auto it = hash.find(key); // O(1)
    if (it != hash.end())
        return false;
    // "Set" refreshes the key, like PutIfAbsent but otherwise
    /* return */ MapBasedNoLockImpl::Put(key, value);
    return true;
}

// See MapBasedNoLockImpl.h
bool MapBasedNoLockImpl::Delete(const std::string &key) {
    auto it = hash.find(key); // O(1)
    if (it == hash.end())
        return false;
    storage.erase(it->second); // list iterator is second in returned pair
    hash.erase(it);
    return true; // return if the key was present in the first place
}

} // namespace Backend
} // namespace Afina
