#pragma once
#include <cstddef>

// Executable-local C++ allocation injection. It does not instrument C malloc,
// JerryScript's arena, IDF heaps or device fragmentation.
namespace allocation_fault {
void begin(std::size_t fail_at = static_cast<std::size_t>(-1)) noexcept;
std::size_t end() noexcept;
bool triggered() noexcept;
// Requested C++ bytes only: excludes ledger headers, allocator overhead and C/VM
// allocations. Process-wide counters; reset_peak requires a quiescent boundary.
struct Usage {
    std::size_t live_bytes, peak_bytes, total_bytes, allocations;
};
Usage usage() noexcept;
void reset_peak() noexcept;
}
