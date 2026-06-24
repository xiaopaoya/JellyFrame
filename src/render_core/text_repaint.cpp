#include "render_core/text_repaint.h"

#include "render_core/text_layout_reuse.h"

namespace jellyframe {
namespace {

bool dirty_flags_are_text_layout_only(DomDirtyFlags flags) {
    return (flags & ~(DomDirtyText | DomDirtyLayout | DomDirtyPaint)) == 0U;
}

} // namespace

bool text_dirty_can_reuse_layout(const Node& document,
                                 const LayoutBox& layout,
                                 const TextMeasureProvider& text_measure) {
    if ((document.dirty_flags & DomDirtyText) == 0U ||
        !dirty_flags_are_text_layout_only(document.dirty_flags)) {
        return false;
    }
    return dirty_text_nodes_have_stable_layout(document, layout, text_measure, true);
}

} // namespace jellyframe
