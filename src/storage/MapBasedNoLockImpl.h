#ifndef AFINA_STORAGE_MAP_BASED_NO_LOCK_IMPL_H
#define AFINA_STORAGE_MAP_BASED_NO_LOCK_IMPL_H

#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include <afina/Storage.h>

namespace Afina {
namespace Backend {

/**
 * # Map based implementation with global lock
 *
 *
 */
class MapBasedNoLockImpl : public Afina::Storage {
public:
    MapBasedNoLockImpl(size_t max_size = 1024) : _max_size(max_size) {}
    ~MapBasedNoLockImpl() {}

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
    size_t _max_size;

    std::list<std::pair<std::string, std::string>> storage;
    std::unordered_map<std::string, std::list<std::pair<std::string, std::string>>::iterator> hash;
};

} // namespace Backend
} // namespace Afina

#endif // AFINA_STORAGE_MAP_BASED_NO_LOCK_IMPL_H
