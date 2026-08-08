#pragma once

#include "app_runtime/host_services.h"
#include "app_runtime/script_task_contract.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jellyframe {

// Value-only request submitted by a script worker. request_handle is an
// opaque supervisor-owned ID; it is never a pointer or a task-local address.
struct ScriptTaskServiceRequest {
    HostServiceJobKind kind = HostServiceJobKind::Other;
    std::uint32_t request_id = 0;
    std::uint32_t client_token = 0;
    std::uint32_t request_handle = 0;
    std::uint8_t priority = 0;
    std::uint32_t timeout_ms = 0;
};

// Value-only cancellation identity. The worker never sends a host job ID or
// bridge address; the supervisor/bridge resolves this token in its own task.
struct ScriptTaskServiceCancel {
    std::uint32_t request_id = 0;
    std::uint32_t client_token = 0;

    bool valid() const { return request_id != 0 && client_token != 0; }
};

struct ScriptTaskServiceRequestCodecOptions {
    std::size_t max_payload_bytes = 0;
};

enum class ScriptTaskServiceRequestCodecStatus {
    Accepted,
    PayloadTooLarge,
    InvalidValue,
    Malformed,
};

ScriptTaskServiceRequestCodecStatus encode_script_task_service_request(
    const ScriptTaskServiceRequest& request,
    const ScriptTaskServiceRequestCodecOptions& options,
    std::vector<std::uint8_t>& output);
ScriptTaskServiceRequestCodecStatus decode_script_task_service_request(
    const std::vector<std::uint8_t>& input,
    const ScriptTaskServiceRequestCodecOptions& options,
    ScriptTaskServiceRequest& output);

struct ScriptTaskServiceRequestPostResult {
    ScriptTaskServiceRequestCodecStatus codec_status = ScriptTaskServiceRequestCodecStatus::InvalidValue;
    ScriptTaskMailboxPostStatus mailbox_status = ScriptTaskMailboxPostStatus::InvalidPacket;

    bool accepted() const {
        return codec_status == ScriptTaskServiceRequestCodecStatus::Accepted &&
               mailbox_status == ScriptTaskMailboxPostStatus::Accepted;
    }
};

ScriptTaskServiceRequestPostResult post_script_task_service_request(
    ScriptTaskSupervisor& supervisor,
    const ScriptAppSession& session,
    std::uint32_t sequence,
    const ScriptTaskServiceRequest& request,
    const ScriptTaskServiceRequestCodecOptions& options);

ScriptTaskServiceRequestCodecStatus encode_script_task_service_cancel(
    const ScriptTaskServiceCancel& cancel,
    const ScriptTaskServiceRequestCodecOptions& options,
    std::vector<std::uint8_t>& output);
ScriptTaskServiceRequestCodecStatus decode_script_task_service_cancel(
    const std::vector<std::uint8_t>& input,
    const ScriptTaskServiceRequestCodecOptions& options,
    ScriptTaskServiceCancel& output);

ScriptTaskServiceRequestPostResult post_script_task_service_cancel(
    ScriptTaskSupervisor& supervisor,
    const ScriptAppSession& session,
    std::uint32_t sequence,
    const ScriptTaskServiceCancel& cancel,
    const ScriptTaskServiceRequestCodecOptions& options);

} // namespace jellyframe
