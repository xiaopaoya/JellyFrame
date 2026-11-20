#include "host_store_fixture.h"

int main(int argc, char** argv) {
    CHECK(argc == 5 || argc == 6);
    const std::string action = argv[1];
    const std::filesystem::path nor(argv[2]);
    const auto version = static_cast<unsigned>(std::stoul(argv[3]));
    if (std::filesystem::exists(nor)) {
        const auto bytes = read(nor);
        CHECK(bytes.size() == flash.size());
        flash = bytes;
    }
    auto store = boot();
    if (action == "install") {
        CHECK(argc == 6);
        install(*store, read(argv[5]));
    } else if (action == "rollback-rejected") {
        CHECK(store->rollback(app_id) == DeviceRequestResultCode::IntegrityFailed);
    } else {
        CHECK(action == "readback");
    }
    assert_version(*store, version);
    if (version != 0) {
        const auto package = read(argv[4]);
        constexpr std::size_t key_id_offset = 24;
        std::array<std::uint8_t, kDeviceSignedPackageKeyIdBytes> key{};
        CHECK(package.size() >= key_id_offset + key.size());
        std::copy_n(package.begin() + key_id_offset, key.size(), key.begin());
        assert_lease(*store, package, version, key);
    } else {
        AppInstalledBundleLease* lease = nullptr;
        CHECK(store->acquire_installed_bundle(app_id, lease) == DeviceBundleStatus::ResourceNotFound);
        CHECK(lease == nullptr);
    }
    DeviceRecoveryDetailPayload recovery;
    store->copy_recovery(recovery);
    const bool registry_invalid = recovery.reason == DeviceRecoveryReason::RegistryInvalid;
    CHECK(!registry_invalid || (recovery.flags & DeviceRecoveryLauncherActive) != 0);
    std::ofstream output(nor, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(flash.data()), flash.size());
    output.close();
    CHECK(output.good());
    std::printf("%u %u\n", store->registry_generation(), static_cast<unsigned>(registry_invalid));
}
