#include "allocation_fault.h"
#include <cstdlib>
#include <new>
#if defined(_WIN32)
#include <malloc.h>
#endif

namespace {
thread_local bool active=false,failed=false;
thread_local std::size_t at=0,limit=static_cast<std::size_t>(-1);
bool refuse() noexcept {
    if (!active) return false;
    if (at++!=limit) return false;
    failed=true;
    return true;
}
void* allocate(std::size_t size) {
    if (refuse()) throw std::bad_alloc();
    if (void* p=std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* aligned_allocate(std::size_t size,std::size_t alignment) {
    if (refuse()) throw std::bad_alloc();
#if defined(_WIN32)
    void* p=_aligned_malloc(size ? size : 1,alignment);
#else
    void* p=nullptr;
    if (posix_memalign(&p,alignment,size ? size : 1)!=0) p=nullptr;
#endif
    if (p) return p;
    throw std::bad_alloc();
}
void aligned_release(void* p) noexcept {
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
}
namespace allocation_fault {
void begin(std::size_t fail_at) noexcept { at=0;limit=fail_at;failed=false;active=true; }
std::size_t end() noexcept { active=false;return at; }
bool triggered() noexcept { return failed; }
}
void* operator new(std::size_t n) { return allocate(n); }
void* operator new[](std::size_t n) { return allocate(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t n,const std::nothrow_t&) noexcept { try { return allocate(n); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n,const std::nothrow_t&) noexcept { try { return allocate(n); } catch (...) { return nullptr; } }
void operator delete(void* p,const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p,const std::nothrow_t&) noexcept { std::free(p); }
void* operator new(std::size_t n,std::align_val_t a) { return aligned_allocate(n,static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n,std::align_val_t a) { return aligned_allocate(n,static_cast<std::size_t>(a)); }
void operator delete(void* p,std::align_val_t) noexcept { aligned_release(p); }
void operator delete[](void* p,std::align_val_t) noexcept { aligned_release(p); }
void operator delete(void* p,std::size_t,std::align_val_t) noexcept { aligned_release(p); }
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept { aligned_release(p); }
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept { try { return aligned_allocate(n,static_cast<std::size_t>(a)); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept { try { return aligned_allocate(n,static_cast<std::size_t>(a)); } catch (...) { return nullptr; } }
void operator delete(void* p,std::align_val_t,const std::nothrow_t&) noexcept { aligned_release(p); }
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&) noexcept { aligned_release(p); }
