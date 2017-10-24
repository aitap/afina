/*
 BIG FAT DISCLAIMER
 This allocator doesn't give a damn about alignment requirements.
 Expect undefined behaviour on anything that doesn't allow unaligned access.
*/

#include <afina/allocator/Error.h>
#include <afina/allocator/Pointer.h>
#include <afina/allocator/Simple.h>

#include <cassert> // not supplying enough memory to the constructor will kill the program
#include <cstring> // std::memmove, memcpy
#include <memory>
#include <sstream> // building strings in style

namespace Afina {
namespace Allocator {

// chosen by a fair dice roll
// NOTE: the highest bit of block_magic should be 0 to account for the truncation later
static const std::uint32_t block_magic = 0x7700e698, footer_magic = 0xe26ab656;

struct block; // of course they depend on each other

// lives at fixed offset, contains global info about our allocator
struct footer {
    std::uint32_t magic;
    block *last;
    size_t max_allocated;
    footer(void *base) : magic(footer_magic), last((block *)base), max_allocated(0){};
    void check() const {
        if (magic != footer_magic)
            throw AllocError{AllocErrorType::CorruptionDetected, "Corruption found in footer"};
    }
    void cleanup_pointer_slots();
};

// header for a memory block
struct block {
    std::uint32_t magic : 31; // 31-bit uint32! shock, horrors!
    bool free : 1;
    size_t size;
    block *next;
    block(size_t size_, block *next_ = nullptr) : magic(block_magic), free(true), size(size_), next(next_){};
    void check() const {
        if (magic != block_magic)
            throw AllocError{AllocErrorType::CorruptionDetected, "Corruption found in block"};
    }
    void squash_next_free(footer *ftr) {
        check();
        while (next && next->free) {
            block *to_squash = next;
            to_squash->check(); // only paranoid survive
            next = to_squash->next;
            size += sizeof(block) + to_squash->size;
        }
        if (!next)
            ftr->last = this; // don't break the pointer to the last block
    }

    void maybe_split_off(size_t N, footer *ftr) {
        /*
         |BLOCK|================= size ===========================|

          _ free_block      _ free_block + sizeof(block) + N
         /                 /
         |BLOCK|==== N ====|BLOCK|=== size - N - sizeof(block) ===|
                   |
                   \_ free_block+sizeof(block)
        */
        if (size - N < 2 * sizeof(block))
            return; // you've got to be kidding me
        next = new ((char *)this + N + sizeof(block)) block{size - N - sizeof(block), next};
        if (this == ftr->last) // well, it isn't last anymore
            ftr->last = next;
        size = N;
        next->squash_next_free(ftr);
    }

