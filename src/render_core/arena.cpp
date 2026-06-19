#include "render_core/arena.h"

#include <algorithm>
#include <cstdlib>

namespace jellyframe {
namespace {

std::size_t align_up(std::size_t value, std::size_t alignment) {
    const std::size_t mask = alignment - 1U;
    return (value + mask) & ~mask;
}

} // namespace

MonotonicArena::MonotonicArena(std::size_t block_size)
    : block_size_(std::max<std::size_t>(block_size, 256)) {}

MonotonicArena::~MonotonicArena() {
    reset();
}

void* MonotonicArena::allocate(std::size_t size, std::size_t alignment) {
    if (size == 0) {
        size = 1;
    }
    alignment = std::max<std::size_t>(alignment, 1);
    if ((alignment & (alignment - 1U)) != 0U || alignment > alignof(std::max_align_t)) {
        return nullptr;
    }

    if (blocks_.empty()) {
        add_block(size + alignment);
    }

    Block* block = &blocks_.back();
    std::size_t aligned = align_up(block->used, alignment);
    if (aligned + size > block->capacity) {
        block = &add_block(size + alignment);
        aligned = align_up(block->used, alignment);
    }

    void* result = block->bytes.get() + aligned;
    block->used = aligned + size;
    return result;
}

void MonotonicArena::reset() {
    for (auto it = destructors_.rbegin(); it != destructors_.rend(); ++it) {
        if (it->destroy != nullptr) {
            it->destroy(it->object);
        }
    }
    destructors_.clear();
    blocks_.clear();
}

std::size_t MonotonicArena::used_bytes() const {
    std::size_t total = 0;
    for (const Block& block : blocks_) {
        total += block.used;
    }
    return total;
}

std::size_t MonotonicArena::capacity_bytes() const {
    std::size_t total = 0;
    for (const Block& block : blocks_) {
        total += block.capacity;
    }
    return total;
}

std::size_t MonotonicArena::block_count() const {
    return blocks_.size();
}

MonotonicArena::Block& MonotonicArena::add_block(std::size_t min_capacity) {
    const std::size_t capacity = std::max(block_size_, align_up(min_capacity, alignof(std::max_align_t)));
    Block block;
    block.bytes = std::make_unique<std::uint8_t[]>(capacity);
    block.capacity = capacity;
    blocks_.push_back(std::move(block));
    return blocks_.back();
}

} // namespace jellyframe
