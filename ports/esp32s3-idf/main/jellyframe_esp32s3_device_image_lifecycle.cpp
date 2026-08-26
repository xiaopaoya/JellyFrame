#include "jellyframe_esp32s3_device_image_store.h"
#include "jellyframe_esp32s3_jfdp_transport.h"
#include "jellyframe_esp32s3_ui_task.h"

#include "app_runtime/app_installed_bundle.h"
#include "device_runtime_contracts/device_install_transaction.h"
#include "render_core/html_parser.h"

#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace jellyframe_esp32s3 {
namespace {

using namespace jellyframe;

constexpr std::size_t kUsbReadBytes = 256;
constexpr std::uint32_t kUsbBufferBytes = 1024;
constexpr TickType_t kUsbIoTimeout = pdMS_TO_TICKS(50);
// JFDP control requests have a three-second host deadline. USB CDC can
// briefly backpressure a near-4 KiB logs frame, so leave a bounded margin for
// receiving and correlation while still completing a whole device response.
constexpr std::int64_t kUsbWriteDeadlineUs = 2500000;
constexpr std::int64_t kPartialFrameTimeoutUs = 500000;
constexpr std::size_t kInstalledAppEntryMaxBytes = 16u * 1024u;
constexpr std::size_t kInstalledResourceMaxBytes = 48u * 1024u;
constexpr std::size_t kInstalledResourceSnapshotMaxBytes = 96u * 1024u;
constexpr std::size_t kInstalledResourceSnapshotMaxEntries = 16u;

constexpr char kImageId[] = "org.jellyframe.ws147.developer";
constexpr char kProfileId[] = "rect-172x320";
constexpr char kImageVersion[] = "0.6.0-a2";
#ifndef JELLYFRAME_ESP32S3_SOURCE_REVISION
#define JELLYFRAME_ESP32S3_SOURCE_REVISION "0000000000000000000000000000000000000000"
#endif
#ifndef JELLYFRAME_ESP32S3_RENDER_CORE_VERSION
#define JELLYFRAME_ESP32S3_RENDER_CORE_VERSION "0.6.1"
#endif
#ifndef JELLYFRAME_ESP32S3_RENDER_CORE_ABI
#define JELLYFRAME_ESP32S3_RENDER_CORE_ABI 1
#endif

jellyframe::HostResourceKind resource_kind_for_path(std::string_view path) {
    const std::size_t dot = path.rfind('.');
    if (dot == std::string_view::npos) return jellyframe::HostResourceKind::Other;
    const std::string_view suffix = path.substr(dot);
    if (suffix == ".css") return jellyframe::HostResourceKind::Stylesheet;
    if (suffix == ".js") return jellyframe::HostResourceKind::ClassicScript;
    if (suffix == ".bmp" || suffix == ".png" || suffix == ".jpg" || suffix == ".jpeg" ||
        suffix == ".gif" || suffix == ".webp") return jellyframe::HostResourceKind::Image;
    return jellyframe::HostResourceKind::Other;
}

void collect_resource_references(const jellyframe::Node& node,
                                 std::vector<std::pair<jellyframe::HostResourceKind, std::string>>& output) {
    const std::string& src = node.attribute("src");
    const std::string& href = node.attribute("href");
    if (node.tag_name == "link" && node.attribute("rel") == "stylesheet" && !href.empty()) {
        output.emplace_back(jellyframe::HostResourceKind::Stylesheet, href);
    } else if (node.tag_name == "script" && !src.empty()) {
        output.emplace_back(jellyframe::HostResourceKind::ClassicScript, src);
    } else if ((node.tag_name == "img" || node.tag_name == "source") && !src.empty()) {
        output.emplace_back(jellyframe::HostResourceKind::Image, src);
    }
    for (const auto& child : node.children) {
        collect_resource_references(*child, output);
    }
}

bool snapshot_active_resources(const jellyframe::AppInstalledBundleBinding& binding,
                               const jellyframe::DeviceBundleDescriptor& descriptor,
                               std::string_view entry_document,
                               InstalledResourceSnapshot& snapshot) {
    snapshot.clear();
    jellyframe::HtmlParser parser;
    const std::unique_ptr<jellyframe::Node> document = parser.parse(
        std::string(entry_document), jellyframe::HtmlParserOptions{});
    if (!document) {
        return false;
    }
    std::vector<std::pair<jellyframe::HostResourceKind, std::string>> references;
    collect_resource_references(*document, references);
    for (const auto& reference : references) {
        std::string path;
        if (!resolve_local_resource_url(reference.second, descriptor.summary.entry_path_view(), path)) {
            return false;
        }
        bool already_loaded = false;
        for (const InstalledResourceSnapshotEntry& entry : snapshot.entries) {
            if (entry.url == path) {
                already_loaded = true;
                break;
            }
        }
        if (already_loaded) continue;
        if (snapshot.entries.size() >= kInstalledResourceSnapshotMaxEntries) {
            return false;
        }
        std::vector<std::uint8_t> bytes(kInstalledResourceMaxBytes);
        std::size_t read_bytes = 0;
        if (binding.read_active_resource(path, bytes.data(), bytes.size(), read_bytes) != DeviceBundleStatus::Ok ||
            read_bytes == 0 || read_bytes > kInstalledResourceMaxBytes ||
            snapshot.total_bytes > kInstalledResourceSnapshotMaxBytes - read_bytes) {
            return false;
        }
        bytes.resize(read_bytes);
        const jellyframe::HostResourceKind path_kind = resource_kind_for_path(path);
        if (path_kind != reference.first) {
            return false;
        }
        snapshot.total_bytes += read_bytes;
        snapshot.entries.push_back(InstalledResourceSnapshotEntry{std::move(path), path_kind, std::move(bytes)});
    }
    return snapshot.rebuild_views();
}

DeviceRequestResultCode result_code(DeviceInstallStatus status) {
    switch (status) {
    case DeviceInstallStatus::Accepted: return DeviceRequestResultCode::Accepted;
    case DeviceInstallStatus::Busy: return DeviceRequestResultCode::Busy;
    case DeviceInstallStatus::InvalidRequest:
    case DeviceInstallStatus::OffsetMismatch: return DeviceRequestResultCode::InvalidRequest;
    case DeviceInstallStatus::PayloadTooLarge: return DeviceRequestResultCode::PayloadTooLarge;
    case DeviceInstallStatus::Incomplete: return DeviceRequestResultCode::Accepted;
    case DeviceInstallStatus::StoreRejected: return DeviceRequestResultCode::StorageFull;
    case DeviceInstallStatus::IntegrityRejected: return DeviceRequestResultCode::IntegrityFailed;
    case DeviceInstallStatus::CommitFailed: return DeviceRequestResultCode::Failed;
    case DeviceInstallStatus::Aborted: return DeviceRequestResultCode::Cancelled;
    case DeviceInstallStatus::NoActiveTransaction: return DeviceRequestResultCode::NotFound;
    }
    return DeviceRequestResultCode::Failed;
}

class ProtectedLauncher final : public AppProtectedLauncher {
public:
    bool launch_protected_launcher(AppRuntimeHost& host, AppTeardownReason) override {
        return host.launch("protected-launcher", AppRole::Launcher).active();
    }
};

class DeviceImageEndpoint final : public JfdpStreamSink {
public:
    explicit DeviceImageEndpoint(DeviceImageStore& store)
        : store_(store), transaction_(DeviceInstallLimits{DeviceImageStore::kMaxBundleBytes, 4096}),
          host_(AppRuntimeHostOptions{2, 2, 4, 4096, 1}), binding_(store_, &launcher_) {}

