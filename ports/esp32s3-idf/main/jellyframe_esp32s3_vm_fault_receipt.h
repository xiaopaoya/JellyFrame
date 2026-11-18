#pragma once

#include "device_runtime_contracts/device_runtime_protocol.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

namespace jellyframe_esp32s3 {

// Value-only diagnostic retained across a software restart. It does not
// grant trust, disable an installed package, or alter flash registry state.
struct VmFaultBootReceipt {
    std::uint32_t check;
    std::uint32_t code;
    std::uint32_t generation;
    std::array<char, jellyframe::kDeviceMaxAppIdBytes + 1> app_id;
};
static_assert(std::is_trivial_v<VmFaultBootReceipt>);

inline std::uint32_t vm_fault_boot_check(const VmFaultBootReceipt& value) noexcept {
    // FNV-1a over exact fields, independent of structure padding.
    std::uint32_t check = 0x7d35a941u;
    const auto mix = [&](std::uint32_t octet) { check = (check ^ octet) * 16777619u; };
    for (auto field : {value.code, value.generation}) {
        for (unsigned i = 0; i < 4; ++i) {
            mix(field & 0xffu);
            field >>= 8;
        }
    }
    for (unsigned char ch : value.app_id) mix(ch);
    return check == 0 ? 1 : check;
}

inline VmFaultBootReceipt make_vm_fault_boot_receipt(
    std::string_view app_id, std::uint32_t generation, int code) noexcept {
    VmFaultBootReceipt value{};
    if (code < 0 || app_id.empty() || app_id.size() >= value.app_id.size() ||
        app_id.find('\0') != std::string_view::npos) return value;
    value.code = static_cast<std::uint32_t>(code);
    value.generation = generation;
    std::copy(app_id.begin(), app_id.end(), value.app_id.begin());
    value.check = vm_fault_boot_check(value);
    return value;
}

inline bool consume_vm_fault_boot_receipt(
    VmFaultBootReceipt& retained, bool software_reset, VmFaultBootReceipt& receipt) noexcept {
    receipt = retained;
    retained.check = 0;
    return software_reset && receipt.check != 0 && receipt.check == vm_fault_boot_check(receipt) &&
        receipt.code <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()) &&
        receipt.app_id.front() != '\0' && receipt.app_id.back() == '\0';
}

} // namespace jellyframe_esp32s3
