#pragma once
#include <atomic>
#include <cstdint>
#include <limits>

namespace jellyframe::detail {
// Private finite ID source. Zero is a permanently exhausted state; the final
// uint64 ID is usable once. Production has one process-lifetime source and no
// reset operation. Independent sources are only useful for boundary tests.
class RuntimeFontIdSource final {
public:
    explicit RuntimeFontIdSource(std::uint64_t first = 1) noexcept : next_(first) {}
    bool take(std::uint64_t& output) noexcept {
        auto next = next_.load(std::memory_order_relaxed);
        while (next) {
            const auto following = next == std::numeric_limits<std::uint64_t>::max() ? 0 : next + 1;
            if (next_.compare_exchange_weak(next, following, std::memory_order_relaxed)) {
                output = next;
                return true;
            }
        }
        return false;
    }
private:
    std::atomic<std::uint64_t> next_;
};
} // namespace jellyframe::detail
