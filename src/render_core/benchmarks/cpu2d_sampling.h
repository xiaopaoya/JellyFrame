#pragma once

#include <chrono>
#include <utility>
#include <vector>

namespace jellyframe::benchmark {

// Paint and completion are timed together; reset must complete before returning.
template <typename Clock = std::chrono::steady_clock,
          typename ResetA, typename PaintA, typename FinishA,
          typename ResetB, typename PaintB, typename FinishB>
std::pair<std::vector<double>, std::vector<double>> measure_interleaved(
    int samples, int warmups, int operations,
    ResetA reset_a, PaintA paint_a, FinishA finish_a,
    ResetB reset_b, PaintB paint_b, FinishB finish_b) {
    const auto sample = [operations](auto& reset, auto& paint, auto& finish) {
        reset();
        const auto begin = Clock::now();
        for (int operation = 0; operation < operations; ++operation) paint(operation);
        finish();
        const auto end = Clock::now();
        return std::chrono::duration<double, std::micro>(end - begin).count() / operations;
    };
    std::pair<std::vector<double>, std::vector<double>> result;
    result.first.reserve(samples);
    result.second.reserve(samples);
    for (int index = 0; index < warmups + samples; ++index) {
        double a = 0.0;
        double b = 0.0;
        if (index % 2 == 0) {
            a = sample(reset_a, paint_a, finish_a);
            b = sample(reset_b, paint_b, finish_b);
        } else {
            b = sample(reset_b, paint_b, finish_b);
            a = sample(reset_a, paint_a, finish_a);
        }
        if (index >= warmups) {
            result.first.push_back(a);
            result.second.push_back(b);
        }
    }
    return result;
}

} // namespace jellyframe::benchmark
