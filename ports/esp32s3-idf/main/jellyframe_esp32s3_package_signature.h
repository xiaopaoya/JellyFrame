#pragma once

#include "device_runtime_contracts/device_signed_package.h"

namespace jellyframe_esp32s3 {

// Verifies the JFSG v1 canonical message with an ESP-IDF mbedTLS ECDSA
// P-256 key selected by key id. The production allowlist is intentionally
// empty until JellyFrame product public keys are provisioned.
bool verify_device_package_signature(
    void* context,
    const std::uint8_t* key_id,
    const std::uint8_t* signature_raw_rs,
    const std::uint8_t* domain_with_nul,
    std::size_t domain_size,
    const std::uint8_t* header,
    std::size_t header_size,
    const std::uint8_t* manifest,
    std::size_t manifest_size);

#if defined(JELLYFRAME_PACKAGE_SIGNATURE_TESTING)
// Test-only trusted key injection; never compiled into the device callback.
bool verify_device_package_signature_with_test_key(
    const std::array<std::uint8_t, 16>& trusted_key_id,
    const std::array<std::uint8_t, 32>& x,
    const std::array<std::uint8_t, 32>& y,
    const std::uint8_t* key_id,
    const std::uint8_t* signature_raw_rs,
    const std::uint8_t* domain_with_nul,
    std::size_t domain_size,
    const std::uint8_t* header,
    std::size_t header_size,
    const std::uint8_t* manifest,
    std::size_t manifest_size);
#endif

} // namespace jellyframe_esp32s3
