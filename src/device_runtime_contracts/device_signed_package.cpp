#include "device_runtime_contracts/device_signed_package.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace jellyframe {
namespace {

constexpr std::uint8_t kMagic[8] = {'J', 'F', 'S', 'G', 0, 0, 0, 0};
constexpr std::uint8_t kBundleMagic[8] = {'J', 'F', 'A', 'P', 'P', 'V', '0', 0};
constexpr std::uint16_t kFormatVersion = 1;
constexpr std::size_t kAlgorithmOffset = 12;
constexpr std::size_t kManifestBytesOffset = 16;
constexpr std::size_t kSignatureBytesOffset = 20;
constexpr std::size_t kKeyIdOffset = 24;
constexpr std::size_t kReservedOffset = 40;
constexpr char kSignatureDomain[] = "JellyFrame Signed Package v1";

std::uint16_t read_le16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[1]) << 8u);
}

std::uint32_t read_le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8u) |
           (static_cast<std::uint32_t>(p[2]) << 16u) |
           (static_cast<std::uint32_t>(p[3]) << 24u);
}

bool all_zero(const std::uint8_t* p, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        if (p[i] != 0) return false;
    }
    return true;
}

bool utf8_is_valid(std::string_view text) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(text.data());
    std::size_t i = 0;
    while (i < text.size()) {
        const std::uint8_t first = p[i++];
        if (first <= 0x7fu) {
            continue;
        }
        std::uint32_t codepoint = 0;
        std::size_t continuation_count = 0;
        if (first >= 0xc2u && first <= 0xdfu) {
            codepoint = first & 0x1fu;
            continuation_count = 1;
        } else if (first >= 0xe0u && first <= 0xefu) {
            codepoint = first & 0x0fu;
            continuation_count = 2;
        } else if (first >= 0xf0u && first <= 0xf4u) {
            codepoint = first & 0x07u;
            continuation_count = 3;
        } else {
            return false;
        }
        if (continuation_count > text.size() - i) return false;
        for (std::size_t j = 0; j < continuation_count; ++j) {
            const std::uint8_t next = p[i++];
            if ((next & 0xc0u) != 0x80u) return false;
            codepoint = (codepoint << 6u) | (next & 0x3fu);
        }
        if ((continuation_count == 2 && codepoint < 0x800u) ||
            (continuation_count == 3 && codepoint < 0x10000u) ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu) || codepoint > 0x10ffffu) {
            return false;
        }
    }
    return true;
}

bool utf8_has_control(std::string_view text) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(text.data());
    std::size_t i = 0;
    while (i < text.size()) {
        const std::uint8_t first = p[i++];
        std::uint32_t codepoint = first;
        std::size_t continuation_count = 0;
        if (first >= 0xc2u && first <= 0xdfu) {
            codepoint = first & 0x1fu;
            continuation_count = 1;
        } else if (first >= 0xe0u && first <= 0xefu) {
            codepoint = first & 0x0fu;
            continuation_count = 2;
        } else if (first >= 0xf0u && first <= 0xf4u) {
            codepoint = first & 0x07u;
            continuation_count = 3;
        }
        for (std::size_t j = 0; j < continuation_count; ++j) {
            codepoint = (codepoint << 6u) | (p[i++] & 0x3fu);
        }
        if (codepoint < 0x20u || codepoint == 0x7fu ||
            (codepoint >= 0x80u && codepoint <= 0x9fu)) return true;
    }
    return false;
}

bool path_is_normalized(std::string_view path) {
    if (path.size() < 2 || path[0] != '/' || !utf8_is_valid(path) || utf8_has_control(path)) return false;
    std::size_t segment_start = 1;
    for (std::size_t i = 1; i <= path.size(); ++i) {
        const bool end = i == path.size();
        if (!end && (path[i] == '\\' || path[i] == ':')) return false;
        if (!end && path[i] != '/') continue;
        if (i == segment_start) return false;
        const std::size_t segment_size = i - segment_start;
        if ((segment_size == 1 && path[segment_start] == '.') ||
            (segment_size == 2 && path[segment_start] == '.' && path[segment_start + 1] == '.')) {
            return false;
        }
        segment_start = i + 1;
    }
    return true;
}

