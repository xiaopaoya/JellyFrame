#include "core/arena.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Tracked {
    explicit Tracked(std::vector<int>& log, int value)
        : log(&log), value(value) {}

    ~Tracked() {
        log->push_back(value);
    }

    std::vector<int>* log = nullptr;
    int value = 0;
};

struct Pair {
    int left = 0;
    int right = 0;
};

void allocates_aligned_objects() {
    MonotonicArena arena(64);
    auto& first = arena.create<Pair>(Pair{1, 2});
    auto& second = arena.create<double>(3.5);

    check(first.left == 1 && first.right == 2, "arena constructs aggregate object");
    check(second > 3.4 && second < 3.6, "arena constructs primitive object");
    check(reinterpret_cast<std::uintptr_t>(&first) % alignof(Pair) == 0, "pair is aligned");
    check(reinterpret_cast<std::uintptr_t>(&second) % alignof(double) == 0, "double is aligned");
    check(arena.used_bytes() >= sizeof(Pair) + sizeof(double), "used bytes include allocations");
    check(arena.capacity_bytes() >= arena.used_bytes(), "capacity includes used bytes");
}

void grows_by_blocks() {
    MonotonicArena arena(32);
    arena.allocate(200, alignof(std::max_align_t));
    arena.allocate(200, alignof(std::max_align_t));

    check(arena.block_count() >= 2, "arena grows when current block is full");
}

void destroys_non_trivial_objects_in_reverse_order() {
    std::vector<int> log;
    {
        MonotonicArena arena(64);
        arena.create<Tracked>(log, 1);
        arena.create<Tracked>(log, 2);
        arena.create<Tracked>(log, 3);
    }

    check(log.size() == 3, "arena destroys tracked objects");
    check(log[0] == 3 && log[1] == 2 && log[2] == 1, "arena destroys in reverse construction order");
}

void reset_releases_blocks() {
    std::vector<int> log;
    MonotonicArena arena(64);
    arena.create<Tracked>(log, 7);
    arena.allocate(128, alignof(std::max_align_t));
    check(arena.block_count() > 0, "arena has blocks before reset");

    arena.reset();

    check(log.size() == 1 && log[0] == 7, "reset destroys live objects");
    check(arena.block_count() == 0, "reset releases blocks");
    check(arena.used_bytes() == 0, "reset clears used byte accounting");
    check(arena.capacity_bytes() == 0, "reset clears capacity accounting");
}

} // namespace

int main() {
    try {
        allocates_aligned_objects();
        grows_by_blocks();
        destroys_non_trivial_objects_in_reverse_order();
        reset_releases_blocks();
    } catch (const std::exception& error) {
        std::cerr << "arena test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "arena tests passed\n";
    return 0;
}
