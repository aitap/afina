#ifndef AFINA_STORAGE_MAP_BASED_GLOBAL_LOCK_IMPL_H
#define AFINA_STORAGE_MAP_BASED_GLOBAL_LOCK_IMPL_H

#include <string>
#include <unordered_map>

#include <afina/Storage.h>

namespace Afina {
namespace Backend {

/**
 * # Map based implementation with global lock
 *
 *
 */
class MapBasedGlobalLockImpl : public Afina::Storage {
public:
    MapBasedGlobalLockImpl() {}
    ~MapBasedGlobalLockImpl() {}

    // Implements Afina::Storage interface
    const std::string &Put(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    const std::string &PutIfAbsent(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool Get(const std::string &key, std::string &value) override;

private:
    std::unordered_map<std::string, std::string> backend;
};

} // namespace Storage
} // namespace Afina

#endif // AFINA_STORAGE_MAP_BASED_GLOBAL_LOCK_IMPL_H