int compare_bytes(std::string_view left, std::string_view right) {
    const std::size_t common = std::min(left.size(), right.size());
    for (std::size_t i = 0; i < common; ++i) {
        const auto a = static_cast<unsigned char>(left[i]);
        const auto b = static_cast<unsigned char>(right[i]);
        if (a < b) return -1;
        if (a > b) return 1;
    }
    if (left.size() < right.size()) return -1;
    if (left.size() > right.size()) return 1;
    return 0;
}

bool equal_hash(const std::uint8_t* left, const std::array<std::uint8_t, kDeviceSignedPackageSha256Bytes>& right) {
    return std::memcmp(left, right.data(), right.size()) == 0;
}

struct OutputRollback {
    DeviceSignedPackageResourceOutput* output;
    std::size_t count;
    bool committed = false;
    ~OutputRollback() {
        if (!committed && output != nullptr) {
            for (std::size_t i = 0; i < count; ++i) output[i] = {};
        }
    }
};

class Cursor {
public:
    Cursor(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}
    bool read_u16(std::uint16_t& value) {
        if (remaining() < 2) return false;
        value = read_le16(data_ + offset_);
        offset_ += 2;
        return true;
    }
    bool read_u32(std::uint32_t& value) {
        if (remaining() < 4) return false;
        value = read_le32(data_ + offset_);
        offset_ += 4;
        return true;
    }
    bool read_view(std::size_t size, std::string_view& value) {
        if (size > remaining()) return false;
        value = std::string_view(reinterpret_cast<const char*>(data_ + offset_), size);
        offset_ += size;
        return true;
    }
    bool read_bytes(std::size_t size, const std::uint8_t*& value) {
        if (size > remaining()) return false;
        value = data_ + offset_;
        offset_ += size;
        return true;
    }
    std::size_t remaining() const { return size_ - offset_; }
    std::size_t offset() const { return offset_; }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t offset_ = 0;
};

} // namespace

