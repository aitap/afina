#ifndef AFINA_STORAGE_MAP_BASED_RW_LOCK_IMPL_H
#define AFINA_STORAGE_MAP_BASED_RW_LOCK_IMPL_H

#include <pthread.h>
#include <string>

#include "MapBasedNoLockImpl.h"
#include <afina/Storage.h>

namespace Afina {
namespace Backend {

/**
 * # Map based implementation with global lock
 *
 *
 */
class MapBasedRWLockImpl : public MapBasedNoLockImpl {
public:
    MapBasedRWLockImpl(size_t max_size = 1024);
    ~MapBasedRWLockImpl();

    // Implements Afina::Storage interface
    bool Put(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool PutIfAbsent(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool Set(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool Delete(const std::string &key) override;

    // Implements Afina::Storage interface
    bool Get(const std::string &key, std::string &value) const override;

private:
    mutable pthread_rwlock_t lock;
};

} // namespace Backend
} // namespace Afina

#endif // AFINA_STORAGE_MAP_BASED_RW_LOCK_IMPL_H
