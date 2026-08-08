#pragma once

#include "app_runtime/script_task_contract.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jellyframe {

// Value-only fatal status received by the supervisor. reason is owned by the
// script layer so this protocol does not depend on JerryScript headers.
struct ScriptTaskFatalRecord {
    ScriptAppSession session;
    std::uint8_t reason = 0;
    std::uint32_t diagnostic_code = 0;
    std::uint32_t last_input_sequence = 0;
    std::uint32_t last_frame_sequence = 0;
    std::uint64_t internal_bytes = 0;
    std::uint32_t message_bytes = 0;

    bool valid() const {
        return session.valid() && reason != 0;
    }
};

struct ScriptTaskFatalCodecOptions {
    std::size_t max_payload_bytes = 0;
};

enum class ScriptTaskFatalCodecStatus : std::uint8_t {
    Accepted,
    PayloadTooLarge,
    InvalidValue,
    Malformed,
};

ScriptTaskFatalCodecStatus encode_script_task_fatal(
    const ScriptTaskFatalRecord& record,
    const ScriptTaskFatalCodecOptions& options,
    std::vector<std::uint8_t>& output);

ScriptTaskFatalCodecStatus decode_script_task_fatal(
    const std::vector<std::uint8_t>& input,
    const ScriptTaskFatalCodecOptions& options,
    ScriptTaskFatalRecord& output);

ScriptTaskMailboxPostStatus post_script_task_fatal(
    ScriptTaskSupervisor& supervisor,
    const ScriptTaskFatalRecord& record,
    std::uint32_t sequence,
    const ScriptTaskFatalCodecOptions& options);

} // namespace jellyframe
