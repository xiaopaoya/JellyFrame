#include "jellyframe_esp32s3_package_signature.h"

#include "mbedtls/ecp.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/sha256.h"
#include "mbedtls/bignum.h"

#include <array>
#include <cstring>

namespace jellyframe_esp32s3 {
namespace {

struct PublicKey {
    std::array<std::uint8_t, jellyframe::kDeviceSignedPackageKeyIdBytes> key_id;
    std::array<std::uint8_t, 32> x;
    std::array<std::uint8_t, 32> y;
};

// Product release tooling must provision reviewed public keys here. An empty
// table rejects every signature by design; no CRC or test key grants trust.
constexpr std::array<PublicKey, 0> kProductionKeys{};

bool verify_with_key(
    const PublicKey& key,
    const std::uint8_t* key_id,
    const std::uint8_t* signature_raw_rs,
    const std::uint8_t* domain_with_nul,
    std::size_t domain_size,
    const std::uint8_t* header,
    std::size_t header_size,
    const std::uint8_t* manifest,
    std::size_t manifest_size) {
    if (key_id == nullptr || signature_raw_rs == nullptr || domain_with_nul == nullptr ||
        header == nullptr || manifest == nullptr || domain_size == 0 || header_size == 0 ||
        manifest_size == 0) return false;
    if (std::memcmp(key.key_id.data(), key_id, key.key_id.size()) != 0) return false;

    std::array<std::uint8_t, 32> hash{};
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    const int sha_status = mbedtls_sha256_starts(&sha, 0) == 0 &&
        mbedtls_sha256_update(&sha, domain_with_nul, domain_size) == 0 &&
        mbedtls_sha256_update(&sha, header, header_size) == 0 &&
        mbedtls_sha256_update(&sha, manifest, manifest_size) == 0 &&
        mbedtls_sha256_finish(&sha, hash.data()) == 0;
    mbedtls_sha256_free(&sha);
    if (!sha_status) return false;

    mbedtls_ecdsa_context ecdsa;
    mbedtls_ecdsa_init(&ecdsa);
    bool accepted = false;
    do {
        if (mbedtls_ecp_group_load(&ecdsa.MBEDTLS_PRIVATE(grp), MBEDTLS_ECP_DP_SECP256R1) != 0 ||
            mbedtls_mpi_read_binary(&ecdsa.MBEDTLS_PRIVATE(Q).MBEDTLS_PRIVATE(X), key.x.data(), key.x.size()) != 0 ||
            mbedtls_mpi_read_binary(&ecdsa.MBEDTLS_PRIVATE(Q).MBEDTLS_PRIVATE(Y), key.y.data(), key.y.size()) != 0 ||
            mbedtls_mpi_lset(&ecdsa.MBEDTLS_PRIVATE(Q).MBEDTLS_PRIVATE(Z), 1) != 0 ||
            mbedtls_ecp_check_pubkey(&ecdsa.MBEDTLS_PRIVATE(grp), &ecdsa.MBEDTLS_PRIVATE(Q)) != 0) break;

        mbedtls_mpi r, s;
        mbedtls_mpi_init(&r);
        mbedtls_mpi_init(&s);
        const bool parsed = mbedtls_mpi_read_binary(&r, signature_raw_rs, 32) == 0 &&
            mbedtls_mpi_read_binary(&s, signature_raw_rs + 32, 32) == 0;
        if (parsed) {
            accepted = mbedtls_ecdsa_verify(&ecdsa.MBEDTLS_PRIVATE(grp), hash.data(), hash.size(),
                                            &ecdsa.MBEDTLS_PRIVATE(Q), &r, &s) == 0;
        }
        mbedtls_mpi_free(&r);
        mbedtls_mpi_free(&s);
    } while (false);
    mbedtls_ecdsa_free(&ecdsa);
    return accepted;
}

} // namespace

bool verify_device_package_signature(
    void* context,
    const std::uint8_t* key_id,
    const std::uint8_t* signature_raw_rs,
    const std::uint8_t* domain_with_nul,
    std::size_t domain_size,
    const std::uint8_t* header,
    std::size_t header_size,
    const std::uint8_t* manifest,
    std::size_t manifest_size) {
    (void)context;
    if (key_id == nullptr) return false;
    for (const PublicKey& candidate : kProductionKeys) {
        if (std::memcmp(candidate.key_id.data(), key_id, candidate.key_id.size()) == 0) {
            return verify_with_key(candidate, key_id, signature_raw_rs, domain_with_nul,
                                   domain_size, header, header_size, manifest, manifest_size);
        }
    }
    return false;
}

#if defined(JELLYFRAME_PACKAGE_SIGNATURE_TESTING)
// This injection entry point is absent from device builds. The production
// callback always selects exclusively from kProductionKeys.
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
    std::size_t manifest_size) {
    return verify_with_key(PublicKey{trusted_key_id, x, y}, key_id, signature_raw_rs,
                           domain_with_nul, domain_size, header, header_size,
                           manifest, manifest_size);
}
#endif

} // namespace jellyframe_esp32s3