    void on_jfdp_frame(const std::uint8_t* frame, std::size_t frame_size) override {
        DeviceFrameHeader request;
        const std::uint8_t* payload = nullptr;
        if (decode_device_frame(frame, frame_size, request, payload) != DeviceProtocolStatus::Ok || request.flags != 0) {
            ++counters_.rejected_frames;
            abort_active_transaction();
            return;
        }
        ++counters_.dispatched_frames;
        switch (request.type) {
        case DeviceMessageType::Discovery: send_capabilities(request); return;
        case DeviceMessageType::AppList: send_app_list(request); return;
        case DeviceMessageType::Recovery: send_recovery(request); return;
        case DeviceMessageType::Identity: send_identity(request); return;
        case DeviceMessageType::Logs: send_logs(request, payload, request.payload_length); return;
        default: break;
        }

        DeviceOperationResultPayload result{};
        result.result_code = DeviceRequestResultCode::Unsupported;
        switch (request.type) {
        case DeviceMessageType::InstallBegin: handle_begin(payload, request.payload_length, result); break;
        case DeviceMessageType::InstallChunk: handle_chunk(payload, request.payload_length, result); break;
        case DeviceMessageType::InstallCommit: handle_commit(payload, request.payload_length, result); break;
        case DeviceMessageType::InstallAbort: handle_abort(payload, request.payload_length, result); break;
        case DeviceMessageType::Launch: handle_launch(payload, request.payload_length, result); break;
        case DeviceMessageType::Stop: handle_stop(payload, request.payload_length, result); break;
        case DeviceMessageType::Remove: handle_remove(payload, request.payload_length, result); break;
        case DeviceMessageType::Rollback: handle_rollback(payload, request.payload_length, result); break;
        default: break;
        }
        const ResponseWrite response = send_result(request, result);
        if (request.type == DeviceMessageType::InstallCommit) {
            record_commit_telemetry(response);
        }
    }

