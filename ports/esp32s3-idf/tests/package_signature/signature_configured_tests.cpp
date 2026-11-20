#include "jellyframe_esp32s3_package_signature.h"
#include "configured_signature.h"
#include "signature_vectors.h"

#include <cstdio>

int main() {
    using namespace signature_vectors;
    using jellyframe_esp32s3::verify_device_package_signature;
    const auto verifies = [&](const auto& key_id, const auto& signature) {
        return verify_device_package_signature(nullptr, key_id.data(), signature.data(),
            kDomain.data(), kDomain.size(), kHeader.data(), kHeader.size(),
            kManifest.data(), kManifest.size());
    };
    if (!verifies(kKeyId, kConfiguredSignature)) {
        std::fputs("configured public point did not verify its signature\n", stderr);
        return 1;
    }
    auto wrong_id = kKeyId;
    wrong_id[0] ^= 1;
    auto changed_signature = kConfiguredSignature;
    changed_signature[0] ^= 1;
    if (verifies(wrong_id, kConfiguredSignature) || verifies(kKeyId, changed_signature) ||
        verifies(kKeyId, kSignature)) {
        std::fputs("configured verifier accepted a wrong id, modified signature, or different point\n", stderr);
        return 1;
    }
    std::puts("configured public point passed positive verification and all three refusal cases");
}
