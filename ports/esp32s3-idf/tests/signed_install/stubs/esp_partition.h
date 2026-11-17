#pragma once
#include <cstddef>
#include <cstdint>
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
struct esp_partition_t { std::uint32_t size; };
esp_err_t esp_partition_read(const esp_partition_t*, std::size_t, void*, std::size_t);
esp_err_t esp_partition_write(const esp_partition_t*, std::size_t, const void*, std::size_t);
esp_err_t esp_partition_erase_range(const esp_partition_t*, std::size_t, std::size_t);
