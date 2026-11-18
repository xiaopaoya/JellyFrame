#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace jellyframe {

// JFSG v1 is a signed envelope around the original, unmodified JFAPP byte
// sequence. All integers are little-endian. The manifest and header are the
// only signed envelope fields; the bundle is identified by its SHA-256.
constexpr std::size_t kDeviceSignedPackageHeaderBytes = 64;
constexpr std::size_t kDeviceSignedPackageSignatureBytes = 64;
constexpr std::size_t kDeviceSignedPackageKeyIdBytes = 16;
constexpr std::size_t kDeviceSignedPackageSha256Bytes = 32;
constexpr std::size_t kDeviceSignedPackageMaxAppIdBytes = 95;
constexpr std::size_t kDeviceSignedPackageMaxVersionBytes = 63;
constexpr std::size_t kDeviceSignedPackageMaxPathBytes = 255;
constexpr std::size_t kDeviceSignedPackageDefaultMaxResources = 4096;
constexpr std::uint32_t kDeviceSignedPackageMaxPackageBytes = 4u * 1024u * 1024u;
constexpr std::uint32_t kDeviceSignedPackageMaxBundleBytes = 4u * 1024u * 1024u;
constexpr std::uint32_t kDeviceSignedPackageMaxManifestBytes = 1024u * 1024u;
constexpr std::uint16_t kDeviceSignedPackageAlgorithmEcdsaP256Sha256 = 1;

struct DeviceSignedPackageResourceMetadata {
    std::string_view path;
    std::array<std::uint8_t, kDeviceSignedPackageSha256Bytes> sha256{};
};

// Facts must come from inspection/hash of the same immutable JFAPPV0 snapshot
// passed to inspect_device_signed_package. This parser compares them exactly;
// it does not compute SHA-256 or validate the underlying JFAPPV0 container.
// The caller's resources must be unique and sorted by unsigned UTF-8 bytes,
// matching the canonical order required in the signed manifest.
struct DeviceSignedPackageBundleMetadata {
    std::string_view app_id;
    std::string_view version;
    std::uint32_t version_code = 0;
    std::array<std::uint8_t, kDeviceSignedPackageSha256Bytes> bundle_sha256{};
    const DeviceSignedPackageResourceMetadata* resources = nullptr;
    std::size_t resource_count = 0;
};

struct DeviceSignedPackageValidationPolicy {
    // Policy values may narrow, but never raise the contract's hard ceilings.
    std::uint32_t max_package_bytes = kDeviceSignedPackageMaxPackageBytes;
    std::uint32_t max_bundle_bytes = kDeviceSignedPackageMaxBundleBytes;
    std::uint32_t max_manifest_bytes = kDeviceSignedPackageMaxManifestBytes;
    std::uint32_t max_resource_count = static_cast<std::uint32_t>(kDeviceSignedPackageDefaultMaxResources);
};

enum class DeviceSignedPackageStatus : std::uint8_t {
    Ok,
    InvalidArgument,
    PayloadTooLarge,
    Truncated,
    InvalidMagic,
    UnsupportedVersion,
    UnsupportedAlgorithm,
    InvalidHeader,
    InvalidManifest,
    InvalidBundle,
    InvalidUtf8,
    InvalidPath,
    DuplicateOrUnsortedPath,
    MetadataMismatch,
    SignatureRejected,
    BufferTooSmall,
};

struct DeviceSignedPackageResourceView {
    std::string_view path;
    std::array<std::uint8_t, kDeviceSignedPackageSha256Bytes> sha256{};
};

struct DeviceSignedPackageView {
    std::string_view app_id;
    std::string_view version;
    std::uint32_t version_code = 0;
    std::array<std::uint8_t, kDeviceSignedPackageSha256Bytes> bundle_sha256{};
    std::array<std::uint8_t, kDeviceSignedPackageKeyIdBytes> key_id{};
    const std::uint8_t* signature = nullptr;
    const std::uint8_t* bundle_bytes = nullptr;
    std::size_t bundle_size = 0;
    std::size_t resource_count = 0;
};

// Resource views point into the input package, which must remain alive and
// immutable for the lifetime of the result and any callback use.
using DeviceSignedPackageResourceOutput = DeviceSignedPackageResourceView;

// A platform crypto adapter must synchronously verify raw ECDSA P-256
// signature r||s (64 bytes) using SHA-256 over:
//   domain[sizeof("JellyFrame Signed Package v1")] || header || manifest
// The domain array includes its terminating NUL. Returning true means the
// adapter accepted the signature for key_id. This contract has no crypto
// implementation and a callback's presence alone does not establish trust.
using DeviceSignedPackageSignatureVerifier = bool (*)(
    void* context,
    const std::uint8_t* key_id,
    const std::uint8_t* signature_raw_rs,
    const std::uint8_t* domain_with_nul,
    std::size_t domain_size,
    const std::uint8_t* header,
    std::size_t header_size,
    const std::uint8_t* manifest,
    std::size_t manifest_size);

// Parses and verifies one exact envelope. Manifest encoding is:
//   u16 app_id_bytes, app_id UTF-8,
//   u16 version_bytes, version UTF-8,
//   u32 version_code, bundle_sha256[32], u32 resource_count,
//   repeated: u16 path_bytes, normalized UTF-8 path, sha256[32].
// Resource paths are strict bytewise-UTF-8 sorted, unique, absolute JFAPP
// paths. No trailing manifest bytes are accepted. The envelope is header,
// manifest, raw signature, then the exact JFAPPV0 bytes.
// Header byte layout is: 0..7 magic "JFSG\\0\\0\\0\\0", 8..9 version=1,
// 10..11 header_bytes=64, 12..13 algorithm=1, 14..15 flags=0,
// 16..19 manifest_bytes, 20..21 signature_bytes=64, 22..23 reserved=0,
// 24..39 key_id, 40..63 reserved=0. All integer fields are little-endian.
//
// `output_resources` is caller-owned bounded storage (at most 4096 entries
// are touched). `result` and output entries are cleared on any refusal; they
// are published only after exact metadata coverage and callback acceptance.
DeviceSignedPackageStatus inspect_device_signed_package(
    const std::uint8_t* package,
    std::size_t package_size,
    const DeviceSignedPackageBundleMetadata& bundle_metadata,
    DeviceSignedPackageSignatureVerifier verify_signature,
    void* verifier_context,
    DeviceSignedPackageResourceOutput* output_resources,
    std::size_t output_resource_capacity,
    const DeviceSignedPackageValidationPolicy& policy,
    DeviceSignedPackageView& result);

const char* device_signed_package_status_name(DeviceSignedPackageStatus status);

} // namespace jellyframe
