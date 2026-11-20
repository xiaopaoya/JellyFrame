#pragma once
// Shared host NOR harness; never linked into target firmware.
#include "jellyframe_esp32s3_device_image_store.h"
#include "jellyframe_esp32s3_package_signature.h"
#if defined(JELLYFRAME_PACKAGE_SIGNATURE_TESTING)
#include "signature_vectors.h"
#endif
#include "esp_rom_crc.h"
#include "mbedtls/sha256.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {
#ifndef JFSG_HOST_PARTITION_BYTES
#define JFSG_HOST_PARTITION_BYTES (2 * 1024 * 1024)
#endif
std::vector<std::uint8_t> flash(JFSG_HOST_PARTITION_BYTES, 0xff);
esp_partition_t partition{static_cast<std::uint32_t>(flash.size())};
bool refuse_allocation = false;
unsigned checks = 0;
#if defined(JELLYFRAME_PACKAGE_SIGNATURE_TESTING)
bool trust_test_key = true, wrong_test_key = false;
unsigned crypto_calls = 0;
#endif
constexpr const char* app_id = "org.jellyframe.signed-test";
void check(bool result, const char* expression, int line) {
    ++checks;
    if (!result) { std::fprintf(stderr, "%d: %s\n", line, expression); std::exit(1); }
}
#define CHECK(x) check((x), #x, __LINE__)
using namespace jellyframe;
using namespace jellyframe_esp32s3;
std::vector<std::uint8_t> read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    CHECK(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
std::unique_ptr<DeviceImageStore> boot() {
    auto store = std::make_unique<DeviceImageStore>(&partition);
    CHECK(store->initialize());
    return store;
}
DeviceInstallRequest stage(DeviceImageStore& store, const std::vector<std::uint8_t>& package) {
    DeviceInstallRequest request;
    request.transaction_id = 1;
    std::strcpy(request.app_id.data(), app_id);
    request.bundle_bytes = static_cast<std::uint32_t>(package.size());
    request.bundle_crc32 = esp_rom_crc32_le(0, package.data(), package.size());
    CHECK(store.begin_staging(request));
    for (std::size_t offset = 0; offset < package.size(); offset += 97) {
        CHECK(store.write_staging(static_cast<std::uint32_t>(offset), package.data() + offset,
              std::min<std::size_t>(97, package.size() - offset)));
    }
    return request;
}
void install(DeviceImageStore& store, const std::vector<std::uint8_t>& package) {
    const auto request = stage(store, package);
    CHECK(store.verify_staging(request));
    CHECK(store.commit_staging(request));
}
void assert_version(DeviceImageStore& store, unsigned version) {
    DeviceAppListPayload list;
    CHECK(store.list(list));
    CHECK(list.entry_count == (version ? 1 : 0));
    if (version) CHECK(list.entries[0].version_code == version);
}
void assert_lease(DeviceImageStore& store, const std::vector<std::uint8_t>& package, unsigned version,
    const std::array<std::uint8_t, kDeviceSignedPackageKeyIdBytes>& expected_key
#if defined(JELLYFRAME_PACKAGE_SIGNATURE_TESTING)
        = signature_vectors::kKeyId
#endif
) {
    AppInstalledBundleLease* lease = nullptr;
#if defined(JELLYFRAME_PACKAGE_SIGNATURE_TESTING)
    const auto before = crypto_calls;
#endif
    CHECK(store.acquire_installed_bundle(app_id, lease) == DeviceBundleStatus::Ok);
#if defined(JELLYFRAME_PACKAGE_SIGNATURE_TESTING)
    CHECK(lease != nullptr && crypto_calls > before);
#else
    CHECK(lease != nullptr);
#endif
    CHECK(lease->descriptor().summary.version_code == version);
    std::array<std::uint8_t, 16> key{};
    CHECK(lease->copy_authenticated_signer_key_id(key) && key == expected_key);
    std::array<std::uint8_t, 32> hash{}, expected{};
    CHECK(lease->copy_authenticated_resource_sha256("/index.html", hash));
    const char content[] = "<body>signed app</body>";
    CHECK(mbedtls_sha256(reinterpret_cast<const std::uint8_t*>(content), sizeof(content)-1, expected.data(), 0) == 0);
    CHECK(hash == expected);
    CHECK(!lease->copy_authenticated_resource_sha256("/missing", hash));
    CHECK(std::all_of(hash.begin(), hash.end(), [](auto b) { return b == 0; }));
    const std::uint32_t manifest_size = package[16] | (package[17] << 8) | (package[18] << 16) | (package[19] << 24);
    std::vector<std::uint8_t> bytes(package.size() - 128 - manifest_size);
    CHECK(lease->read_at(0, bytes.data(), bytes.size()));
    CHECK(std::equal(bytes.begin(), bytes.end(), package.begin() + 128 + manifest_size));
    DeviceBundleTextPipeline pipeline;
    DeviceBundleInspectionWorkspace workspace;
    CHECK(read_device_bundle_text_pipeline(*lease, lease->descriptor(), workspace, pipeline) == DeviceBundleStatus::Ok);
    CHECK(pipeline.kind == (version == 1 ? DeviceBundleTextPipelineKind::Scalar : DeviceBundleTextPipelineKind::Jfsp1BlockV11));
    lease->release();
}
} // namespace

