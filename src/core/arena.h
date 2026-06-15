#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace jellyframe {

class MonotonicArena {
public:
    explicit MonotonicArena(std::size_t block_size = 4096);
    ~MonotonicArena();

    MonotonicArena(const MonotonicArena&) = delete;
    MonotonicArena& operator=(const MonotonicArena&) = delete;

    void* allocate(std::size_t size, std::size_t alignment);

    template <typename T, typename... Args>
    T& create(Args&&... args) {
        static_assert(alignof(T) <= alignof(std::max_align_t), "over-aligned arena objects are not supported");
        void* storage = allocate(sizeof(T), alignof(T));
        T* object = new (storage) T(std::forward<Args>(args)...);
        if (!std::is_trivially_destructible<T>::value) {
            destructors_.push_back(Destructor{object, destroy<T>});
        }
        return *object;
    }

    void reset();
    std::size_t used_bytes() const;
    std::size_t block_count() const;

private:
    struct Block {
        std::unique_ptr<std::uint8_t[]> bytes;
        std::size_t capacity = 0;
        std::size_t used = 0;
    };

    struct Destructor {
        void* object = nullptr;
        void (*destroy)(void*) = nullptr;
    };

    template <typename T>
    static void destroy(void* object) {
        static_cast<T*>(object)->~T();
    }

    std::size_t block_size_;
    std::vector<Block> blocks_;
    std::vector<Destructor> destructors_;

    Block& add_block(std::size_t min_capacity);
};

} // namespace jellyframe

