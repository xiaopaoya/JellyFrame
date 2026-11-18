#include "jellyframe_esp32s3_package_signature.h"
#include "signature_vectors.h"
#include "mbedtls/sha256.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace signature_vectors;
using namespace jellyframe;
using namespace jellyframe_esp32s3;
namespace {
unsigned checks = 0;
void check(bool value, const char* expression, int line) {
    ++checks;
    if (!value) { std::fprintf(stderr, "line %d: %s\n", line, expression); std::exit(1); }
}
#define CHECK(x) check((x), #x, __LINE__)
bool verify(const std::uint8_t* signature = kSignature.data(),
            const std::uint8_t* domain = kDomain.data(), std::size_t domain_size = kDomain.size(),
            const std::uint8_t* header = kHeader.data(), std::size_t header_size = kHeader.size(),
            const std::uint8_t* manifest = kManifest.data(), std::size_t manifest_size = kManifest.size()) {
    return verify_device_package_signature_with_test_key(kKeyId, kX, kY, kKeyId.data(),
        signature, domain, domain_size, header, header_size, manifest, manifest_size);
}
void valid() {
    CHECK(verify());
    std::vector<std::uint8_t> message(kDomain.begin(), kDomain.end());
    message.insert(message.end(), kHeader.begin(), kHeader.end());
    message.insert(message.end(), kManifest.begin(), kManifest.end());
    std::array<std::uint8_t, 32> hash{};
    CHECK(mbedtls_sha256(message.data(), message.size(), hash.data(), 0) == 0);
    CHECK(hash == kMessageHash);
    // Valid test signature and even a matching test-only context cannot grant production trust.
    CHECK(!verify_device_package_signature(nullptr, kKeyId.data(), kSignature.data(),
        kDomain.data(), kDomain.size(), kHeader.data(), kHeader.size(), kManifest.data(), kManifest.size()));
    CHECK(!verify_device_package_signature(const_cast<std::uint8_t*>(kX.data()), kKeyId.data(),
        kSignature.data(), kDomain.data(), kDomain.size(), kHeader.data(), kHeader.size(),
        kManifest.data(), kManifest.size()));
}
void tamper() {
    auto domain = kDomain; auto header = kHeader; auto manifest = kManifest;
    for (std::size_t i = 0; i < domain.size(); ++i) {
        domain[i] ^= 1; CHECK(!verify(kSignature.data(), domain.data())); domain[i] ^= 1;
    }
    for (std::size_t i = 0; i < header.size(); ++i) {
        header[i] ^= 1; CHECK(!verify(kSignature.data(), kDomain.data(), kDomain.size(), header.data())); header[i] ^= 1;
    }
    for (std::size_t i = 0; i < manifest.size(); ++i) {
        manifest[i] ^= 1;
        CHECK(!verify(kSignature.data(), kDomain.data(), kDomain.size(), kHeader.data(), kHeader.size(), manifest.data()));
        manifest[i] ^= 1;
    }
    CHECK(!verify(kSignature.data(), kDomain.data(), kDomain.size() - 1));
    CHECK(!verify(kSignatureWithoutNul.data()));
    CHECK(!verify(kSignature.data(), kDomain.data(), kDomain.size(), kHeader.data(), kHeader.size() - 1));
    CHECK(!verify(kSignature.data(), kDomain.data(), kDomain.size(), kHeader.data(), kHeader.size(), kManifest.data(), kManifest.size() - 1));
}
void wrong_key() {
    CHECK(!verify_device_package_signature_with_test_key(kKeyId, kOtherX, kOtherY, kKeyId.data(),
        kSignature.data(), kDomain.data(), kDomain.size(), kHeader.data(), kHeader.size(), kManifest.data(), kManifest.size()));
    auto wrong_id = kKeyId; wrong_id[0] ^= 1;
    CHECK(!verify_device_package_signature_with_test_key(wrong_id, kX, kY, kKeyId.data(),
        kSignature.data(), kDomain.data(), kDomain.size(), kHeader.data(), kHeader.size(), kManifest.data(), kManifest.size()));
    std::array<std::uint8_t, 32> zero{};
    CHECK(!verify_device_package_signature_with_test_key(kKeyId, zero, zero, kKeyId.data(),
        kSignature.data(), kDomain.data(), kDomain.size(), kHeader.data(), kHeader.size(), kManifest.data(), kManifest.size()));
}
void invalid_scalars() {
    auto signature = kSignature;
    for (std::size_t i = 0; i < signature.size(); ++i) {
        signature[i] ^= 1; CHECK(!verify(signature.data())); signature[i] ^= 1;
    }
    for (unsigned offset : {0u, 32u}) {
        signature = kSignature; std::fill_n(signature.begin() + offset, 32, 0); CHECK(!verify(signature.data()));
        signature = kSignature; std::fill_n(signature.begin() + offset, 32, 0xff); CHECK(!verify(signature.data()));
    }
    // The API consumes exactly 64 raw bytes; a DER prefix is not a raw signature.
    static_assert(kSignatureDer.size() >= 64);
    CHECK(!verify(kSignatureDer.data()));
}
void arguments() {
    CHECK(!verify(nullptr));
    CHECK(!verify(kSignature.data(), nullptr));
    CHECK(!verify(kSignature.data(), kDomain.data(), 0));
    CHECK(!verify(kSignature.data(), kDomain.data(), kDomain.size(), nullptr));
    CHECK(!verify(kSignature.data(), kDomain.data(), kDomain.size(), kHeader.data(), 0));
    CHECK(!verify(kSignature.data(), kDomain.data(), kDomain.size(), kHeader.data(), kHeader.size(), nullptr));
    CHECK(!verify(kSignature.data(), kDomain.data(), kDomain.size(), kHeader.data(), kHeader.size(), kManifest.data(), 0));
    CHECK(!verify_device_package_signature_with_test_key(kKeyId, kX, kY, nullptr,
        kSignature.data(), kDomain.data(), kDomain.size(), kHeader.data(), kHeader.size(), kManifest.data(), kManifest.size()));
}
unsigned callback_count = 0;
bool callback(void*, const std::uint8_t* id, const std::uint8_t* sig,
              const std::uint8_t* domain, std::size_t ds, const std::uint8_t* header,
              std::size_t hs, const std::uint8_t* manifest, std::size_t ms) {
    ++callback_count;
    return verify_device_package_signature_with_test_key(kKeyId, kX, kY, id, sig, domain, ds, header, hs, manifest, ms);
}
void parser() {
    std::vector<std::uint8_t> package(kHeader.begin(), kHeader.end());
    package.insert(package.end(), kManifest.begin(), kManifest.end());
    package.insert(package.end(), kSignature.begin(), kSignature.end());
    package.insert(package.end(), kBundle.begin(), kBundle.end());
    DeviceSignedPackageResourceMetadata resource{"/a.txt", kResourceHash};
    DeviceSignedPackageBundleMetadata metadata{"crypto.test", "1.0.0", 7, kBundleHash, &resource, 1};
    DeviceSignedPackageView view;
    std::array<DeviceSignedPackageResourceOutput, 1> resources{};
    auto inspect = [&](DeviceSignedPackageSignatureVerifier verifier) {
        return inspect_device_signed_package(package.data(), package.size(), metadata, verifier, nullptr,
            resources.data(), resources.size(), {}, view);
    };
    CHECK(inspect(callback) == DeviceSignedPackageStatus::Ok);
    CHECK(callback_count == 1 && view.resource_count == 1 && resources[0].path == "/a.txt");
    CHECK(view.key_id == kKeyId && view.bundle_size == kBundle.size());
    package[kHeader.size() + kManifest.size()] ^= 1;
    CHECK(inspect(callback) == DeviceSignedPackageStatus::SignatureRejected);
    CHECK(callback_count == 2 && view.bundle_bytes == nullptr && resources[0].path.empty());
    package[kHeader.size() + kManifest.size()] ^= 1;
    CHECK(inspect(verify_device_package_signature) == DeviceSignedPackageStatus::SignatureRejected);
    CHECK(view.bundle_bytes == nullptr && resources[0].path.empty());
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (!std::strcmp(argv[1], "valid")) valid();
    else if (!std::strcmp(argv[1], "tamper")) tamper();
    else if (!std::strcmp(argv[1], "wrong_key")) wrong_key();
    else if (!std::strcmp(argv[1], "invalid_scalars")) invalid_scalars();
    else if (!std::strcmp(argv[1], "arguments")) arguments();
    else if (!std::strcmp(argv[1], "parser")) parser();
    else return 2;
    std::printf("%s: %u checks passed\n", argv[1], checks);
}
