#include "device_runtime_contracts/device_signed_package.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace jellyframe;

namespace {

void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8u));
}

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void write_u16(std::uint8_t* out, std::uint16_t value) {
    out[0] = static_cast<std::uint8_t>(value);
    out[1] = static_cast<std::uint8_t>(value >> 8u);
}

void write_u32(std::uint8_t* out, std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        out[shift / 8] = static_cast<std::uint8_t>(value >> shift);
    }
}

DeviceSignedPackageResourceMetadata resource(std::string_view path, std::uint8_t seed) {
    DeviceSignedPackageResourceMetadata value;
    value.path = path;
    for (std::size_t i = 0; i < value.sha256.size(); ++i) {
        value.sha256[i] = static_cast<std::uint8_t>(seed + i);
    }
    return value;
}

std::array<std::uint8_t, kDeviceSignedPackageSha256Bytes> synthetic_bundle_hash() {
    std::array<std::uint8_t, kDeviceSignedPackageSha256Bytes> value{};
    for (std::size_t i = 0; i < value.size(); ++i) value[i] = static_cast<std::uint8_t>(0xa0u + i);
    return value;
}

std::vector<std::uint8_t> make_manifest(
    const std::vector<DeviceSignedPackageResourceMetadata>& resources,
    std::uint32_t version_code = 7,
    std::string_view app_id = "demo.app",
    std::string_view version = "1.2.3") {
    std::vector<std::uint8_t> out;
    append_u16(out, static_cast<std::uint16_t>(app_id.size()));
    out.insert(out.end(), app_id.begin(), app_id.end());
    append_u16(out, static_cast<std::uint16_t>(version.size()));
    out.insert(out.end(), version.begin(), version.end());
    append_u32(out, version_code);
    const auto whole_hash = synthetic_bundle_hash();
    out.insert(out.end(), whole_hash.begin(), whole_hash.end());
    append_u32(out, static_cast<std::uint32_t>(resources.size()));
    for (const auto& item : resources) {
        append_u16(out, static_cast<std::uint16_t>(item.path.size()));
        out.insert(out.end(), item.path.begin(), item.path.end());
        out.insert(out.end(), item.sha256.begin(), item.sha256.end());
    }
    return out;
}

std::vector<std::uint8_t> make_package(const std::vector<std::uint8_t>& manifest) {
    std::vector<std::uint8_t> out(kDeviceSignedPackageHeaderBytes, 0);
    const std::uint8_t magic[8] = {'J', 'F', 'S', 'G', 0, 0, 0, 0};
    std::memcpy(out.data(), magic, sizeof(magic));
    write_u16(out.data() + 8, 1);
    write_u16(out.data() + 10, static_cast<std::uint16_t>(kDeviceSignedPackageHeaderBytes));
    write_u16(out.data() + 12, kDeviceSignedPackageAlgorithmEcdsaP256Sha256);
    write_u32(out.data() + 16, static_cast<std::uint32_t>(manifest.size()));
    write_u16(out.data() + 20, static_cast<std::uint16_t>(kDeviceSignedPackageSignatureBytes));
    for (std::size_t i = 0; i < kDeviceSignedPackageKeyIdBytes; ++i) {
        out[24 + i] = static_cast<std::uint8_t>(0x10u + i);
    }
    out.insert(out.end(), manifest.begin(), manifest.end());
    out.insert(out.end(), kDeviceSignedPackageSignatureBytes, 0x5a);
    const std::uint8_t bundle_magic[8] = {'J', 'F', 'A', 'P', 'P', 'V', '0', 0};
    out.insert(out.end(), std::begin(bundle_magic), std::end(bundle_magic));
    out.insert(out.end(), 48, 0x33);
    return out;
}

struct VerifyProbe {
    const std::uint8_t* package = nullptr;
    std::size_t manifest_size = 0;
    bool accept = true;
    std::size_t calls = 0;
};

bool fake_signature_adapter(void* opaque,
                            const std::uint8_t* key_id,
                            const std::uint8_t* signature,
                            const std::uint8_t* domain,
                            std::size_t domain_size,
                            const std::uint8_t* header,
                            std::size_t header_size,
                            const std::uint8_t* manifest,
                            std::size_t manifest_size) {
    auto& probe = *static_cast<VerifyProbe*>(opaque);
    ++probe.calls;
    static constexpr char expected_domain[] = "JellyFrame Signed Package v1";
    assert(domain_size == sizeof(expected_domain));
    assert(std::memcmp(domain, expected_domain, sizeof(expected_domain)) == 0);
    assert(header == probe.package && header_size == kDeviceSignedPackageHeaderBytes);
    assert(manifest == probe.package + kDeviceSignedPackageHeaderBytes);
    assert(manifest_size == probe.manifest_size);
    for (std::size_t i = 0; i < kDeviceSignedPackageKeyIdBytes; ++i) {
        assert(key_id[i] == static_cast<std::uint8_t>(0x10u + i));
    }
    for (std::size_t i = 0; i < kDeviceSignedPackageSignatureBytes; ++i) assert(signature[i] == 0x5a);
    return probe.accept;
}

