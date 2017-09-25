#include <afina/allocator/Error.h>
#include <afina/allocator/Pointer.h>
#include <afina/allocator/Simple.h>

#include <memory>

namespace Afina {
namespace Allocator {

// chosen by a fair dice roll
const std::uint32_t block_magic = 0xd700e698, footer_magic = 0xe26ab656;

// header for a memory block
struct block {
    std::uint32_t magic;
    std::size_t size;
    block *next;
    block *next_free;
    block() : magic(block_magic), next(nullptr), next_free(nullptr){};
};

// lives at fixed offset, contains global info about our allocator
struct footer {
    std::uint32_t magic;
    block *first_free;
    std::size_t num_allocated;
    footer() : magic(footer_magic){};
};

Simple::Simple(void *base, size_t size) : _base(base), _base_len(size) {
    if (sizeof(footer) + sizeof(block) <= size)
        throw AllocError(AllocErrorType::NoMemory, "Can't find place for the global header");
    // TODO
}

/**
 * TODO: semantics
 * @param N size_t
 */
Pointer Simple::alloc(size_t N) { return Pointer(); }

/**
 * TODO: semantics
 * @param p Pointer
 * @param N size_t
 */
void Simple::realloc(Pointer &p, size_t N) {}

/**
 * TODO: semantics
 * @param p Pointer
 */
void Simple::free(Pointer &p) {}

/**
 * TODO: semantics
 */
void Simple::defrag() {}

/**
 * TODO: semantics
 */
std::string Simple::dump() const {
    // I guess I am allowed to do std::string-related heap operations there
    return "";
}

} // namespace Allocator
} // namespace Afina
