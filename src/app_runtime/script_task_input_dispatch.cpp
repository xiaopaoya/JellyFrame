#include "app_runtime/script_task_input_dispatch.h"

namespace jellyframe {
namespace {
InputModifiers modifiers(std::uint8_t bits) {
    return {(bits & 0x01U) != 0, (bits & 0x02U) != 0, (bits & 0x04U) != 0, (bits & 0x08U) != 0};
}
PointerButton button(int value) {
    switch (value) {
    case -1: return PointerButton::None;
    case 0: return PointerButton::Primary;
    case 1: return PointerButton::Middle;
    case 2: return PointerButton::Secondary;
    default: return PointerButton::None;
    }
}
bool key_code(std::uint32_t value, KeyCode& output) {
    switch (static_cast<KeyCode>(value)) {
    case KeyCode::Unknown: case KeyCode::Backspace: case KeyCode::Enter: case KeyCode::Space:
    case KeyCode::Tab: case KeyCode::ArrowUp: case KeyCode::ArrowDown:
        output = static_cast<KeyCode>(value); return true;
    }
    return false;
}

ScriptTaskInputDispatchResult dispatch(InputController& controller, const ScriptTaskInputEvent& input
#if defined(JELLYFRAME_CONTEXTUAL_INPUT_CONTROLLER_API_VERSION) && JELLYFRAME_CONTEXTUAL_INPUT_CONTROLLER_API_VERSION == 1
    , const contextual_text::LayerCapture* capture
#endif
) {
    const InputModifiers input_modifiers = modifiers(input.modifiers);
    const PointerInput pointer{input.x, input.y, button(input.button), input.buttons, input_modifiers};
    const WheelInput wheel{input.x, input.y, input.delta_x, input.delta_y, input_modifiers};
    switch (input.kind) {
    case ScriptTaskInputKind::PointerMove:
#if defined(JELLYFRAME_CONTEXTUAL_INPUT_CONTROLLER_API_VERSION) && JELLYFRAME_CONTEXTUAL_INPUT_CONTROLLER_API_VERSION == 1
        if (capture) controller.pointer_move(pointer, *capture);
        else
#endif
        controller.pointer_move(pointer);
        return {ScriptTaskInputDispatchStatus::Accepted, true};
    case ScriptTaskInputKind::PointerDown:
#if defined(JELLYFRAME_CONTEXTUAL_INPUT_CONTROLLER_API_VERSION) && JELLYFRAME_CONTEXTUAL_INPUT_CONTROLLER_API_VERSION == 1
        if (capture) controller.pointer_down(pointer, *capture);
        else
#endif
        controller.pointer_down(pointer);
        return {ScriptTaskInputDispatchStatus::Accepted, true};
    case ScriptTaskInputKind::PointerUp:
#if defined(JELLYFRAME_CONTEXTUAL_INPUT_CONTROLLER_API_VERSION) && JELLYFRAME_CONTEXTUAL_INPUT_CONTROLLER_API_VERSION == 1
        if (capture) controller.pointer_up(pointer, *capture);
        else
#endif
        controller.pointer_up(pointer);
        return {ScriptTaskInputDispatchStatus::Accepted, true};
    case ScriptTaskInputKind::Wheel:
#if defined(JELLYFRAME_CONTEXTUAL_INPUT_CONTROLLER_API_VERSION) && JELLYFRAME_CONTEXTUAL_INPUT_CONTROLLER_API_VERSION == 1
        if (capture) controller.wheel(wheel, *capture);
        else
#endif
        controller.wheel(wheel);
        return {ScriptTaskInputDispatchStatus::Accepted, true};
    case ScriptTaskInputKind::KeyDown: {
        KeyCode code;
        if (!key_code(input.key_code, code)) return {ScriptTaskInputDispatchStatus::EventRejected, false};
        return {ScriptTaskInputDispatchStatus::Accepted, controller.key_down({code, input_modifiers})};
    }
    case ScriptTaskInputKind::TextInput:
        return {ScriptTaskInputDispatchStatus::Accepted, controller.text_input(input.text)};
    }
    return {ScriptTaskInputDispatchStatus::EventRejected, false};
}
} // namespace

ScriptTaskInputDispatchResult dispatch_script_task_input(InputController& controller,
                                                         const ScriptTaskInputEvent& input) {
#if defined(JELLYFRAME_CONTEXTUAL_INPUT_CONTROLLER_API_VERSION) && JELLYFRAME_CONTEXTUAL_INPUT_CONTROLLER_API_VERSION == 1
    return dispatch(controller, input, nullptr);
#else
    return dispatch(controller, input);
#endif
}

#if defined(JELLYFRAME_CONTEXTUAL_INPUT_CONTROLLER_API_VERSION) && JELLYFRAME_CONTEXTUAL_INPUT_CONTROLLER_API_VERSION == 1
ScriptTaskInputDispatchResult dispatch_script_task_input(InputController& controller,
    const ScriptTaskInputEvent& input, const contextual_text::LayerCapture& capture) {
    return dispatch(controller, input, &capture);
}
#endif

ScriptTaskInputDispatchResult dispatch_script_task_input_packet(
    InputController& controller,
    const ScriptTaskPacket& packet,
    const ScriptTaskInputCodecOptions& options) {
    if (packet.kind != ScriptTaskPacketKind::Input) return {ScriptTaskInputDispatchStatus::PacketRejected, false};
    ScriptTaskInputEvent input;
    if (decode_script_task_input(packet.payload, options, input) != ScriptTaskInputCodecStatus::Accepted) {
        return {ScriptTaskInputDispatchStatus::PacketRejected, false};
    }
    return dispatch_script_task_input(controller, input);
}
} // namespace jellyframe