DeviceSignedPackageStatus inspect_device_signed_package(
    const std::uint8_t* package,
    std::size_t package_size,
    const DeviceSignedPackageBundleMetadata& bundle_metadata,
    DeviceSignedPackageSignatureVerifier verify_signature,
    void* verifier_context,
    DeviceSignedPackageResourceOutput* output_resources,
    std::size_t output_resource_capacity,
    const DeviceSignedPackageValidationPolicy& policy,
    DeviceSignedPackageView& result) {
    result = {};
    const std::size_t clear_count = std::min<std::size_t>(output_resource_capacity,
                                                          kDeviceSignedPackageDefaultMaxResources);
    if (output_resources != nullptr) {
        for (std::size_t i = 0; i < clear_count; ++i) output_resources[i] = {};
    }
    OutputRollback output_rollback{output_resources, clear_count};

    if (package == nullptr || verify_signature == nullptr ||
        (bundle_metadata.resource_count != 0 && bundle_metadata.resources == nullptr) ||
        (output_resource_capacity != 0 && output_resources == nullptr) ||
        policy.max_package_bytes == 0 || policy.max_bundle_bytes == 0 || policy.max_manifest_bytes == 0 ||
        policy.max_package_bytes > kDeviceSignedPackageMaxPackageBytes ||
        policy.max_bundle_bytes > kDeviceSignedPackageMaxBundleBytes ||
        policy.max_manifest_bytes > kDeviceSignedPackageMaxManifestBytes ||
        policy.max_resource_count == 0 ||
        policy.max_resource_count > kDeviceSignedPackageDefaultMaxResources) {
        return DeviceSignedPackageStatus::InvalidArgument;
    }
    if (package_size > policy.max_package_bytes || package_size > std::numeric_limits<std::uint32_t>::max()) {
        return DeviceSignedPackageStatus::PayloadTooLarge;
    }
    if (package_size < kDeviceSignedPackageHeaderBytes) return DeviceSignedPackageStatus::Truncated;
    if (!std::equal(std::begin(kMagic), std::end(kMagic), package)) return DeviceSignedPackageStatus::InvalidMagic;

    if (read_le16(package + 8) != kFormatVersion ||
        read_le16(package + 10) != kDeviceSignedPackageHeaderBytes) {
        return DeviceSignedPackageStatus::UnsupportedVersion;
    }
    const std::uint16_t algorithm = read_le16(package + kAlgorithmOffset);
    if (algorithm != kDeviceSignedPackageAlgorithmEcdsaP256Sha256) {
        return DeviceSignedPackageStatus::UnsupportedAlgorithm;
    }
    if (read_le16(package + 14) != 0 ||
        read_le16(package + kSignatureBytesOffset) != kDeviceSignedPackageSignatureBytes ||
        read_le16(package + 22) != 0 || !all_zero(package + kReservedOffset,
                                                  kDeviceSignedPackageHeaderBytes - kReservedOffset)) {
        return DeviceSignedPackageStatus::InvalidHeader;
    }
    const std::uint32_t manifest_size = read_le32(package + kManifestBytesOffset);
    if (manifest_size == 0 || manifest_size > policy.max_manifest_bytes) {
        return DeviceSignedPackageStatus::PayloadTooLarge;
    }
    const std::size_t bytes_after_header = package_size - kDeviceSignedPackageHeaderBytes;
    if (manifest_size > bytes_after_header ||
        kDeviceSignedPackageSignatureBytes > bytes_after_header - manifest_size) {
        return DeviceSignedPackageStatus::Truncated;
    }
    const std::size_t payload_offset = kDeviceSignedPackageHeaderBytes + manifest_size +
                                       kDeviceSignedPackageSignatureBytes;
    if (package_size - payload_offset < 56) return DeviceSignedPackageStatus::Truncated;
    if (package_size - payload_offset > policy.max_bundle_bytes) {
        return DeviceSignedPackageStatus::PayloadTooLarge;
    }
    if (!std::equal(std::begin(kBundleMagic), std::end(kBundleMagic), package + payload_offset)) {
        return DeviceSignedPackageStatus::InvalidBundle;
    }
    if (bundle_metadata.resource_count > policy.max_resource_count ||
        bundle_metadata.resource_count > output_resource_capacity) {
        return DeviceSignedPackageStatus::BufferTooSmall;
    }
    if (bundle_metadata.app_id.empty() || bundle_metadata.app_id.size() > kDeviceSignedPackageMaxAppIdBytes ||
        bundle_metadata.version.empty() || bundle_metadata.version.size() > kDeviceSignedPackageMaxVersionBytes ||
        !utf8_is_valid(bundle_metadata.app_id) || utf8_has_control(bundle_metadata.app_id) ||
        !utf8_is_valid(bundle_metadata.version) || utf8_has_control(bundle_metadata.version)) {
        return DeviceSignedPackageStatus::InvalidArgument;
    }
    for (std::size_t i = 0; i < bundle_metadata.resource_count; ++i) {
        const auto& item = bundle_metadata.resources[i];
        if (item.path.size() > kDeviceSignedPackageMaxPathBytes || !path_is_normalized(item.path) ||
            (i != 0 && compare_bytes(bundle_metadata.resources[i - 1].path, item.path) >= 0)) {
            return DeviceSignedPackageStatus::InvalidArgument;
        }
    }

    const std::uint8_t* manifest = package + kDeviceSignedPackageHeaderBytes;
    Cursor cursor(manifest, manifest_size);
    std::uint16_t text_size = 0;
    std::string_view app_id;
    if (!cursor.read_u16(text_size) || text_size == 0 || text_size > kDeviceSignedPackageMaxAppIdBytes ||
        !cursor.read_view(text_size, app_id)) return DeviceSignedPackageStatus::InvalidManifest;
    if (!utf8_is_valid(app_id) || utf8_has_control(app_id)) return DeviceSignedPackageStatus::InvalidUtf8;
    std::string_view version;
    if (!cursor.read_u16(text_size) || text_size == 0 || text_size > kDeviceSignedPackageMaxVersionBytes ||
        !cursor.read_view(text_size, version)) return DeviceSignedPackageStatus::InvalidManifest;
    if (!utf8_is_valid(version) || utf8_has_control(version)) return DeviceSignedPackageStatus::InvalidUtf8;

    std::uint32_t version_code = 0;
    const std::uint8_t* bundle_sha256 = nullptr;
    std::uint32_t resource_count = 0;
    if (!cursor.read_u32(version_code) ||
        !cursor.read_bytes(kDeviceSignedPackageSha256Bytes, bundle_sha256) ||
        !cursor.read_u32(resource_count)) {
        return DeviceSignedPackageStatus::InvalidManifest;
    }
    if (resource_count > policy.max_resource_count) return DeviceSignedPackageStatus::PayloadTooLarge;
    if (resource_count != bundle_metadata.resource_count) return DeviceSignedPackageStatus::MetadataMismatch;
    if (resource_count > output_resource_capacity) return DeviceSignedPackageStatus::BufferTooSmall;
    if (app_id != bundle_metadata.app_id || version != bundle_metadata.version ||
        version_code != bundle_metadata.version_code || !equal_hash(bundle_sha256, bundle_metadata.bundle_sha256)) {
        return DeviceSignedPackageStatus::MetadataMismatch;
    }

    for (std::size_t i = 0; i < resource_count; ++i) {
        std::uint16_t path_size = 0;
        std::string_view path;
        const std::uint8_t* hash = nullptr;
        if (!cursor.read_u16(path_size) || path_size == 0 || path_size > kDeviceSignedPackageMaxPathBytes ||
            !cursor.read_view(path_size, path) || !cursor.read_bytes(kDeviceSignedPackageSha256Bytes, hash)) {
            return DeviceSignedPackageStatus::InvalidManifest;
        }
        if (!utf8_is_valid(path)) return DeviceSignedPackageStatus::InvalidUtf8;
        if (!path_is_normalized(path)) return DeviceSignedPackageStatus::InvalidPath;
        if (i != 0 && compare_bytes(output_resources[i - 1].path, path) >= 0) {
            return DeviceSignedPackageStatus::DuplicateOrUnsortedPath;
        }
        output_resources[i].path = path;
        std::memcpy(output_resources[i].sha256.data(), hash, output_resources[i].sha256.size());
    }
    if (cursor.remaining() != 0) return DeviceSignedPackageStatus::InvalidManifest;
    for (std::size_t i = 0; i < resource_count; ++i) {
        const auto& parsed = output_resources[i];
        const auto& expected = bundle_metadata.resources[i];
        if (parsed.path != expected.path || parsed.sha256 != expected.sha256) {
            return DeviceSignedPackageStatus::MetadataMismatch;
        }
    }

    const std::uint8_t* signature = manifest + manifest_size;
    const std::uint8_t* bundle = package + payload_offset;
    DeviceSignedPackageView candidate;
    candidate.app_id = app_id;
    candidate.version = version;
    candidate.version_code = version_code;
    std::memcpy(candidate.bundle_sha256.data(), bundle_sha256, candidate.bundle_sha256.size());
    std::memcpy(candidate.key_id.data(), package + kKeyIdOffset, candidate.key_id.size());
    candidate.signature = signature;
    candidate.bundle_bytes = bundle;
    candidate.bundle_size = package_size - payload_offset;
    candidate.resource_count = resource_count;

    if (!verify_signature(verifier_context,
                          candidate.key_id.data(),
                          signature,
                          reinterpret_cast<const std::uint8_t*>(kSignatureDomain),
                          sizeof(kSignatureDomain),
                          package,
                          kDeviceSignedPackageHeaderBytes,
                          manifest,
                          manifest_size)) {
        return DeviceSignedPackageStatus::SignatureRejected;
    }
    result = candidate;
    output_rollback.committed = true;
    return DeviceSignedPackageStatus::Ok;
}

