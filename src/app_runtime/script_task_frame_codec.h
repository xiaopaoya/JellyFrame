#pragma once

#include "render_core/geometry.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jellyframe {

struct ScriptTaskInputTarget {
    std::uint32_t target_key = 0;
    Rect rect;
    bool enabled = true;
};

struct ScriptTaskAppFrame {
    Rect viewport;
    DisplayList display_list;
    std::vector<ScriptTaskInputTarget> input_targets;
};

struct ScriptTaskAppFrameCodecOptions {
    std::size_t max_commands = 0;
    std::size_t max_text_bytes = 0;
    std::size_t max_input_targets = 0;
    std::size_t max_payload_bytes = 0;
};

enum class ScriptTaskAppFrameCodecStatus {
    Accepted,
    TooManyCommands,
    TooManyTextBytes,
    TooManyInputTargets,
    PayloadTooLarge,
    InvalidValue,
    Malformed,
};

// Versioned, value-only wire format. Image handles and font-family hashes are
// opaque integers; no DisplayCommand storage or text pointer crosses tasks.
ScriptTaskAppFrameCodecStatus encode_script_task_app_frame(
    const ScriptTaskAppFrame& frame,
    const ScriptTaskAppFrameCodecOptions& options,
    std::vector<std::uint8_t>& output);
ScriptTaskAppFrameCodecStatus decode_script_task_app_frame(
    const std::vector<std::uint8_t>& input,
    const ScriptTaskAppFrameCodecOptions& options,
    ScriptTaskAppFrame& output);

// The frame carries hit regions in paint order. Reverse lookup selects the
// visually topmost enabled target and returns only its opaque worker key.
std::uint32_t resolve_script_task_input_target(const ScriptTaskAppFrame& frame, int x, int y);

} // namespace jellyframe
