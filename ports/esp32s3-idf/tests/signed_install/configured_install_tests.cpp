#include "host_store_fixture.h"

namespace {
DeviceInstallStatus install_transaction(DeviceImageStore& store, std::uint32_t id,
                                        const std::vector<std::uint8_t>& package) {
    constexpr std::size_t chunk_bytes = 97;
    DeviceInstallTransaction transaction({store.max_bundle_bytes(), chunk_bytes});
    CHECK(transaction.begin(id, app_id, static_cast<std::uint32_t>(package.size()),
        esp_rom_crc32_le(0, package.data(), package.size()), false, store).accepted());
    for (std::size_t offset = 0; offset < package.size(); offset += chunk_bytes) {
        CHECK(transaction.append(id, static_cast<std::uint32_t>(offset), package.data() + offset,
            std::min(chunk_bytes, package.size() - offset), store).accepted());
    }
    const auto result = transaction.commit(id, store);
    CHECK(transaction.phase() == DeviceInstallPhase::Idle);
    return result.status;
}
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::filesystem::path fixtures(argv[1]);
    const auto trusted = read(fixtures / "trusted-v1.jfsg");
    const auto trusted_update = read(fixtures / "trusted-v2.jfsg");
    const auto untrusted_update = read(fixtures / "untrusted-v2.jfsg");
    const std::array<std::uint8_t, kDeviceSignedPackageKeyIdBytes> signer{
        'c','o','n','f','i','g','u','r','e','d','-','s','t','o','r','e'};
    auto store = boot();
    CHECK(install_transaction(*store, 1, trusted) == DeviceInstallStatus::Accepted);
    const auto generation = store->registry_generation();
    assert_version(*store, 1);
    assert_lease(*store, trusted, 1, signer);
    std::printf("configured signer accepted v1 at generation %u\n", generation);

    store.reset();
    store = boot();
    CHECK(store->registry_generation() == generation);
    assert_version(*store, 1);
    assert_lease(*store, trusted, 1, signer);
    std::puts("reconstructed v1 and authenticated its signer, resource digest and bytes");

    // Control: the same v2 bundle is valid with the configured signer.
    // Abort that staging so v1 remains the published app during the test.
    const auto control = stage(*store, trusted_update);
    CHECK(store->verify_staging(control));
    store->abort_staging(control.transaction_id);
    CHECK(install_transaction(*store, 2, untrusted_update) == DeviceInstallStatus::IntegrityRejected);
    CHECK(store->registry_generation() == generation);
    assert_version(*store, 1);
    assert_lease(*store, trusted, 1, signer);
    std::puts("untrusted signer refused with the configured key ID and v1 still available");

    store.reset();
    store = boot();
    CHECK(store->registry_generation() == generation);
    assert_version(*store, 1);
    assert_lease(*store, trusted, 1, signer);
    std::printf("reconstructed the unchanged v1 after refusal (%u checks)\n", checks);
}