DeviceSignedPackageBundleMetadata metadata(const std::vector<DeviceSignedPackageResourceMetadata>& resources) {
    DeviceSignedPackageBundleMetadata value;
    value.app_id = "demo.app";
    value.version = "1.2.3";
    value.version_code = 7;
    value.bundle_sha256 = synthetic_bundle_hash();
    value.resources = resources.data();
    value.resource_count = resources.size();
    return value;
}

DeviceSignedPackageStatus inspect(std::vector<std::uint8_t>& bytes,
                                  const std::vector<DeviceSignedPackageResourceMetadata>& resources,
                                  VerifyProbe& probe,
                                  DeviceSignedPackageView& result,
                                  std::array<DeviceSignedPackageResourceOutput, 4>& output,
                                  const DeviceSignedPackageValidationPolicy& policy = {}) {
    probe.package = bytes.data();
    probe.manifest_size = bytes.size() >= 20 ?
        static_cast<std::size_t>(bytes[16]) | (static_cast<std::size_t>(bytes[17]) << 8u) |
        (static_cast<std::size_t>(bytes[18]) << 16u) | (static_cast<std::size_t>(bytes[19]) << 24u) : 0;
    return inspect_device_signed_package(bytes.data(), bytes.size(), metadata(resources),
                                         fake_signature_adapter, &probe, output.data(), output.size(),
                                         policy, result);
}

void accepts_canonical_envelope_and_preserves_original_bundle_bytes() {
    const std::vector<DeviceSignedPackageResourceMetadata> resources = {
        resource("/a.txt", 1), resource("/z.txt", 40)};
    const auto manifest = make_manifest(resources);
    auto bytes = make_package(manifest);
    VerifyProbe probe;
    DeviceSignedPackageView result;
    std::array<DeviceSignedPackageResourceOutput, 4> output{};
    assert(inspect(bytes, resources, probe, result, output) == DeviceSignedPackageStatus::Ok);
    assert(probe.calls == 1);
    assert(result.app_id == "demo.app" && result.version == "1.2.3" && result.version_code == 7);
    assert(result.resource_count == resources.size());
    assert(output[0].path == "/a.txt" && output[1].path == "/z.txt");
    assert(result.bundle_size == 56);
    assert(std::memcmp(result.bundle_bytes, "JFAPPV0\0", 8) == 0);
    assert(result.bundle_bytes == bytes.data() + kDeviceSignedPackageHeaderBytes + manifest.size() +
                                  kDeviceSignedPackageSignatureBytes);
}

void rejects_bad_header_and_truncation_before_callback() {
    const std::vector<DeviceSignedPackageResourceMetadata> resources = {resource("/a.txt", 1)};
    auto bytes = make_package(make_manifest(resources));
    VerifyProbe probe;
    DeviceSignedPackageView result;
    std::array<DeviceSignedPackageResourceOutput, 4> output{};
    bytes[12] = 99;
    assert(inspect(bytes, resources, probe, result, output) == DeviceSignedPackageStatus::UnsupportedAlgorithm);
    assert(probe.calls == 0 && result.bundle_bytes == nullptr);

    bytes = make_package(make_manifest(resources));
    bytes.resize(bytes.size() - 20);
    assert(inspect(bytes, resources, probe, result, output) == DeviceSignedPackageStatus::Truncated);
    assert(probe.calls == 0);

    bytes = make_package(make_manifest(resources));
    bytes[40] = 1;
    assert(inspect(bytes, resources, probe, result, output) == DeviceSignedPackageStatus::InvalidHeader);
    assert(probe.calls == 0);

    bytes = make_package(make_manifest(resources));
    const auto bundle_offset = bytes.size() - 56;
    bytes[bundle_offset] ^= 1;
    assert(inspect(bytes, resources, probe, result, output) == DeviceSignedPackageStatus::InvalidBundle);
    assert(probe.calls == 0);
}

void rejects_noncanonical_paths_invalid_utf8_and_trailing_manifest_bytes() {
    const std::vector<DeviceSignedPackageResourceMetadata> expected = {
        resource("/a.txt", 1), resource("/z.txt", 40)};
    VerifyProbe probe;
    DeviceSignedPackageView result;
    std::array<DeviceSignedPackageResourceOutput, 4> output{};

    auto bytes = make_package(make_manifest({resource("/a.txt", 1), resource("/a.txt", 2)}));
    assert(inspect(bytes, expected, probe, result, output) == DeviceSignedPackageStatus::DuplicateOrUnsortedPath);
    assert(probe.calls == 0);

    bytes = make_package(make_manifest({resource("/z.txt", 40), resource("/a.txt", 1)}));
    assert(inspect(bytes, expected, probe, result, output) == DeviceSignedPackageStatus::DuplicateOrUnsortedPath);
    assert(probe.calls == 0);

    bytes = make_package(make_manifest({resource("/a/../z.txt", 1)}));
    assert(inspect(bytes, {resource("/a.txt", 1)}, probe, result, output) ==
           DeviceSignedPackageStatus::InvalidPath);
    assert(probe.calls == 0);

    for (const std::string_view invalid_path : {"/a//b", "/a/./b", "/a:b", "/a\\b", "/a/\t"}) {
        bytes = make_package(make_manifest({resource(invalid_path, 1)}));
        assert(inspect(bytes, {resource("/valid.txt", 1)}, probe, result, output) ==
               DeviceSignedPackageStatus::InvalidPath);
        assert(probe.calls == 0);
    }

    const std::string bad_path("/x/\xc0\xaf", 5);
    bytes = make_package(make_manifest({resource(bad_path, 1)}));
    assert(inspect(bytes, {resource("/a.txt", 1)}, probe, result, output) == DeviceSignedPackageStatus::InvalidUtf8);
    assert(probe.calls == 0);

    auto manifest = make_manifest({resource("/a.txt", 1), resource("/z.txt", 40)});
    manifest.push_back(0);
    bytes = make_package(manifest);
    assert(inspect(bytes, expected, probe, result, output) == DeviceSignedPackageStatus::InvalidManifest);
    assert(probe.calls == 0);
}