    void poll_lifecycle() {
        poll_active_session();
    }

    void on_jfdp_transport_reset() override { abort_active_transaction(); }
    JfdpTransportCounters& counters() { return counters_; }

private:
    static constexpr std::size_t kAppLogCapacity = 32;
    // The protocol permits up to eleven records, but USB Serial/JTAG has a
    // small TX FIFO. Keep one response comfortably below that FIFO's burst
    // pressure and report omitted matching records through dropped_records.
    static constexpr std::size_t kAppLogResponseMaxEntries = 2;
    static constexpr std::uint32_t kDeviceImageTaskStackBytes = 24576;

    struct ResponseWrite {
        std::uint32_t elapsed_us = 0;
        bool ok = false;
    };

    struct CommitTelemetry {
        bool pending = false;
        std::array<char, kDeviceMaxAppIdBytes + 1> app_id{};
        DeviceRequestResultCode result_code = DeviceRequestResultCode::Failed;
        DeviceImageStore::VerifyTelemetry verify{};
    };

    void record_app_log(DeviceAppLogLevel level, std::string_view app_id, std::uint32_t generation,
                        std::string_view message) {
        if (app_id.empty() || app_id.size() > kDeviceMaxAppIdBytes || message.size() > kDeviceAppLogMaxMessageBytes) {
            return;
        }
        if (app_log_count_ == app_logs_.size()) {
            for (std::size_t index = 1; index < app_logs_.size(); ++index) app_logs_[index - 1] = app_logs_[index];
            --app_log_count_;
            ++app_log_overwrites_;
        }
        DeviceAppLogEntry& entry = app_logs_[app_log_count_++];
        entry = {};
        std::memcpy(entry.app_id.data(), app_id.data(), app_id.size());
        std::memcpy(entry.message.data(), message.data(), message.size());
        entry.generation = generation;
        entry.timestamp_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
        entry.level = level;
    }

    bool stop_active_ui() {
        if (!stop_installed_bundle_ui_task(ui_session_)) {
            ESP_LOGE("JellyFrameDevice", "installed app UI did not stop before lifecycle transition");
            return false;
        }
        if (!stop_installed_bundle_script_task(script_session_)) {
            ESP_LOGE("JellyFrameDevice", "installed script app did not stop before lifecycle transition");
            return false;
        }
        return true;
    }

    void poll_active_session() {
        if (!installed_bundle_script_task_has_fatal(script_session_)) {
            return;
        }
        const std::string app_id = active_app_id_;
        if (app_id.empty()) {
            return;
        }
        ESP_LOGE("JellyFrameDevice", "installed script app fatal app=%s generation=%u", app_id.c_str(),
                 static_cast<unsigned>(store_.registry_generation()));
        record_app_log(DeviceAppLogLevel::Error, app_id, store_.registry_generation(), "script worker fatal");
        (void)recover_to_launcher(DeviceRecoveryReason::AppRuntimeFailure, app_id);
    }

    bool recover_to_launcher(DeviceRecoveryReason reason, std::string_view app_id) {
        if (!stop_active_ui()) {
            return false;
        }
        store_.record_recovery(reason, app_id,
                               DeviceRecoveryLauncherActive | DeviceRecoveryAppDisabled);
        const bool launcher_started =
            binding_.recover_to_protected_launcher(host_, AppTeardownReason::LoadFailure).launcher_started;
        active_app_id_.clear();
        return launcher_started;
    }

    void abort_active_transaction() {
        if (transaction_.phase() != DeviceInstallPhase::Idle) {
            (void)transaction_.abort(transaction_.request().transaction_id, store_);
        }
    }

    void fill_install_result(const DeviceInstallResult& install,
                             DeviceOperationResultPayload& result,
                             bool complete) {
        result.result_code = result_code(install.status);
        result.transaction_id = install.transaction_id;
        result.received_bytes = install.received_bytes;
        result.expected_bytes = install.expected_bytes;
        if (complete) {
            result.flags |= DeviceOperationResultComplete;
        }
        if (install.accepted() && complete) {
            ++counters_.registry_publications;
        }
    }

