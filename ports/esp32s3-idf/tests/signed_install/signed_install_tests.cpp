#include "host_store_fixture.h"

int main(int argc, char** argv) {
    CHECK(argc == 3);
    const std::string mode = argv[1];
    const auto v1 = read(std::filesystem::path(argv[2]) / "v1.jfsg");
    const auto v2 = read(std::filesystem::path(argv[2]) / "v2.jfsg");
    if (mode == "capacity_small_partition") partition.size = 1024 * 1024;
    if (mode == "capacity_empty_partition") partition.size = 8192 + 4096;
    if (mode == "capacity_empty_partition") {
        DeviceImageStore empty(&partition);
        CHECK(empty.max_bundle_bytes() == 0);
        const auto before = flash;
        CHECK(!empty.initialize());
        CHECK(flash == before);
        return 0;
    }
    auto store = boot();
    if (mode == "production") {
        trust_test_key = false;
        auto request = stage(*store, v1);
        CHECK(!store->verify_staging(request));
        CHECK(!store->commit_staging(request));
        assert_version(*store, 0);
    } else {
        install(*store, v1);
        assert_lease(*store, v1, 1);
        if (mode == "capacity" || mode == "capacity_small_partition") {
            const auto v3 = read(std::filesystem::path(argv[2]) / "v3.jfsg");
            CHECK(v3.size() > 900 * 1024 && v3.size() < 1024 * 1024);
            const auto slot_bytes = ((partition.size - 8192) / 3) & ~std::uint32_t(4095);
            CHECK(store->max_bundle_bytes() == std::min(DeviceImageStore::kMaxBundleBytes, slot_bytes));
            DeviceInstallRequest request;
            request.transaction_id = 77;
            std::strcpy(request.app_id.data(), app_id);
            request.bundle_bytes = store->max_bundle_bytes() + 1;
            const auto before = flash;
            const auto generation = store->registry_generation();
            CHECK(!store->begin_staging(request));
            DeviceInstallTransaction bounded({store->max_bundle_bytes(), 4096});
            CHECK(bounded.begin(77, app_id, request.bundle_bytes, 0, false, *store).status ==
                  DeviceInstallStatus::InvalidRequest);
            CHECK(bounded.phase() == DeviceInstallPhase::Idle);
            CHECK(flash == before && store->registry_generation() == generation);
            request.bundle_bytes = 17;
            CHECK(store->begin_staging(request));
            const auto staged = flash;
            const std::uint8_t byte = 0;
            CHECK(!store->write_staging(17, &byte, 1));
            CHECK(!store->write_staging(UINT32_MAX, &byte, 1));
            CHECK(!store->write_staging(16, &byte, 2));
            CHECK(flash == staged);
            store->abort_staging(request.transaction_id);
            request.bundle_bytes = store->max_bundle_bytes();
            CHECK(store->begin_staging(request));
            CHECK(store->write_staging(request.bundle_bytes - 1, &byte, 1));
            CHECK(!store->write_staging(request.bundle_bytes, &byte, 1));
            store->abort_staging(request.transaction_id);
            request.bundle_bytes = static_cast<std::uint32_t>(v3.size());
            if (v3.size() > store->max_bundle_bytes()) {
                // Aborting a valid staging request may erase its unused slot;
                // the rejected oversized request itself must not alter flash.
                const auto rejected = flash;
                CHECK(!store->begin_staging(request));
                CHECK(flash == rejected);
                assert_lease(*store, v1, 1);
            } else {
                install(*store, v3); assert_lease(*store, v3, 3);
                store = boot(); assert_lease(*store, v3, 3);
                CHECK(store->rollback(app_id) == DeviceRequestResultCode::Ok);
                assert_lease(*store, v1, 1);
            }
        } else if (mode == "valid") {
            install(*store, v2); assert_lease(*store, v2, 2);
        } else if (mode == "transaction") {
            DeviceInstallTransaction transaction({DeviceImageStore::kMaxBundleBytes, 97});
            CHECK(transaction.begin(42, app_id, static_cast<std::uint32_t>(v2.size()),
                esp_rom_crc32_le(0, v2.data(), v2.size()), false, *store).accepted());
            CHECK(transaction.commit(42, *store).status == DeviceInstallStatus::Incomplete);
            assert_version(*store, 1);
            for (std::size_t offset = 0; offset < v2.size(); offset += 97) {
                CHECK(transaction.append(42, static_cast<std::uint32_t>(offset), v2.data() + offset,
                    std::min<std::size_t>(97, v2.size() - offset), *store).accepted());
            }
            CHECK(transaction.commit(42, *store).accepted());
            CHECK(transaction.phase() == DeviceInstallPhase::Idle);
            assert_lease(*store, v2, 2);
        } else if (mode == "reboot") {
            install(*store, v2);
            auto before = crypto_calls;
            store = boot(); CHECK(crypto_calls > before);
            assert_version(*store, 2); assert_lease(*store, v2, 2);
        } else if (mode == "tamper") {
            // Recomputed transport CRC cannot authenticate a changed signature.
            auto changed = v2;
            const auto manifest_size = std::uint32_t(changed[16]) | (std::uint32_t(changed[17]) << 8) |
                (std::uint32_t(changed[18]) << 16) | (std::uint32_t(changed[19]) << 24);
            changed[64 + manifest_size] ^= 1;
            auto request = stage(*store, changed);
            const auto before = crypto_calls;
            CHECK(!store->verify_staging(request)); CHECK(!store->commit_staging(request));
            CHECK(crypto_calls == before + 1);
            store->abort_staging(request.transaction_id);
            assert_lease(*store, v1, 1);
            install(*store, v2);
            const std::size_t slot_bytes = ((partition.size - 8192) / 3) & ~std::size_t(4095);
            flash[8192 + slot_bytes + 64] ^= 1; // v2 occupies the next physical slot.
            store = boot(); assert_version(*store, 1); assert_lease(*store, v1, 1);
        } else if (mode == "boot_signature") {
            install(*store, v2);
            const std::size_t slot_bytes = ((partition.size - 8192) / 3) & ~std::size_t(4095);
            const auto envelope_offset = 8192 + slot_bytes;
            const auto manifest_size = std::uint32_t(v2[16]) | (std::uint32_t(v2[17]) << 8) |
                (std::uint32_t(v2[18]) << 16) | (std::uint32_t(v2[19]) << 24);
            const auto old_crc = esp_rom_crc32_le(0, v2.data(), v2.size());
            flash[envelope_offset + 64 + manifest_size] ^= 1;
            const auto new_crc = esp_rom_crc32_le(0, flash.data() + envelope_offset, v2.size());
            unsigned replaced = 0;
            for (std::size_t sector : {std::size_t(0), std::size_t(4096)}) {
                const std::size_t bytes = flash[sector+6] | (std::size_t(flash[sector+7]) << 8);
                CHECK(bytes >= 16 && bytes < 4096);
                bool changed = false;
                for (std::size_t offset = sector + 8; offset + 4 <= sector + bytes - 4; offset += 4) {
                    std::uint32_t value = 0;
                    std::memcpy(&value, flash.data() + offset, 4);
                    if (value == old_crc) {
                        std::memcpy(flash.data() + offset, &new_crc, 4);
                        ++replaced; changed = true;
                    }
                }
                if (changed) {
                    std::fill_n(flash.data() + sector + bytes - 4, 4, 0);
                    const auto crc = esp_rom_crc32_le(0, flash.data() + sector, bytes);
                    std::memcpy(flash.data() + sector + bytes - 4, &crc, 4);
                }
            }
            CHECK(replaced == 1);
            const auto before = crypto_calls;
            store = boot(); CHECK(crypto_calls >= before + 2);
            assert_version(*store, 1); assert_lease(*store, v1, 1);
        } else if (mode == "interrupted") {
            const auto request = stage(*store, v2);
            CHECK(store->verify_staging(request));
            // Reconstruct from durable bytes without committing the verified candidate.
            store = boot(); assert_version(*store, 1); assert_lease(*store, v1, 1);
        } else if (mode == "rollback") {
            install(*store, v2);
            CHECK(store->rollback(app_id) == DeviceRequestResultCode::Ok);
            assert_version(*store, 1); assert_lease(*store, v1, 1);
        } else if (mode == "wrong_key") {
            wrong_test_key = true;
            auto request = stage(*store, v2);
            CHECK(!store->verify_staging(request)); CHECK(!store->commit_staging(request));
            wrong_test_key = false; store->abort_staging(request.transaction_id);
            assert_lease(*store, v1, 1);
        } else if (mode == "allocation") {
            auto request = stage(*store, v2);
            refuse_allocation = true;
            CHECK(!store->verify_staging(request)); CHECK(!store->commit_staging(request));
            refuse_allocation = false; store->abort_staging(request.transaction_id);
            assert_lease(*store, v1, 1);
        } else CHECK(false);
    }
    std::printf("%s: %u checks, %u actual verifier calls\n", mode.c_str(), checks, crypto_calls);
}
