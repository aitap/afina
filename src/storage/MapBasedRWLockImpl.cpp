#include "MapBasedRWLockImpl.h"
#include <cerrno>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace Afina {
namespace Backend {

template <int (*do_lock)(pthread_rwlock_t *rwlock)> struct rwlock_guard {
    pthread_rwlock_t &lock;
    rwlock_guard(pthread_rwlock_t &rwl) : lock(rwl) {
        if (do_lock(&lock))
            throw std::runtime_error(std::string("failed to lock a pthread_rwlock, errno=") + std::to_string(errno));
    };
    ~rwlock_guard() {
        if (pthread_rwlock_unlock(&lock))
            abort(); // any possible reason is a programming error which should have been fixed
    }
};

typedef rwlock_guard<pthread_rwlock_rdlock> rdlock_guard;
typedef rwlock_guard<pthread_rwlock_wrlock> wrlock_guard;

MapBasedRWLockImpl::MapBasedRWLockImpl(size_t max_size) : MapBasedNoLockImpl(max_size) {
    if (pthread_rwlock_init(&lock, nullptr))
        abort(); // sorry
}

MapBasedRWLockImpl::~MapBasedRWLockImpl() {
    if (pthread_rwlock_destroy(&lock))
        abort(); // sorry
}

// See MapBasedRWLockImpl.h
bool MapBasedRWLockImpl::Put(const std::string &key, const std::string &value) {
    wrlock_guard wg(lock);
    return MapBasedNoLockImpl::Put(key, value);
}

// See MapBasedRWLockImpl.h
bool MapBasedRWLockImpl::PutIfAbsent(const std::string &key, const std::string &value) {
    wrlock_guard wg(lock);
    return MapBasedNoLockImpl::PutIfAbsent(key, value);
}

// See MapBasedRWLockImpl.h
bool MapBasedRWLockImpl::Get(const std::string &key, std::string &value) const {
    rdlock_guard rg(lock);
    return MapBasedNoLockImpl::Get(key, value);
}

// See MapBasedRWLockImpl.h
bool MapBasedRWLockImpl::Set(const std::string &key, const std::string &value) {
    wrlock_guard wg(lock);
    return MapBasedNoLockImpl::Set(key, value);
}

// See MapBasedRWLockImpl.h
bool MapBasedRWLockImpl::Delete(const std::string &key) {
    wrlock_guard wg(lock);
    return MapBasedNoLockImpl::Delete(key);
}

} // namespace Backend
} // namespace Afina