esp_err_t esp_partition_read(const esp_partition_t* part, std::size_t offset, void* out, std::size_t bytes) {
    if (!out || offset > part->size || bytes > part->size - offset) return 1;
    std::memcpy(out, flash.data() + offset, bytes); return ESP_OK;
}
esp_err_t esp_partition_write(const esp_partition_t* part, std::size_t offset, const void* input, std::size_t bytes) {
    if (!input || offset > part->size || bytes > part->size - offset) return 1;
    const auto* source = static_cast<const std::uint8_t*>(input);
    for (std::size_t i = 0; i < bytes; ++i) if ((flash[offset+i] & source[i]) != source[i]) return 1;
    for (std::size_t i = 0; i < bytes; ++i) flash[offset+i] &= source[i];
    return ESP_OK;
}
esp_err_t esp_partition_erase_range(const esp_partition_t* part, std::size_t offset, std::size_t bytes) {
    if (offset % 4096 || bytes % 4096 || offset > part->size || bytes > part->size - offset) return 1;
    std::fill_n(flash.begin() + offset, bytes, 0xff); return ESP_OK;
}
std::uint32_t esp_rom_crc32_le(std::uint32_t crc, const std::uint8_t* bytes, std::size_t size) {
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
std::int64_t esp_timer_get_time() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
[[noreturn]] void esp_restart() { std::abort(); }
void* heap_caps_malloc(std::size_t bytes, unsigned) { return refuse_allocation ? nullptr : std::malloc(bytes); }
void heap_caps_free(void* memory) { std::free(memory); }

#if defined(JELLYFRAME_PACKAGE_SIGNATURE_TESTING)
namespace jellyframe_esp32s3 {
bool verify_host_fixture_signature(void*, const std::uint8_t* id, const std::uint8_t* signature,
    const std::uint8_t* domain, std::size_t domain_size, const std::uint8_t* header,
    std::size_t header_size, const std::uint8_t* manifest, std::size_t manifest_size) {
    ++crypto_calls;
    using namespace signature_vectors;
    if (!trust_test_key) return verify_device_package_signature(nullptr, id, signature, domain, domain_size,
        header, header_size, manifest, manifest_size);
    return verify_device_package_signature_with_test_key(kKeyId, wrong_test_key ? kOtherX : kX,
        wrong_test_key ? kOtherY : kY, id, signature, domain, domain_size, header, header_size, manifest, manifest_size);
}
} // namespace jellyframe_esp32s3
#endif
