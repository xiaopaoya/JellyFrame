#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace jellyframe {

class AppRuntimeHost;
class AppPrivateKvStorageMock;

enum class AppStorageLifecycleTrigger {
    Suspend,
    Resume,
    Exit,
    Crash,
    Uninstall,
    UpdateReplace,
    MemoryPressure,
};

struct AppStorageLifecyclePolicy {
    bool flush_pending_on_suspend = false;
    bool flush_pending_on_exit = true;
    bool flush_pending_on_update = true;
    bool drop_pending_on_crash = true;
    bool drop_pending_on_memory_pressure = true;
    bool delete_data_on_uninstall = true;
    bool keep_data_on_update = true;
};

struct AppStorageLifecycleDecision {
    bool flush_pending_writes = false;
    bool drop_pending_writes = false;
    bool delete_persistent_data = false;
    bool keep_persistent_data = true;
};

struct AppStorageLifecycleApplyResult {
    std::size_t flushed_pending_writes = 0;
    std::size_t dropped_pending_writes = 0;
    std::size_t deleted_persistent_items = 0;
    std::size_t remaining_pending_writes = 0;
    bool flush_stopped_before_empty = false;
};

AppStorageLifecycleDecision app_storage_lifecycle_decision_for(
    AppStorageLifecycleTrigger trigger,
    const AppStorageLifecyclePolicy& policy = {});

AppStorageLifecycleApplyResult apply_app_storage_lifecycle_decision(
    AppRuntimeHost& host,
    AppPrivateKvStorageMock& storage,
    const std::string& app_id,
    std::uint32_t app_instance_id,
    const AppStorageLifecycleDecision& decision,
    std::size_t max_flush_ops = 0);

const char* app_storage_lifecycle_trigger_name(AppStorageLifecycleTrigger trigger);

} // namespace jellyframe
