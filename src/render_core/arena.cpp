#include "render_core/arena.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace jellyframe {
namespace {

bool align_up(std::size_t value, std::size_t alignment, std::size_t& aligned) {
    const std::size_t mask = alignment - 1U;
    if (value > std::numeric_limits<std::size_t>::max() - mask) {
        return false;
    }
    aligned = (value + mask) & ~mask;
    return true;
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
    if (size > std::numeric_limits<std::size_t>::max() - alignment) {
        return nullptr;
    }

    if (blocks_.empty()) {
        add_block(size + alignment);
    }

    for (std::size_t index = next_block_index_; index < blocks_.size(); ++index) {
        Block& block = blocks_[index];
        std::size_t aligned = 0;
        if (!align_up(block.used, alignment, aligned)) {
            continue;
        }
        if (aligned <= block.capacity && size <= block.capacity - aligned) {
            void* result = block.bytes.get() + aligned;
            block.used = aligned + size;
            next_block_index_ = index;
            return result;
        }
    }

    Block& block = add_block(size + alignment);
    std::size_t aligned = 0;
    if (!align_up(block.used, alignment, aligned)) {
        return nullptr;
    }
    void* result = block.bytes.get() + aligned;
    block.used = aligned + size;
    next_block_index_ = blocks_.size() - 1;
    return result;
}

void MonotonicArena::rewind() {
    destroy_live_objects();
    for (Block& block : blocks_) {
        block.used = 0;
    }
    next_block_index_ = 0;
}

void MonotonicArena::reset() {
    destroy_live_objects();
    blocks_.clear();
    next_block_index_ = 0;
}

void MonotonicArena::destroy_live_objects() {
    for (auto it = destructors_.rbegin(); it != destructors_.rend(); ++it) {
        if (it->destroy != nullptr) {
            it->destroy(it->object);
        }
    }
    destructors_.clear();
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
    const std::size_t capacity = std::max(block_size_, min_capacity);
    Block block;
    block.bytes = std::make_unique<std::uint8_t[]>(capacity);
    block.capacity = capacity;
    blocks_.push_back(std::move(block));
    return blocks_.back();
}

} // namespace jellyframe