    void handle_begin(const std::uint8_t* bytes, std::size_t size, DeviceOperationResultPayload& result) {
        DeviceInstallBeginPayload begin;
        if (decode_device_install_begin_payload(bytes, size, begin) != DeviceProtocolStatus::Ok) {
            result.result_code = DeviceRequestResultCode::InvalidRequest;
            return;
        }
        ++counters_.staging_begins;
        fill_install_result(transaction_.begin(begin.transaction_id, begin.app_id_view(), begin.bundle_bytes,
                                               begin.bundle_crc32, begin.allow_downgrade, store_), result, false);
    }

    void handle_chunk(const std::uint8_t* bytes, std::size_t size, DeviceOperationResultPayload& result) {
        DeviceInstallChunkView chunk;
        if (decode_device_install_chunk_payload(bytes, size, chunk) != DeviceProtocolStatus::Ok) {
            result.result_code = DeviceRequestResultCode::InvalidRequest;
            return;
        }
        const DeviceInstallResult install = transaction_.append(chunk.transaction_id, chunk.offset, chunk.bytes,
                                                                  chunk.byte_count, store_);
        if (install.accepted()) {
            ++counters_.staging_writes;
        }
        fill_install_result(install, result, install.accepted() && install.received_bytes == install.expected_bytes);
    }

    void handle_commit(const std::uint8_t* bytes, std::size_t size, DeviceOperationResultPayload& result) {
        DeviceTransactionPayload commit;
        if (decode_device_transaction_payload(bytes, size, commit) != DeviceProtocolStatus::Ok) {
            result.result_code = DeviceRequestResultCode::InvalidRequest;
            return;
        }
        std::array<char, kDeviceMaxAppIdBytes + 1> app_id{};
        const std::string_view active_app_id = transaction_.request().app_id_view();
        if (!active_app_id.empty()) {
            std::memcpy(app_id.data(), active_app_id.data(), active_app_id.size());
        }
        const DeviceInstallResult install = transaction_.commit(commit.transaction_id, store_);
        fill_install_result(install, result, install.accepted());
        if (!active_app_id.empty()) {
            commit_telemetry_ = {};
            commit_telemetry_.app_id = app_id;
            commit_telemetry_.pending = true;
            commit_telemetry_.result_code = result.result_code;
            commit_telemetry_.verify = store_.copy_verify_telemetry();
        }
    }

    void handle_abort(const std::uint8_t* bytes, std::size_t size, DeviceOperationResultPayload& result) {
        DeviceTransactionPayload abort;
        if (decode_device_transaction_payload(bytes, size, abort) != DeviceProtocolStatus::Ok) {
            result.result_code = DeviceRequestResultCode::InvalidRequest;
            return;
        }
        ++counters_.staging_aborts;
        fill_install_result(transaction_.abort(abort.transaction_id, store_), result, true);
    }

    bool decode_app_id(const std::uint8_t* bytes, std::size_t size, DeviceAppIdPayload& app_id,
                       DeviceOperationResultPayload& result) {
        if (decode_device_app_id_payload(bytes, size, app_id) != DeviceProtocolStatus::Ok) {
            result.result_code = DeviceRequestResultCode::InvalidRequest;
            return false;
        }
        return true;
    }

