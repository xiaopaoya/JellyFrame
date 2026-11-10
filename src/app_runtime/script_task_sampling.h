#pragma once

#include "app_runtime/script_task_command_access.h"

#include "render_core/geometry.h"

#include <type_traits>
#include <utility>

namespace jellyframe {
namespace detail {

// Runtime may consume either its locked Core package or an experimental source
// provider. Keep the optional sampling field out of the locked Core's ABI.
template <typename Command, typename = void>
struct HasCommandSamplingRect : std::false_type {};

template <typename Command>
struct HasCommandSamplingRect<Command, std::void_t<decltype(std::declval<Command>().sampling_rect)>>
    : std::true_type {};

constexpr bool kScriptTaskSamplingTransportAvailable = HasCommandSamplingRect<DisplayCommand>::value;

template <typename Command>
Rect command_sampling_rect(const Command& command) {
    if constexpr (HasCommandSamplingRect<Command>::value) return command.sampling_rect;
    else return {};
}

template <typename Command>
void set_command_sampling_rect(Command& command, Rect rect) {
    if constexpr (HasCommandSamplingRect<Command>::value) command.sampling_rect = rect;
}

inline bool equal_sampling_rects(Rect left, Rect right) {
    return left.x == right.x && left.y == right.y &&
        left.width == right.width && left.height == right.height;
}

inline bool has_sampling_payload(const DisplayCommand& command) {
    const bool packet_command = jellyframe::detail::command_type(command) == DisplayCommandType::Image ||
        jellyframe::detail::command_type(command) == DisplayCommandType::LinearGradient ||
        jellyframe::detail::command_type(command) == DisplayCommandType::ConicGradient ||
        jellyframe::detail::command_type(command) == DisplayCommandType::RadialGradient;
    // These command kinds reserve nonempty text for sampling packets. Reject
    // malformed/unknown packets too, instead of silently ignoring their bytes.
    return packet_command && !command.text.empty();
}

inline bool has_legacy_incompatible_sampling(const DisplayCommand& command) {
    const Rect sampling = command_sampling_rect(command);
    return has_sampling_payload(command) ||
        (sampling.width > 0 && sampling.height > 0 && !equal_sampling_rects(sampling, command.rect));
}

} // namespace detail
} // namespace jellyframe
