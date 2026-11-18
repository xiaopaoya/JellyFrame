#pragma once

#include "render_core/host.h"
#include "render_core/feature_config.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace jellyframe::native {

inline void admit_desktop_core_layers(HostBudgets& budgets) {
#if defined(JELLYFRAME_RENDER_CORE_CONTEXTUAL_TEXT_ENABLED)
    // ABI9's scalar Core path reserves four potential layers per layout box
    // plus a root. Match the desktop's existing box allowance; this is a hard
    // ceiling, not an eager allocation and not an embedded default change.
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    if (budgets.max_layout_boxes > (maximum - 1) / 4)
        throw std::overflow_error("desktop Core layer reservation overflow");
    budgets.max_layers = std::max(budgets.max_layers, budgets.max_layout_boxes * 4 + 1);
#else
    (void)budgets;
#endif
}

} // namespace jellyframe::native
