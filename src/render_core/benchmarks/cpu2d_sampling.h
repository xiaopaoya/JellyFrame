#pragma once

#include <chrono>
#include <stdexcept>
#include <utility>
#include <vector>

namespace jellyframe::benchmark {

// Rotate the starting backend and reverse direction on alternate rounds. With
// three backends this visits all six permutations. One completed draw per sample.
template <typename Clock = std::chrono::steady_clock, typename Reset, typename Paint, typename Finish>
std::vector<std::vector<double>> measure_rotating(int samples, int warmups, int backends,
                                                Reset reset, Paint paint, Finish finish) {
    if (samples < 1 || samples > 10000 || warmups < 0 || warmups > 10000 || backends < 1 || backends > 4) {
        throw std::invalid_argument("invalid rotating sample limits");
    }
    std::vector<std::vector<double>> result(static_cast<std::size_t>(backends));
    for (auto& values : result) values.reserve(samples);
    for (int round = 0; round < warmups + samples; ++round) {
        const int start = (round / 2) % backends;
        for (int step = 0; step < backends; ++step) {
            const int backend = (start + (round % 2 ? backends - step : step)) % backends;
            reset(backend);
            const auto begin = Clock::now();
            paint(backend);
            finish(backend);
            const auto end = Clock::now();
            if (round >= warmups) {
                result[static_cast<std::size_t>(backend)].push_back(
                    std::chrono::duration<double, std::micro>(end - begin).count());
            }
        }
    }
    return result;
}

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
