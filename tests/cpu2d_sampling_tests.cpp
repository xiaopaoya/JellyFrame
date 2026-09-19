#include "../src/render_core/benchmarks/cpu2d_sampling.h"

#include <stdexcept>
#include <string>

struct FakeClock {
    using duration = std::chrono::microseconds;
    using time_point = std::chrono::time_point<FakeClock>;
    static inline duration elapsed{0};
    static time_point now() { return time_point(elapsed); }
};

int main() {
    int pending = 0;
    int completed = 0;
    std::string order;
    const auto result = jellyframe::benchmark::measure_interleaved<FakeClock>(
        3, 1, 4,
        [&] {
            if (pending != 0) throw std::runtime_error("reset before queue completion");
            order += 'A';
            FakeClock::elapsed += FakeClock::duration(1000);
        },
        [&](int) { ++pending; },
        [&] {
            FakeClock::elapsed += FakeClock::duration(pending * 5);
            completed += pending;
            pending = 0;
        },
        [&] { order += 'B'; FakeClock::elapsed += FakeClock::duration(2000); },
        [](int) { FakeClock::elapsed += FakeClock::duration(7); },
        [] {});
    if (order != "ABBAABBA" || completed != 16 || pending != 0 ||
        result.first != std::vector<double>(3, 5.0) ||
        result.second != std::vector<double>(3, 7.0)) {
        throw std::runtime_error("completion timing, reset exclusion or interleaving failed");
    }
    bool propagated = false;
    try {
        jellyframe::benchmark::measure_interleaved<FakeClock>(
            1, 0, 1, [] {}, [](int) {},
            [] { throw std::runtime_error("completion failed"); },
            [] {}, [](int) {}, [] {});
    } catch (const std::runtime_error&) { propagated = true; }
    if (!propagated) throw std::runtime_error("completion failure was ignored");
}