void requires_exact_identity_and_resource_hash_coverage() {
    const std::vector<DeviceSignedPackageResourceMetadata> resources = {
        resource("/a.txt", 1), resource("/z.txt", 40)};
    auto bytes = make_package(make_manifest(resources));
    VerifyProbe probe;
    DeviceSignedPackageView result;
    std::array<DeviceSignedPackageResourceOutput, 4> output{};
    auto wrong_resource = resources;
    wrong_resource[1].sha256[0] ^= 1;
    assert(inspect(bytes, wrong_resource, probe, result, output) == DeviceSignedPackageStatus::MetadataMismatch);
    assert(probe.calls == 0);
    assert(result.bundle_bytes == nullptr && output[0].path.empty());

    wrong_resource = resources;
    wrong_resource[1].path = "/zz.txt";
    assert(inspect(bytes, wrong_resource, probe, result, output) == DeviceSignedPackageStatus::MetadataMismatch);
    assert(probe.calls == 0);

    auto wrong_identity = metadata(resources);
    wrong_identity.version_code = 8;
    probe.package = bytes.data();
    probe.manifest_size = make_manifest(resources).size();
    assert(inspect_device_signed_package(bytes.data(), bytes.size(), wrong_identity,
                                         fake_signature_adapter, &probe, output.data(), output.size(), {}, result) ==
           DeviceSignedPackageStatus::MetadataMismatch);
    assert(probe.calls == 0);

    auto wrong_bundle_hash = metadata(resources);
    wrong_bundle_hash.bundle_sha256[0] ^= 1;
    assert(inspect_device_signed_package(bytes.data(), bytes.size(), wrong_bundle_hash,
                                         fake_signature_adapter, &probe, output.data(), output.size(), {}, result) ==
           DeviceSignedPackageStatus::MetadataMismatch);
    assert(probe.calls == 0);
}

void callback_refusal_and_budgets_are_transactional() {
    const std::vector<DeviceSignedPackageResourceMetadata> resources = {resource("/a.txt", 1)};
    auto bytes = make_package(make_manifest(resources));
    VerifyProbe probe;
    probe.accept = false;
    DeviceSignedPackageView result;
    result.bundle_size = 123;
    std::array<DeviceSignedPackageResourceOutput, 4> output{};
    output[0].path = "/stale";
    assert(inspect(bytes, resources, probe, result, output) == DeviceSignedPackageStatus::SignatureRejected);
    assert(probe.calls == 1 && result.bundle_bytes == nullptr && result.bundle_size == 0);
    assert(output[0].path.empty());

    probe.accept = true;
    probe.calls = 0;
    DeviceSignedPackageValidationPolicy too_small;
    too_small.max_bundle_bytes = 55;
    assert(inspect(bytes, resources, probe, result, output, too_small) == DeviceSignedPackageStatus::PayloadTooLarge);
    assert(probe.calls == 0 && result.bundle_bytes == nullptr);

    too_small = {};
    too_small.max_manifest_bytes = 1;
    assert(inspect(bytes, resources, probe, result, output, too_small) == DeviceSignedPackageStatus::PayloadTooLarge);
    assert(probe.calls == 0);

    too_small = {};
    too_small.max_resource_count = 1;
    auto two_resources = make_package(make_manifest({resource("/a.txt", 1), resource("/z.txt", 40)}));
    assert(inspect(two_resources, {resource("/a.txt", 1), resource("/z.txt", 40)},
                   probe, result, output, too_small) == DeviceSignedPackageStatus::BufferTooSmall);
    assert(probe.calls == 0);

    too_small = {};
    too_small.max_package_bytes = 0xffffffffu;
    assert(inspect(bytes, resources, probe, result, output, too_small) == DeviceSignedPackageStatus::InvalidArgument);
    assert(probe.calls == 0);
}

} // namespace

int main() {
    accepts_canonical_envelope_and_preserves_original_bundle_bytes();
    rejects_bad_header_and_truncation_before_callback();
    rejects_noncanonical_paths_invalid_utf8_and_trailing_manifest_bytes();
    requires_exact_identity_and_resource_hash_coverage();
    callback_refusal_and_budgets_are_transactional();
    return 0;
}
