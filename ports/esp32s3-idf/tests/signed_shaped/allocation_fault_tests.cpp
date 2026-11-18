#include "allocation_fault.h"
#include <cassert>
#include <cstdint>
#include <limits>
#include <new>

int main() {
    const auto initial=allocation_fault::usage();
    allocation_fault::reset_peak();
    void* ordinary=::operator new(37);
    void* aligned=::operator new[](131,std::align_val_t(256));
    assert(reinterpret_cast<std::uintptr_t>(ordinary)%alignof(std::max_align_t)==0);
    assert(reinterpret_cast<std::uintptr_t>(aligned)%256==0);
    auto current=allocation_fault::usage();
    assert(current.live_bytes==initial.live_bytes+168);
    assert(current.peak_bytes==current.live_bytes);
    assert(current.total_bytes==initial.total_bytes+168 && current.allocations==initial.allocations+2);
    allocation_fault::begin(0);
    assert(::operator new(8,std::nothrow)==nullptr);
    assert(allocation_fault::end()==1 && allocation_fault::triggered());
    assert(allocation_fault::usage().live_bytes==current.live_bytes);
    assert(::operator new(std::numeric_limits<std::size_t>::max(),std::nothrow)==nullptr);
    void* zero=::operator new(0);
    assert(zero && allocation_fault::usage().live_bytes==current.live_bytes);
    ::operator delete(zero);
    ::operator delete(ordinary,std::size_t(37));
    ::operator delete[](aligned,std::align_val_t(256));
    assert(allocation_fault::usage().live_bytes==initial.live_bytes);
    allocation_fault::reset_peak();
    assert(allocation_fault::usage().peak_bytes==initial.live_bytes);
    ::operator delete(nullptr);
}
