#include <afina/allocator/Pointer.h>

namespace Afina {
namespace Allocator {

Pointer::Pointer() : ptr(nullptr) {}
Pointer::Pointer(const Pointer &p_) { ptr = p_->ptr; }
Pointer::Pointer(Pointer &&p_) {
    ptr = p_->ptr;
    p_->ptr = nullptr;
}

// TODO: check for self-assignment or something; see "= default"
Pointer &Pointer::operator=(const Pointer &) { return *this; }
Pointer &Pointer::operator=(Pointer &&) { return *this; }

void *Pointer::get() const { return ptr; }

} // namespace Allocator
} // namespace Afina
