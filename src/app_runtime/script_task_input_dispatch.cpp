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
}

ScriptTaskInputDispatchResult dispatch_script_task_input(InputController& controller,
                                                         const ScriptTaskInputEvent& input) {
    const InputModifiers input_modifiers = modifiers(input.modifiers);
    switch (input.kind) {
    case ScriptTaskInputKind::PointerMove:
        controller.pointer_move({input.x, input.y, button(input.button), input.buttons, input_modifiers});
        return {ScriptTaskInputDispatchStatus::Accepted, true};
    case ScriptTaskInputKind::PointerDown:
        controller.pointer_down({input.x, input.y, button(input.button), input.buttons, input_modifiers});
        return {ScriptTaskInputDispatchStatus::Accepted, true};
    case ScriptTaskInputKind::PointerUp:
        controller.pointer_up({input.x, input.y, button(input.button), input.buttons, input_modifiers});
        return {ScriptTaskInputDispatchStatus::Accepted, true};
    case ScriptTaskInputKind::Wheel:
        controller.wheel({input.x, input.y, input.delta_x, input.delta_y, input_modifiers});
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