    block *find_free_block(size_t N, footer *ftr) {
        for (block *blk = this; blk; blk = blk->next) {
            blk->check(); // better safe than sorry
            if (!blk->free || blk->size < N)
                continue;
            // found it!
            blk->free = false;
            blk->maybe_split_off(N, ftr);
            return blk;
        }
        return nullptr;
    }
};

void footer::cleanup_pointer_slots() {
    check();
    // decrement max_allocated if there are nullptr at the end of pointer array
    while (!*((void **)this - max_allocated) && max_allocated) {
        max_allocated--;
        // adjust the size of the last block accordingly
        // even if it's occupied, so what? I only extend it anyway
        last->size += sizeof(void *);
    }
}

Simple::Simple(void *base, size_t size) : _base(base), _base_len(size) {
    // they told me not to throw from constructor, so I didn't
    assert(size > sizeof(block) + sizeof(footer));
    /* Leave enough space for:
     * - the block descriptor
     * - the footer at the end of the memory area */
    new (base) block{size - sizeof(footer) - sizeof(block)}; // the base free block
    new ((char *)base + size - sizeof(footer)) footer{base}; // the footer
}

/**
 * @param N size_t
 */
Pointer Simple::alloc(size_t N) {
    // makes you want to have stored that pointer instead, doesn't it?
    footer *ftr = (footer *)((char *)_base + _base_len - sizeof(footer));
    ftr->check(); // just in case

    // find space in the pointer list
    void **pptr = (void **)ftr;
    for (size_t i = 1; i <= ftr->max_allocated; i++) { // traverse the already allocated slots
        if (!pptr[-i]) {                               // we're lucky: there's a free pointer slot for us
            pptr = pptr - i;
            break;
        }
    }
    if (pptr == (void **)ftr) { // all pointer slots are occupied => reduce the last block
        ftr->last->check();     // constant vigilance!
        if (!ftr->last->free || ftr->last->size < sizeof(void *))
            throw AllocError{AllocErrorType::NoMemory, "Couldn't extend the pointer array"};
        ftr->last->size -= sizeof(void *); // reluctantly allocate one more slot
        ftr->max_allocated++;
        pptr -= ftr->max_allocated;
        *pptr = nullptr;
    }

    block *free_block = ((block *)_base)->find_free_block(N, ftr);
    if (!free_block) {
        defrag();
        free_block = ((block *)_base)->find_free_block(N, ftr);
    }
    if (!free_block) {
        ftr->cleanup_pointer_slots();
        throw AllocError{AllocErrorType::NoMemory, "Couldn't find a suitable free memory block"};
    }

    *pptr = (void *)((char *)free_block + sizeof(block)); // pointer past the end
    return Pointer(pptr);
}

/**
 * @param p Pointer
 * @param N size_t
 */
void Simple::realloc(Pointer &p, size_t N) {
    if (!p.ptr) { // easy: this is a fancy alloc
        p = alloc(N);
        return;
    }
    block *blk = (block *)((char *)*p.ptr - sizeof(block));
    footer *ftr = (footer *)((char *)_base + _base_len - sizeof(footer));
    blk->check();
    ftr->check();
    if (blk->size >= N) { // maybe shrinking?
        blk->maybe_split_off(N, ftr);
    } else {                                                                    // oh well, we must enlarge
        if (blk->next && blk->next->free && blk->next->size + blk->size >= N) { // lucky?
            blk->squash_next_free(ftr);
            blk->maybe_split_off(N, ftr);
        } else { // oh well, let's get moving
            Pointer new_p = alloc(N);
            std::memcpy(new_p.get(), p.get(), blk->size);
            free(p);
            p = new_p;
        }
    }
}

/**
 * @param p Pointer
 */
void Simple::free(Pointer &p) {
    if (!p.ptr)
        throw AllocError{AllocErrorType::InvalidFree, "Trying to free a null pointer"};
    block *to_free = (block *)((char *)p.get() - sizeof(block));
    try {
        to_free->check();
    } catch (AllocError &e) {
        throw AllocError{AllocErrorType::InvalidFree, "Corruption detected while checking the pointer to free"};
    }

    footer *ftr = (footer *)((char *)_base + _base_len - sizeof(footer));
    ftr->check(); // you can't be paranoid enough

    // free the pointer slot
    *p.ptr = nullptr;
    ftr->cleanup_pointer_slots();
    // clean up the pointer object
    p.ptr = nullptr;

    // now the interesting part: linked list
    to_free->free = true;
    // there might be a free block before this (check could be trivial with a doubly-linked list) to squash together
    // but I'm lazy (for now), let's do a simple & stupid O(n) linked list travesal
    for (block *blk = (block *)_base; blk; blk = blk->next) {
        blk->check(); // too defensive?
        if (!blk->free)
            continue;
        blk->squash_next_free(ftr);
    }
}

// NOTE: sane memmove() implementations don't allocate anything. I checked.
// They only behave *AS THOUGH* copying was done through a temporary non-overlapping array.

void Simple::defrag() {
    footer *ftr = (footer *)((char *)_base + _base_len - sizeof(footer));
    // of course I would
    ftr->check();
    /*
       ______________________________________________________
      /                 ____________________________________ \
      |                /                 __________________ \|
      V                V                V                  \||
     |BLKA=123=|BLKF==|BLKA=45=|BLKF===|BLKA=6789=|BLKF===|PPP|FOOTER|
      |          ______|  ______________|
      |         |        |                    <-- memmove
      V         V        V
     |BLKA=123=|BLKA=45=|BLKA=6789=|BLKF==================|PPP|FOOTER|
      ^         ^        ^                                 |||
      |         |        \_________________________________/||
      |         \___________________________________________/|
      \______________________________________________________/
    */
    for (block *blk = (block *)_base; blk->next /* no point touching the last block */; blk = blk->next) {
        if (!blk->free)
            continue;
        // this block is free => squash any following free blocks (we might have created earlier) together
        blk->squash_next_free(ftr);
        if (!blk->next) { // reached the end by squashing
            break;        // our job here is done
        }
        // next one is occupied => prepare for the move
        // update the pointer array
        {
            bool found = false;
            for (size_t i = 1; i <= ftr->max_allocated; i++)
                if (*((void **)ftr - i) == (char *)blk->next + sizeof(block)) {
                    found = true;
                    *((void **)ftr - i) = (char *)blk + sizeof(block);
                    break;
                }

            if (!found)
                throw AllocError{AllocErrorType::CorruptionDetected, "Coundn't find pointer to the block being moved"};
        }
        // make sure not to break the pointer to last block
        if (blk->next == ftr->last)
            ftr->last = blk;
        // do the move
        std::memmove(blk, blk->next, sizeof(block) + blk->next->size);
        /* now blk is the occupied block but it has wrong size
         ...|BLKF==|BLKA=DATA=|...
              _move_|  |       ^
             |         \__next_/
             V
         ...|BLKA=DATA=???????|...
                |              ^
                \________next__/
         * what should we do? create a free block and let the loop do its magic, of course!
         ...|BLKA=DATA=|BLKF==|...
                \___->__/  \_>_/
         */
        blk->next = new ((char *)blk + blk->size + sizeof(block)) block{
            // if there's a next block, size it relative to that
            blk->next
                ? ((char *)blk->next - (char *)blk - 2 * sizeof(block) - blk->size)
                // otherwise, get the size from the footer address
                : ((char *)ftr - (char *)blk - 2 * sizeof(block) - blk->size - sizeof(void *) * ftr->max_allocated),
            blk->next};
    }
}

std::string Simple::dump() const {
    using std::endl;
    std::stringstream ret;
    ret << "Start=" << _base << ", size=" << _base_len << endl;

    footer *ftr = (footer *)((char *)_base + _base_len - sizeof(footer));
    ret << "Footer=" << (void *)ftr << ", last @" << (void *)ftr->last << ", max_allocated=" << ftr->max_allocated;
    try {
        ftr->check();
        ret << " (magic OK)";
    } catch (AllocError &e) {
        ret << " !!! magic FAIL !!!";
    }
    ret << endl;

    // increment for each allocated from pointer list,
    // decrement for each allocated from linked list
    std::ptrdiff_t zero_check = 0;
    {
        size_t num_allocated = ftr->max_allocated;
        for (void **pptr = (void **)(ftr)-1; /* Start at the first pointer */
             num_allocated;                  /* Check if we have any more pointers */
             pptr--, num_allocated--) {
            void *ptr = *pptr;
            if (ptr) {
                zero_check++;
                ret << "#" << num_allocated << " @" << ptr << endl;
            }
        }
    }

    ret << "Blocks:" << endl;
    for (block *blk = (block *)_base; blk; blk = blk->next) {
        ret << (void *)blk << "\t" << (blk->free ? "(free)" : "(allocated)") << "\t" << blk->size;
        try {
            blk->check();
            ret << " (valid magic)";
        } catch (AllocError &e) {
            ret << " !!! invalid magic !!!";
        }
        ret << ((!blk->next)
                    ? " (last)"
                    : ((char *)blk->next == (char *)blk + blk->size + sizeof(block)) ? " (size OK)"
                                                                                     : " !!! size NOT OKAY !!!");
        ret << endl;
        if (!blk->free)
            zero_check--;
    }

    if (zero_check)
        ret << "Inconsistency: 0 != (allocated_array - allocated_list) = " << zero_check << endl;
    else
        ret << "Linked list and pointer array don't contradict too much." << endl;

    return ret.str();
}

} // namespace Allocator
} // namespace Afina
