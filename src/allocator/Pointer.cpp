#include <afina/allocator/Pointer.h>

namespace Afina {
namespace Allocator {

Pointer::Pointer(void *ptr_) : ptr(ptr_) {}
Pointer::Pointer(const Pointer &p_) { ptr = p_.ptr; }

Pointer::Pointer(Pointer &&p_) {
    ptr = p_.ptr;
    p_.ptr = nullptr;
}

Pointer &Pointer::operator=(const Pointer &p_) {
    if (this != &p_)
        ptr = p_.ptr; // actually, this is safe even without the check
    return *this;
}
Pointer &Pointer::operator=(Pointer &&p_) {
    if (this != &p_) { // wait what?
        ptr = p_.ptr;
        p_.ptr = nullptr;
    }
    return *this;
}

void *Pointer::get() const { return ptr; }

} // namespace Allocator
} // namespace Afina
