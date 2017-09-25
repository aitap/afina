#include <afina/allocator/Error.h>
#include <afina/allocator/Pointer.h>
#include <afina/allocator/Simple.h>

#include <cassert> // not supplying enough memory to the constructor will kill the program
#include <memory>

namespace Afina {
namespace Allocator {

// chosen by a fair dice roll
static const std::uint32_t block_magic = 0xd700e698, footer_magic = 0xe26ab656;

// header for a memory block
struct block {
    std::uint32_t magic;
    std::size_t size;
    block *next;
    block *next_free;
    block(std::size_t size_, block *next_ = nullptr, block *next_free_ = nullptr)
        : magic(block_magic), size(size_), next(next_), next_free(next_free_){};
};

// lives at fixed offset, contains global info about our allocator
struct footer {
    std::uint32_t magic;
    block *first_free;
    std::size_t num_allocated;
    footer(void *base) : magic(footer_magic), first_free((block *)base), num_allocated(0){};
};

Simple::Simple(void *base, size_t size) : _base(base), _base_len(size) {
    // they told me not to throw from constructor, so I didn't
    assert(size > sizeof(block) + sizeof(footer));
    /* Leave enough space for:
     * - the block descriptor
     * - the footer at the end of the memory area
     * - one void* to point at the allocated block */
    block *head = new (base) block{size - sizeof(footer) - sizeof(footer) - sizeof(void *)};
    footer *ftr = new ((char *)base - sizeof(footer)) footer{base};
    // Constructors did their magic, we seem to have a valid linked list
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
