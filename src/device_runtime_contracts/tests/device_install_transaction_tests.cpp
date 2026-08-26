#include "device_runtime_contracts/device_install_transaction.h"

#include <cassert>
#include <cstdint>
#include <vector>

using namespace jellyframe;

namespace {

class TestStore final : public DeviceInstallStore {
public:
    bool begin_staging(const DeviceInstallRequest& request) override {
        ++begins;
        active = request.transaction_id;
        received.clear();
        return begin_ok;
    }

    bool write_staging(std::uint32_t offset, const std::uint8_t* bytes, std::size_t size) override {
        ++writes;
        if (!write_ok || active == 0 || offset != received.size()) {
            return false;
        }
        received.insert(received.end(), bytes, bytes + size);
        return true;
    }

    bool verify_staging(const DeviceInstallRequest&) override {
        ++verifies;
        return verify_ok;
    }

    bool commit_staging(const DeviceInstallRequest&) override {
        ++commits;
        if (!commit_ok) {
            return false;
        }
        active = 0;
        return true;
    }

    void abort_staging(std::uint32_t transaction_id) override {
        ++aborts;
        if (active == transaction_id) {
            active = 0;
        }
    }

    bool begin_ok = true;
    bool write_ok = true;
    bool verify_ok = true;
    bool commit_ok = true;
    std::uint32_t active = 0;
    std::size_t begins = 0;
    std::size_t writes = 0;
    std::size_t verifies = 0;
    std::size_t commits = 0;
    std::size_t aborts = 0;
    std::vector<std::uint8_t> received;
};

DeviceInstallTransaction make_transaction() {
    return DeviceInstallTransaction(DeviceInstallLimits{32, 8});
}

void ordered_transfer_commits_only_after_complete_verification() {
    TestStore store;
    DeviceInstallTransaction transaction = make_transaction();
    const std::uint8_t first[] = {1, 2, 3};
    const std::uint8_t second[] = {4, 5};

    assert(transaction.begin(7, "org.example.weather", 5, 0xaabbccdd, false, store).accepted());
    assert(transaction.append(7, 0, first, sizeof(first), store).accepted());
    assert(transaction.commit(7, store).status == DeviceInstallStatus::Incomplete);
    assert(transaction.append(7, 3, second, sizeof(second), store).accepted());
    const DeviceInstallResult committed = transaction.commit(7, store);
    assert(committed.accepted());
    assert(committed.received_bytes == 5);
    assert(transaction.phase() == DeviceInstallPhase::Idle);
    assert(store.verifies == 1);
    assert(store.commits == 1);
    assert(store.aborts == 0);
}

void rejects_replayed_or_out_of_order_chunks_without_writing() {
    TestStore store;
    DeviceInstallTransaction transaction = make_transaction();
    const std::uint8_t bytes[] = {1, 2, 3};

    assert(transaction.begin(4, "org.example.app", 6, 0, false, store).accepted());
    assert(transaction.append(4, 0, bytes, sizeof(bytes), store).accepted());
    assert(transaction.append(4, 0, bytes, sizeof(bytes), store).status == DeviceInstallStatus::OffsetMismatch);
    assert(transaction.append(4, 4, bytes, sizeof(bytes), store).status == DeviceInstallStatus::OffsetMismatch);
    assert(store.writes == 1);
    assert(transaction.received_bytes() == 3);
}

void integrity_and_commit_failure_discard_staging() {
    const std::uint8_t bytes[] = {1, 2, 3, 4};

    TestStore integrity_store;
    integrity_store.verify_ok = false;
    DeviceInstallTransaction integrity_transaction = make_transaction();
    assert(integrity_transaction.begin(11, "org.example.bad", 4, 0, false, integrity_store).accepted());
    assert(integrity_transaction.append(11, 0, bytes, sizeof(bytes), integrity_store).accepted());
    assert(integrity_transaction.commit(11, integrity_store).status == DeviceInstallStatus::IntegrityRejected);
    assert(integrity_transaction.phase() == DeviceInstallPhase::Idle);
    assert(integrity_store.aborts == 1);
    assert(integrity_store.commits == 0);

    TestStore commit_store;
    commit_store.commit_ok = false;
    DeviceInstallTransaction commit_transaction = make_transaction();
    assert(commit_transaction.begin(12, "org.example.commit", 4, 0, false, commit_store).accepted());
    assert(commit_transaction.append(12, 0, bytes, sizeof(bytes), commit_store).accepted());
    assert(commit_transaction.commit(12, commit_store).status == DeviceInstallStatus::CommitFailed);
    assert(commit_transaction.phase() == DeviceInstallPhase::Idle);
    assert(commit_store.aborts == 1);
    assert(commit_store.commits == 1);
}

void abort_and_limits_leave_no_active_transaction() {
    TestStore store;
    DeviceInstallTransaction transaction = make_transaction();
    const std::uint8_t oversized[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};

    assert(transaction.begin(0, "org.example.zero", 1, 0, false, store).status == DeviceInstallStatus::InvalidRequest);
    assert(transaction.begin(13, "org.example.large", 33, 0, false, store).status == DeviceInstallStatus::InvalidRequest);
    assert(transaction.begin(14, "org.example.abort", 9, 0, false, store).accepted());
    assert(transaction.append(14, 0, oversized, sizeof(oversized), store).status == DeviceInstallStatus::PayloadTooLarge);
    assert(transaction.abort(14, store).status == DeviceInstallStatus::Aborted);
    assert(transaction.phase() == DeviceInstallPhase::Idle);
    assert(store.aborts == 1);
    assert(transaction.append(14, 0, oversized, 1, store).status == DeviceInstallStatus::NoActiveTransaction);
}

void store_write_failure_discards_partial_staging() {
    TestStore store;
    store.write_ok = false;
    DeviceInstallTransaction transaction = make_transaction();
    const std::uint8_t byte = 1;

    assert(transaction.begin(15, "org.example.write-failure", 1, 0, false, store).accepted());
    assert(transaction.append(15, 0, &byte, 1, store).status == DeviceInstallStatus::StoreRejected);
    assert(transaction.phase() == DeviceInstallPhase::Idle);
    assert(transaction.received_bytes() == 0);
    assert(store.aborts == 1);
    assert(store.active == 0);
}

void store_begin_failure_discards_partial_staging() {
    TestStore store;
    store.begin_ok = false;
    DeviceInstallTransaction transaction = make_transaction();

    assert(transaction.begin(17, "org.example.begin-failure", 1, 0, false, store).status ==
           DeviceInstallStatus::StoreRejected);
    assert(transaction.phase() == DeviceInstallPhase::Idle);
    assert(store.begins == 1);
    assert(store.aborts == 1);
    assert(store.active == 0);
}

void rejects_embedded_nul_in_app_id() {
    TestStore store;
    DeviceInstallTransaction transaction = make_transaction();
    const std::string_view invalid_id("org.example\0hidden", 18);
    assert(transaction.begin(16, invalid_id, 1, 0, false, store).status == DeviceInstallStatus::InvalidRequest);
    assert(store.begins == 0);
}

void rejected_requests_report_the_active_transaction_snapshot() {
    TestStore store;
    DeviceInstallTransaction transaction = make_transaction();
    const std::uint8_t byte = 1;

    const DeviceInstallResult invalid = transaction.begin(19, "", 5, 0, false, store);
    assert(invalid.status == DeviceInstallStatus::InvalidRequest);
    assert(invalid.transaction_id == 0);
    assert(invalid.received_bytes == 0);
    assert(invalid.expected_bytes == 0);

    assert(transaction.begin(20, "org.example.active", 5, 0, false, store).accepted());
    const DeviceInstallResult busy = transaction.begin(21, "org.example.busy", 7, 0, false, store);
    assert(busy.status == DeviceInstallStatus::Busy);
    assert(busy.transaction_id == 20);
    assert(busy.received_bytes == 0);
    assert(busy.expected_bytes == 5);

    const DeviceInstallResult mismatched = transaction.append(21, 0, &byte, 1, store);
    assert(mismatched.status == DeviceInstallStatus::OffsetMismatch);
    assert(mismatched.transaction_id == 20);
    assert(mismatched.expected_bytes == 5);

    assert(transaction.abort(20, store).status == DeviceInstallStatus::Aborted);
    const DeviceInstallResult no_active = transaction.commit(20, store);
    assert(no_active.status == DeviceInstallStatus::NoActiveTransaction);
    assert(no_active.transaction_id == 0);
    assert(no_active.received_bytes == 0);
    assert(no_active.expected_bytes == 0);
}

} // namespace

int main() {
    ordered_transfer_commits_only_after_complete_verification();
    rejects_replayed_or_out_of_order_chunks_without_writing();
    integrity_and_commit_failure_discard_staging();
    abort_and_limits_leave_no_active_transaction();
    store_write_failure_discards_partial_staging();
    store_begin_failure_discards_partial_staging();
    rejects_embedded_nul_in_app_id();
    rejected_requests_report_the_active_transaction_snapshot();
    return 0;
}
