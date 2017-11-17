#ifndef AFINA_STORAGE_MAP_BASED_STRIPED_LOCK_IMPL_H
#define AFINA_STORAGE_MAP_BASED_STRIPED_LOCK_IMPL_H

#include <memory>
#include <string>
#include <vector>

#include "MapBasedGlobalLockImpl.h"
#include <afina/Storage.h>

namespace Afina {
namespace Backend {

/**
 * # Map based implementation with global lock
 *
 *
 */
class MapBasedStripedLockImpl : public Afina::Storage {
public:
    MapBasedStripedLockImpl(size_t num_buckets, size_t max_size = 1024);
    ~MapBasedStripedLockImpl();

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
    MapBasedGlobalLockImpl *buckets;
    size_t num_buckets;
};

} // namespace Backend
} // namespace Afina

#endif // AFINA_STORAGE_MAP_BASED_STRIPED_LOCK_IMPL_H
