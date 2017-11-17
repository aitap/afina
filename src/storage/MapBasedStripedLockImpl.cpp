#include "MapBasedStripedLockImpl.h"
#include <functional>

namespace Afina {
namespace Backend {

MapBasedStripedLockImpl::MapBasedStripedLockImpl(size_t num_buckets_, size_t max_size)
    : num_buckets(num_buckets_), count(0) {
    // the wonders of immovable objects with important constructor arguments
    buckets = (MapBasedGlobalLockImpl *)operator new[](sizeof(MapBasedGlobalLockImpl) * num_buckets);
    for (size_t i = 0; i < num_buckets; i++) {
        (void)new (&buckets[i]) MapBasedGlobalLockImpl(max_size);
    }
}

// See MapBasedStripedLockImpl.h
bool MapBasedStripedLockImpl::Put(const std::string &key, const std::string &value) {
    return buckets[std::hash<std::string>()(key) % num_buckets].Put(key, value);
}

// See MapBasedStripedLockImpl.h
bool MapBasedStripedLockImpl::PutIfAbsent(const std::string &key, const std::string &value) {
    return buckets[std::hash<std::string>()(key) % num_buckets].PutIfAbsent(key, value);
}

// See MapBasedStripedLockImpl.h
bool MapBasedStripedLockImpl::Get(const std::string &key, std::string &value) const {
    return buckets[std::hash<std::string>()(key) % num_buckets].Get(key, value);
}

// See MapBasedStripedLockImpl.h
bool MapBasedStripedLockImpl::Set(const std::string &key, const std::string &value) {
    return buckets[std::hash<std::string>()(key) % num_buckets].Set(key, value);
}

// See MapBasedStripedLockImpl.h
bool MapBasedStripedLockImpl::Delete(const std::string &key) {
    return buckets[std::hash<std::string>()(key) % num_buckets].Delete(key);
}

MapBasedStripedLockImpl::~MapBasedStripedLockImpl() {
    for (int i = num_buckets; i > 0;) { // checked with Valgrind
        buckets[--i].~MapBasedGlobalLockImpl();
    }
    operator delete[]((void *)buckets);
}

} // namespace Backend
} // namespace Afina
