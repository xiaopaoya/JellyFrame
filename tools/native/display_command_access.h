#pragma once

#include "render_core/geometry.h"

namespace jellyframe::native {

inline DisplayCommandType display_command_type(const DisplayCommand& command) {
#if defined(JELLYFRAME_RENDER_CORE_CANONICAL_VALUE_API_VERSION)
    return command.type();
#else
    return command.type;
#endif
}

} // namespace jellyframe::native