    void handle_launch(const std::uint8_t* bytes, std::size_t size, DeviceOperationResultPayload& result) {
        DeviceAppIdPayload app_id;
        if (!decode_app_id(bytes, size, app_id, result)) {
            return;
        }
        // UI presentation and input must be stopped before the Runtime lease
        // changes. This prevents a previous generation from presenting after
        // the new AppRuntimeHost instance becomes current.
        if (!stop_active_ui()) {
            result.result_code = DeviceRequestResultCode::Failed;
            result.flags = DeviceOperationResultLauncherActive;
            return;
        }
        const AppInstalledBundleLaunchResult launch = binding_.launch(host_, app_id.app_id_view());
        if (!launch.launched()) {
            (void)recover_to_launcher(DeviceRecoveryReason::AppLoadFailure, app_id.app_id_view());
            result.result_code = DeviceRequestResultCode::Failed;
            result.flags = DeviceOperationResultLauncherActive;
            return;
        }
        std::vector<std::uint8_t> entry_bytes(kInstalledAppEntryMaxBytes);
        std::size_t read_bytes = 0;
        DeviceBundleDescriptor descriptor;
        const DeviceBundleStatus entry_status = binding_.copy_active_descriptor(descriptor)
            ? binding_.read_active_resource(descriptor.summary.entry_path_view(), entry_bytes.data(),
                                            entry_bytes.size(), read_bytes)
            : DeviceBundleStatus::ResourceNotFound;
        if (entry_status != DeviceBundleStatus::Ok || read_bytes == 0) {
            (void)recover_to_launcher(DeviceRecoveryReason::AppLoadFailure, app_id.app_id_view());
            result.result_code = DeviceRequestResultCode::Failed;
            result.flags = DeviceOperationResultLauncherActive;
            return;
        }
        std::string entry_document(reinterpret_cast<const char*>(entry_bytes.data()), read_bytes);
        InstalledResourceSnapshot resources;
        if (!snapshot_active_resources(binding_, descriptor, entry_document, resources)) {
            (void)recover_to_launcher(DeviceRecoveryReason::AppLoadFailure, app_id.app_id_view());
            result.result_code = DeviceRequestResultCode::Failed;
            result.flags = DeviceOperationResultLauncherActive;
            return;
        }
        const bool script_app = descriptor.summary.script_mode == DeviceBundleScriptMode::Classic;
        const bool task_started = script_app
            ? start_installed_bundle_script_task(std::string(app_id.app_id_view()), store_.registry_generation(),
                                                 launch.instance.id, std::string(descriptor.summary.entry_path_view()),
                                                 std::move(entry_document), std::move(resources), host_, script_session_)
            : start_installed_bundle_ui_task(std::string(app_id.app_id_view()), store_.registry_generation(),
                                             std::string(descriptor.summary.entry_path_view()), std::move(entry_document),
                                             std::move(resources), ui_session_);
        if (!task_started) {
            (void)recover_to_launcher(DeviceRecoveryReason::AppLoadFailure, app_id.app_id_view());
            result.result_code = DeviceRequestResultCode::Failed;
            result.flags = DeviceOperationResultLauncherActive;
            return;
        }
        active_app_id_.assign(app_id.app_id_view());
        ESP_LOGI("JellyFrameDevice", "installed_app launch app=%s generation=%u entry_bytes=%u script=%d",
                 std::string(app_id.app_id_view()).c_str(),
                 static_cast<unsigned>(store_.registry_generation()),
                 static_cast<unsigned>(read_bytes), script_app ? 1 : 0);
        record_app_log(DeviceAppLogLevel::Info, app_id.app_id_view(), store_.registry_generation(),
                       script_app ? "installed script bundle launched" : "installed bundle launched");
        result.result_code = DeviceRequestResultCode::Ok;
        result.flags = DeviceOperationResultComplete | DeviceOperationResultActive;
    }

    void handle_stop(const std::uint8_t* bytes, std::size_t size, DeviceOperationResultPayload& result) {
        DeviceAppIdPayload app_id;
        if (!decode_app_id(bytes, size, app_id, result)) {
            return;
        }
        if (!binding_.has_active_bundle() || active_app_id_ != app_id.app_id_view()) {
            result.result_code = DeviceRequestResultCode::NotFound;
            return;
        }
        if (!stop_active_ui()) {
            result.result_code = DeviceRequestResultCode::Failed;
            return;
        }
        (void)binding_.terminate_current(host_, AppTeardownReason::NormalExit);
        active_app_id_.clear();
        record_app_log(DeviceAppLogLevel::Info, app_id.app_id_view(), store_.registry_generation(),
                       "installed bundle stopped");
        result.result_code = DeviceRequestResultCode::Ok;
        result.flags = DeviceOperationResultComplete;
    }

    void handle_remove(const std::uint8_t* bytes, std::size_t size, DeviceOperationResultPayload& result) {
        DeviceAppIdPayload app_id;
        if (!decode_app_id(bytes, size, app_id, result)) {
            return;
        }
        if (binding_.has_active_bundle() && active_app_id_ == app_id.app_id_view()) {
            if (!stop_active_ui()) {
                result.result_code = DeviceRequestResultCode::Failed;
                return;
            }
            (void)binding_.terminate_current(host_, AppTeardownReason::SystemPolicy);
            active_app_id_.clear();
        }
        result.result_code = store_.remove(app_id.app_id_view()) ? DeviceRequestResultCode::Ok : DeviceRequestResultCode::NotFound;
        result.flags = result.result_code == DeviceRequestResultCode::Ok ? DeviceOperationResultComplete : 0;
    }

