#pragma once

#include "device_runtime_contracts/device_runtime_protocol.h"
#include "app_runtime/runtime_shaped_text_binding.h"

namespace jellyframe_esp32s3 {
// Shared by the live JFDP response and the exact-target capability audit.
inline constexpr std::uint32_t render_core_feature_family_bits() noexcept {
    using namespace jellyframe;
    std::uint32_t bits = DeviceRenderCoreFeatureDocument | DeviceRenderCoreFeaturePaint;
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    bits |= DeviceRenderCoreFeatureFlexGrid;
#endif
#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    bits |= DeviceRenderCoreFeatureModernPaint;
#endif
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
    bits |= DeviceRenderCoreFeatureAdvancedForms;
#endif
#if JELLYFRAME_RENDER_CORE_CANVAS2D_ENABLED
    bits |= DeviceRenderCoreFeatureCanvas2d;
#endif
#if JELLYFRAME_RENDER_CORE_TEXT_SHAPING_ENABLED
    bits |= DeviceRenderCoreFeatureTextShaping;
#endif
#if JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API && JELLYFRAME_RENDER_CORE_SHAPED_PACKET_V2_ENABLED
    bits |= DeviceRenderCoreFeatureContextualJfsp2;
#if JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
    bits |= DeviceRenderCoreFeatureContextualLayout;
#endif
#endif
#if JELLYFRAME_RENDER_CORE_LENGTH_MATH_ENABLED
    bits |= DeviceRenderCoreFeatureLengthMath;
#endif
#if JELLYFRAME_RENDER_CORE_CSS_COUNTERS_ENABLED
    bits |= DeviceRenderCoreFeatureCounters;
#endif
#if JELLYFRAME_RENDER_CORE_CSS_BORDERS_ENABLED
    bits |= DeviceRenderCoreFeatureBorders;
#endif
#if JELLYFRAME_RENDER_CORE_TABLES_ENABLED
    bits |= DeviceRenderCoreFeatureTables;
#endif
#if JELLYFRAME_RENDER_CORE_POSITIONED_SIZING_ENABLED
    bits |= DeviceRenderCoreFeaturePositionedSizing;
#endif
    return bits;
}
} // namespace jellyframe_esp32s3
