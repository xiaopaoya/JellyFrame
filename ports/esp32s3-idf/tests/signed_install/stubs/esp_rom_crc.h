#pragma once
#include <cstddef>
#include <cstdint>
std::uint32_t esp_rom_crc32_le(std::uint32_t, const std::uint8_t*, std::size_t);