    void handle_rollback(const std::uint8_t* bytes, std::size_t size, DeviceOperationResultPayload& result) {
        DeviceAppIdPayload app_id;
        if (!decode_app_id(bytes, size, app_id, result)) {
            return;
        }
        if (binding_.has_active_bundle() && active_app_id_ == app_id.app_id_view()) {
            if (!stop_active_ui()) {
                result.result_code = DeviceRequestResultCode::Failed;
                return;
            }
            (void)binding_.terminate_current(host_, AppTeardownReason::AppSwitch);
            active_app_id_.clear();
        }
        result.result_code = store_.rollback(app_id.app_id_view()) ? DeviceRequestResultCode::Ok : DeviceRequestResultCode::NotFound;
        result.flags = result.result_code == DeviceRequestResultCode::Ok ? DeviceOperationResultComplete : 0;
    }

    void send_capabilities(const DeviceFrameHeader& request) {
        DeviceCapabilitySnapshot capabilities{};
        capabilities.display_width = 172;
        capabilities.display_height = 320;
        capabilities.capability_bits = DeviceCapabilityTouch | DeviceCapabilityDeviceLogs;
        capabilities.max_bundle_bytes = DeviceImageStore::kMaxBundleBytes;
        capabilities.available_storage_bytes = store_.available_storage_bytes();
        // This is the release board identity, not a board-driver label. The
        // explicit host provider compares it with the Developer Image record.
        std::memcpy(capabilities.board_id, "ws147", 6);
        std::memcpy(capabilities.runtime_version, "0.6.0-dev", 10);
        std::array<std::uint8_t, 128> payload{};
        std::size_t size = 0;
        if (encode_device_capabilities(capabilities, payload.data(), payload.size(), size) == DeviceProtocolStatus::Ok) {
            send_payload(request, payload.data(), size);
        }
    }

    void send_identity(const DeviceFrameHeader& request) {
        DeviceImageIdentityPayload identity{};
        std::memcpy(identity.image_id.data(), kImageId, sizeof(kImageId));
        std::memcpy(identity.profile_id.data(), kProfileId, sizeof(kProfileId));
        std::memcpy(identity.image_version.data(), kImageVersion, sizeof(kImageVersion));
        std::memcpy(identity.render_core_version.data(), JELLYFRAME_ESP32S3_RENDER_CORE_VERSION,
                    sizeof(JELLYFRAME_ESP32S3_RENDER_CORE_VERSION));
        std::memcpy(identity.source_revision.data(), JELLYFRAME_ESP32S3_SOURCE_REVISION,
                    sizeof(JELLYFRAME_ESP32S3_SOURCE_REVISION));
        identity.render_core_abi = JELLYFRAME_ESP32S3_RENDER_CORE_ABI;
        identity.feature_family_bits = DeviceRenderCoreFeatureDocument | DeviceRenderCoreFeaturePaint;
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
        identity.feature_family_bits |= DeviceRenderCoreFeatureFlexGrid;
#endif
#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
        identity.feature_family_bits |= DeviceRenderCoreFeatureModernPaint;
#endif
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
        identity.feature_family_bits |= DeviceRenderCoreFeatureAdvancedForms;
#endif
#if JELLYFRAME_RENDER_CORE_CANVAS2D_ENABLED
        identity.feature_family_bits |= DeviceRenderCoreFeatureCanvas2d;
#endif
        std::array<std::uint8_t, 384> payload{};
        std::size_t size = 0;
        if (encode_device_image_identity_payload(identity, payload.data(), payload.size(), size) == DeviceProtocolStatus::Ok) {
            send_payload(request, payload.data(), size);
        }
    }