const char* device_signed_package_status_name(DeviceSignedPackageStatus status) {
    switch (status) {
        case DeviceSignedPackageStatus::Ok: return "ok";
        case DeviceSignedPackageStatus::InvalidArgument: return "invalid-argument";
        case DeviceSignedPackageStatus::PayloadTooLarge: return "payload-too-large";
        case DeviceSignedPackageStatus::Truncated: return "truncated";
        case DeviceSignedPackageStatus::InvalidMagic: return "invalid-magic";
        case DeviceSignedPackageStatus::UnsupportedVersion: return "unsupported-version";
        case DeviceSignedPackageStatus::UnsupportedAlgorithm: return "unsupported-algorithm";
        case DeviceSignedPackageStatus::InvalidHeader: return "invalid-header";
        case DeviceSignedPackageStatus::InvalidManifest: return "invalid-manifest";
        case DeviceSignedPackageStatus::InvalidBundle: return "invalid-bundle";
        case DeviceSignedPackageStatus::InvalidUtf8: return "invalid-utf8";
        case DeviceSignedPackageStatus::InvalidPath: return "invalid-path";
        case DeviceSignedPackageStatus::DuplicateOrUnsortedPath: return "duplicate-or-unsorted-path";
        case DeviceSignedPackageStatus::MetadataMismatch: return "metadata-mismatch";
        case DeviceSignedPackageStatus::SignatureRejected: return "signature-rejected";
        case DeviceSignedPackageStatus::BufferTooSmall: return "buffer-too-small";
    }
    return "unknown";
}

} // namespace jellyframe
