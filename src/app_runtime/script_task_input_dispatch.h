#pragma once

#include "app_runtime/script_task_input_codec.h"
#include "render_core/input.h"

namespace jellyframe {

enum class ScriptTaskInputDispatchStatus {
    Accepted,
    PacketRejected,
    EventRejected,
};

struct ScriptTaskInputDispatchResult {
    ScriptTaskInputDispatchStatus status = ScriptTaskInputDispatchStatus::PacketRejected;
    bool handled = false;
};

// Worker-task-only adapter. InputController, its LayerNode and all DOM event
// listeners remain local to the worker; this API exposes no target pointer.
ScriptTaskInputDispatchResult dispatch_script_task_input(InputController& controller,
                                                         const ScriptTaskInputEvent& input);
#if defined(JELLYFRAME_CONTEXTUAL_INPUT_CONTROLLER_API_VERSION) && JELLYFRAME_CONTEXTUAL_INPUT_CONTROLLER_API_VERSION == 1
ScriptTaskInputDispatchResult dispatch_script_task_input(InputController& controller,
    const ScriptTaskInputEvent& input, const contextual_text::LayerCapture& capture);
#endif
ScriptTaskInputDispatchResult dispatch_script_task_input_packet(
    InputController& controller,
    const ScriptTaskPacket& packet,
    const ScriptTaskInputCodecOptions& options);

} // namespace jellyframe