    void send_logs(const DeviceFrameHeader& request, const std::uint8_t* bytes, std::size_t size) {
        DeviceLogsRequestPayload logs_request{};
        if (decode_device_logs_request_payload(bytes, size, logs_request) != DeviceProtocolStatus::Ok ||
            logs_request.limit == 0 || logs_request.limit > kDeviceAppLogMaxEntries) {
            DeviceOperationResultPayload result{};
            result.result_code = DeviceRequestResultCode::InvalidRequest;
            send_result(request, result);
            return;
        }
        DeviceAppListPayload installed{};
        if (!store_.list(installed)) {
            DeviceOperationResultPayload result{};
            result.result_code = DeviceRequestResultCode::Failed;
            send_result(request, result);
            return;
        }
        bool app_exists = false;
        for (std::size_t index = 0; index < installed.entry_count; ++index) {
            if (installed.entries[index].app_id_view() == logs_request.app_id_view()) {
                app_exists = true;
                break;
            }
        }
        if (!app_exists) {
            DeviceOperationResultPayload result{};
            result.result_code = DeviceRequestResultCode::NotFound;
            send_result(request, result);
            return;
        }
        DeviceAppLogsPayload logs{};
        std::size_t matches = 0;
        for (std::size_t index = 0; index < app_log_count_; ++index) {
            if (app_logs_[index].app_id_view() == logs_request.app_id_view()) ++matches;
        }
        const std::size_t returned = std::min<std::size_t>(matches,
                                                           std::min<std::size_t>(logs_request.limit,
                                                                                 kAppLogResponseMaxEntries));
        logs.dropped_records = static_cast<std::uint32_t>(matches - returned + app_log_overwrites_);
        const std::size_t first = matches - returned;
        std::size_t seen = 0;
        for (std::size_t index = 0; index < app_log_count_; ++index) {
            if (app_logs_[index].app_id_view() != logs_request.app_id_view()) continue;
            if (seen++ >= first) logs.entries[logs.entry_count++] = app_logs_[index];
        }
        std::array<std::uint8_t, kDeviceProtocolMaxPayloadBytes> payload{};
        std::size_t payload_size = 0;
        if (encode_device_app_logs_payload(logs, payload.data(), payload.size(), payload_size) == DeviceProtocolStatus::Ok) {
            send_payload(request, payload.data(), payload_size);
        }
    }

    void send_app_list(const DeviceFrameHeader& request) {
        DeviceAppListPayload list{};
        if (!store_.list(list)) {
            return;
        }
        // The WS147 raw store exposes one active app record in this first
        // image. Keep list encoding bounded to that typed record instead of
        // paying a second full JFDP frame-sized stack scratch.
        std::array<std::uint8_t, 256> payload{};
        std::size_t size = 0;
        if (encode_device_app_list_payload(list, payload.data(), payload.size(), size) == DeviceProtocolStatus::Ok) {
            send_payload(request, payload.data(), size);
        }
    }

    void send_recovery(const DeviceFrameHeader& request) {
        DeviceRecoveryDetailPayload recovery{};
        store_.copy_recovery(recovery);
        std::array<std::uint8_t, 128> payload{};
        std::size_t size = 0;
        if (encode_device_recovery_detail_payload(recovery, payload.data(), payload.size(), size) == DeviceProtocolStatus::Ok) {
            send_payload(request, payload.data(), size);
        }
    }

    ResponseWrite send_result(const DeviceFrameHeader& request, const DeviceOperationResultPayload& result) {
        std::array<std::uint8_t, 32> payload{};
        std::size_t size = 0;
        if (encode_device_operation_result_payload(result, payload.data(), payload.size(), size) == DeviceProtocolStatus::Ok) {
            return send_payload(request, payload.data(), size);
        }
        ++counters_.response_write_failures;
        return {};
    }

    ResponseWrite send_payload(const DeviceFrameHeader& request, const std::uint8_t* payload, std::size_t payload_size) {
        DeviceFrameHeader response{};
        response.type = request.type;
        response.flags = kDeviceFrameFlagResponse;
        response.session_id = request.session_id;
        response.request_id = request.request_id;
        std::size_t frame_size = 0;
        if (encode_device_frame(response, payload, payload_size, response_frame_.data(), response_frame_.size(), frame_size) !=
            DeviceProtocolStatus::Ok) {
            ++counters_.response_write_failures;
            return {};
        }
        const std::int64_t write_started_us = esp_timer_get_time();
        std::size_t written_bytes = 0;
        // USB Serial/JTAG may accept only its current TX-buffer capacity. A
        // partial write is not a failed JFDP response while the deadline holds.
        while (written_bytes < frame_size && esp_timer_get_time() - write_started_us < kUsbWriteDeadlineUs) {
            const int count = usb_serial_jtag_write_bytes(response_frame_.data() + written_bytes,
                                                           frame_size - written_bytes, kUsbIoTimeout);
            if (count > 0) {
                written_bytes += static_cast<std::size_t>(count);
            }
        }
        const bool written = written_bytes == frame_size;
        const ResponseWrite write{static_cast<std::uint32_t>(esp_timer_get_time() - write_started_us), written};
        if (!written) {
            ++counters_.response_write_failures;
            return write;
        }
        ++counters_.responses;
        return write;
    }

