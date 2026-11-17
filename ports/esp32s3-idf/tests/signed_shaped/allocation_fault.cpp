#include "allocation_fault.h"
#include <cstdlib>
#include <atomic>
#include <cstdint>
#include <limits>
#include <new>

namespace {
thread_local bool active=false,failed=false;
thread_local std::size_t at=0,limit=static_cast<std::size_t>(-1);
// Constant initialization and an allocation-free prefix allow tracking even
// static startup allocations, without a recursive map or lost pre-phase frees.
std::atomic<std::size_t> live{0},peak{0},total{0},count{0};
struct Header { void* raw; std::size_t requested; };
bool refuse() noexcept {
    if (!active) return false;
    if (at++!=limit) return false;
    failed=true;
    return true;
}
void* aligned_allocate(std::size_t size,std::size_t alignment) {
    if (refuse()) throw std::bad_alloc();
    alignment=alignment<alignof(Header)?alignof(Header):alignment;
    const auto maximum=std::numeric_limits<std::size_t>::max();
    const auto payload=size?size:1;
    if ((alignment&(alignment-1)) || alignment-1>maximum-sizeof(Header) ||
        payload>maximum-sizeof(Header)-(alignment-1)) throw std::bad_alloc();
    void* raw=std::malloc(payload+sizeof(Header)+alignment-1);
    if (!raw) throw std::bad_alloc();
    const auto address=(reinterpret_cast<std::uintptr_t>(raw)+sizeof(Header)+alignment-1)&~(alignment-1);
    auto* header=reinterpret_cast<Header*>(address)-1;
    ::new (header) Header{raw,size};
    const auto now=live.fetch_add(size,std::memory_order_relaxed)+size;
    auto previous=peak.load(std::memory_order_relaxed);
    while (previous<now && !peak.compare_exchange_weak(previous,now,std::memory_order_relaxed)) {}
    total.fetch_add(size,std::memory_order_relaxed);
    count.fetch_add(1,std::memory_order_relaxed);
    return reinterpret_cast<void*>(address);
}
void* allocate(std::size_t size) { return aligned_allocate(size,alignof(std::max_align_t)); }
void aligned_release(void* p) noexcept {
    if (!p) return;
    const auto* header=static_cast<Header*>(p)-1;
    live.fetch_sub(header->requested,std::memory_order_relaxed);
    std::free(header->raw);
}
}
namespace allocation_fault {
void begin(std::size_t fail_at) noexcept { at=0;limit=fail_at;failed=false;active=true; }
std::size_t end() noexcept { active=false;return at; }
bool triggered() noexcept { return failed; }
Usage usage() noexcept { return {live.load(),peak.load(),total.load(),count.load()}; }
void reset_peak() noexcept { peak.store(live.load()); }
}
void* operator new(std::size_t n) { return allocate(n); }
void* operator new[](std::size_t n) { return allocate(n); }
void operator delete(void* p) noexcept { aligned_release(p); }
void operator delete[](void* p) noexcept { aligned_release(p); }
void operator delete(void* p,std::size_t) noexcept { aligned_release(p); }
void operator delete[](void* p,std::size_t) noexcept { aligned_release(p); }
void* operator new(std::size_t n,const std::nothrow_t&) noexcept { try { return allocate(n); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n,const std::nothrow_t&) noexcept { try { return allocate(n); } catch (...) { return nullptr; } }
void operator delete(void* p,const std::nothrow_t&) noexcept { aligned_release(p); }
void operator delete[](void* p,const std::nothrow_t&) noexcept { aligned_release(p); }
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
