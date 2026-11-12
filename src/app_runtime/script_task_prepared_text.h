#pragma once

#include "app_runtime/script_task_command_access.h"

#include "render_core/geometry.h"

// The new wire version requires the exact canonical packet API. It never
// converts legacy JFGR or older JFTP versions into a prepared resource packet.
#if __has_include("render_core/prepared_text.h")
#include "render_core/prepared_text.h"
#endif
#if defined(JELLYFRAME_RENDER_CORE_PREPARED_TEXT_API_VERSION) && JELLYFRAME_RENDER_CORE_PREPARED_TEXT_API_VERSION == 3
#define JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API 1
#else
#define JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API 0
#endif

// Independent canonical JFTP v1/v2 resources. The legacy JFTP3 wire family
// retains its separate API gate.
#if defined(JELLYFRAME_RENDER_CORE_CANONICAL_VALUE_API_VERSION) && JELLYFRAME_RENDER_CORE_CANONICAL_VALUE_API_VERSION == 1 && \
    defined(JELLYFRAME_RENDER_CORE_PREPARED_TEXT_PACKET_VERSION) && \
    (JELLYFRAME_RENDER_CORE_PREPARED_TEXT_PACKET_VERSION == 1 || JELLYFRAME_RENDER_CORE_PREPARED_TEXT_PACKET_VERSION == 2)
#define JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API 1
#else
#define JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API 0
#endif
#define JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API (JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API || JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API)

namespace jellyframe::detail {

inline bool has_prepared_text_packet(const DisplayCommand& command) {
#if JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API
    return jellyframe::detail::command_type(command) == DisplayCommandType::GlyphRun &&
        command.text.size() >= 4 && command.text.compare(0, 4, "JFTP") == 0;
#else
    (void)command;
    return false;
#endif
}

inline bool valid_prepared_text_command(const DisplayCommand& command) {
#if JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API
    if (jellyframe::detail::command_type(command) == DisplayCommandType::Text ||
        jellyframe::detail::command_type(command) == DisplayCommandType::BaselineText ||
        jellyframe::detail::command_type(command) == DisplayCommandType::OriginText) return false;
    if (jellyframe::detail::command_type(command) != DisplayCommandType::GlyphRun) return true;
    PreparedTextView view;
    return decode_prepared_text(command.text, view);
#else
    (void)command;
    return false;
#endif
}

inline bool valid_transport_text_command(const DisplayCommand& command, unsigned version) {
    return version >= 7 ? valid_prepared_text_command(command) : !has_prepared_text_packet(command);
}

} // namespace jellyframe::detail