    void record_commit_telemetry(const ResponseWrite& response) {
        if (!commit_telemetry_.pending) return;
        const DeviceImageStore::VerifyTelemetry& verify = commit_telemetry_.verify;
        char message[kDeviceAppLogMaxMessageBytes + 1]{};
        std::snprintf(message, sizeof(message),
                      "install rc=%u crc=%u inspect=%u publish=%u rsp=%u rsp_ok=%u stack=%u stack_free=%u "
                      "reads=%u bytes=%u heap=%u ws=store cache=4096",
                      static_cast<unsigned>(commit_telemetry_.result_code),
                      static_cast<unsigned>(verify.transport_crc_us), static_cast<unsigned>(verify.inspect_bundle_us),
                      static_cast<unsigned>(verify.registry_publish_us), static_cast<unsigned>(response.elapsed_us),
                      response.ok ? 1u : 0u, static_cast<unsigned>(kDeviceImageTaskStackBytes),
                      static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                      static_cast<unsigned>(verify.reader_calls), static_cast<unsigned>(verify.reader_bytes),
                      static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
        record_app_log(DeviceAppLogLevel::Info, std::string_view(commit_telemetry_.app_id.data()),
                       store_.registry_generation(), message);
        commit_telemetry_.pending = false;
    }

    JfdpTransportCounters counters_{};
    DeviceImageStore& store_;
    DeviceInstallTransaction transaction_;
    AppRuntimeHost host_;
    ProtectedLauncher launcher_;
    AppInstalledBundleBinding binding_;
    InstalledBundleUiSession* ui_session_ = nullptr;
    InstalledBundleScriptSession* script_session_ = nullptr;
    std::string active_app_id_;
    std::array<DeviceAppLogEntry, kAppLogCapacity> app_logs_{};
    std::array<std::uint8_t, kDeviceProtocolHeaderBytes + kDeviceProtocolMaxPayloadBytes> response_frame_{};
    std::size_t app_log_count_ = 0;
    std::uint32_t app_log_overwrites_ = 0;
    CommitTelemetry commit_telemetry_{};
};

struct DeviceImageLifecycleRuntime {
    explicit DeviceImageLifecycleRuntime(const esp_partition_t* partition)
        : store(partition), endpoint(store), adapter(endpoint) {}

    DeviceImageStore store;
    DeviceImageEndpoint endpoint;
    JfdpStreamAdapter adapter;
    std::array<std::uint8_t, kUsbReadBytes> received{};
};

void device_image_lifecycle_task(void*) {
    usb_serial_jtag_driver_config_t config{};
    config.rx_buffer_size = kUsbBufferBytes;
    config.tx_buffer_size = kUsbBufferBytes;
    if (usb_serial_jtag_driver_install(&config) != ESP_OK) {
        vTaskDelete(nullptr);
        return;
    }
    const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                                 ESP_PARTITION_SUBTYPE_ANY, "storage");
    // Keep the store-owned inspection workspace, sector cache and transport
    // frames out of this task's call stack. The endpoint serializes access.
    static DeviceImageLifecycleRuntime* runtime = nullptr;
    runtime = new (std::nothrow) DeviceImageLifecycleRuntime(partition);
    if (runtime == nullptr || !runtime->store.initialize()) {
        vTaskDelete(nullptr);
        return;
    }
    std::int64_t last_byte_us = 0;
    bool was_connected = usb_serial_jtag_is_connected();
    for (;;) {
        runtime->endpoint.poll_lifecycle();
        const bool connected = usb_serial_jtag_is_connected();
        if (was_connected && !connected) {
            runtime->adapter.reset(runtime->endpoint.counters(), true);
            last_byte_us = 0;
        }
        was_connected = connected;
        const int count = usb_serial_jtag_read_bytes(runtime->received.data(), runtime->received.size(), kUsbIoTimeout);
        const std::int64_t now = esp_timer_get_time();
        if (count > 0) {
            last_byte_us = now;
            runtime->adapter.feed(runtime->received.data(), static_cast<std::size_t>(count), runtime->endpoint.counters());
        } else if (runtime->adapter.has_partial_frame() && last_byte_us != 0 && now - last_byte_us >= kPartialFrameTimeoutUs) {
            runtime->adapter.reset(runtime->endpoint.counters(), true);
            last_byte_us = 0;
        }
    }
}

} // namespace

bool start_device_image_lifecycle_task() {
    // The lifecycle runtime owns its bounded buffers outside the task stack.
    return xTaskCreate(device_image_lifecycle_task, "device_image", 24576, nullptr, 5, nullptr) == pdPASS;
}

} // namespace jellyframe_esp32s3
