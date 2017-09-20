#include <afina/allocator/Simple.h>

#include <afina/allocator/Pointer.h>

namespace Afina {
namespace Allocator {

const std::uint32_t block_magic = 0xd700e698, footer_magic = 0xe26ab656;

// lives at fixed offset, contains global info about our allocator
struct footer {
    std::uint32_t magic;
    footer() : magic(footer_magic){};
};

// header for a memory block
struct block {
    std::uint32_t magic;
    block() : magic(block_magic){};
};

Simple::Simple(void *base, size_t size) : _base(base), _base_len(size) {}

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
std::string Simple::dump() const { return ""; }

} // namespace Allocator
} // namespace Afina
