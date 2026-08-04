#pragma once

#include "app_runtime/script_task_contract.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace jellyframe {

enum class ScriptTaskInputKind : std::uint8_t {
    PointerMove = 1,
    PointerDown,
    PointerUp,
    Wheel,
    KeyDown,
    TextInput,
};

// A normalized host-input value. button uses -1 for no button; key_code is a
// port-neutral numeric key identifier agreed by the worker and supervisor.
struct ScriptTaskInputEvent {
    ScriptTaskInputKind kind = ScriptTaskInputKind::PointerMove;
    int x = 0;
    int y = 0;
    int delta_x = 0;
    int delta_y = 0;
    int button = -1;
    int buttons = 0;
    std::uint8_t modifiers = 0;
    std::uint32_t key_code = 0;
    std::string text;
};

struct ScriptTaskInputCodecOptions {
    std::size_t max_text_bytes = 0;
    std::size_t max_payload_bytes = 0;
};

enum class ScriptTaskInputCodecStatus {
    Accepted,
    TextTooLarge,
    PayloadTooLarge,
    InvalidValue,
    Malformed,
};

ScriptTaskInputCodecStatus encode_script_task_input(const ScriptTaskInputEvent& input,
                                                     const ScriptTaskInputCodecOptions& options,
                                                     std::vector<std::uint8_t>& output);
ScriptTaskInputCodecStatus decode_script_task_input(const std::vector<std::uint8_t>& input,
                                                     const ScriptTaskInputCodecOptions& options,
                                                     ScriptTaskInputEvent& output);

struct ScriptTaskInputPostResult {
    ScriptTaskInputCodecStatus codec_status = ScriptTaskInputCodecStatus::InvalidValue;
    ScriptTaskMailboxPostStatus mailbox_status = ScriptTaskMailboxPostStatus::InvalidPacket;

    bool accepted() const {
        return codec_status == ScriptTaskInputCodecStatus::Accepted &&
               mailbox_status == ScriptTaskMailboxPostStatus::Accepted;
    }
};

ScriptTaskInputPostResult post_script_task_input(ScriptTaskSupervisor& supervisor,
                                                  const ScriptAppSession& session,
                                                  std::uint32_t sequence,
                                                  const ScriptTaskInputEvent& input,
                                                  const ScriptTaskInputCodecOptions& options);

} // namespace jellyframe
