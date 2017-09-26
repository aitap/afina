#include <afina/allocator/Pointer.h>

namespace Afina {
namespace Allocator {

Pointer::Pointer(void **ptr_) : ptr(ptr_) {}

// no allocations and C++11, heck yeah
Pointer::Pointer(const Pointer &p_) = default;
Pointer::Pointer(Pointer &&p_) = default;
Pointer &Pointer::operator=(const Pointer &p_) = default;
Pointer &Pointer::operator=(Pointer &&p_) = default;

void *Pointer::get() const { if (ptr) return *ptr; else return nullptr; }

} // namespace Allocator
} // namespace Afina
