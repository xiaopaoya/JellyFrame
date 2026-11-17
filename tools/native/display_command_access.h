#pragma once

#include "render_core/geometry.h"
#include <type_traits>

// ABI9 retains legacy constructor signatures, but its scalar Core TextSystem
// is still the native default. Contextual opt-in availability must not silently
// switch a desktop session back to platform GDI text.
#if defined(JELLYFRAME_RENDER_CORE_CANONICAL_VALUE_API_VERSION) || \
    defined(JELLYFRAME_RENDER_CORE_CONTEXTUAL_TEXT_ENABLED)
#define JELLYFRAME_NATIVE_CORE_TEXT_DEFAULT 1
#else
#define JELLYFRAME_NATIVE_CORE_TEXT_DEFAULT 0
#endif

// ABI4 exposes a value-owned sizing policy; ABI9 exposes a borrowed policy
// with a bool/out-parameter callback. Both need image completion relayout.
#if (defined(JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION) && JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION >= 1) || \
    defined(JELLYFRAME_RENDER_CORE_CONTEXTUAL_TEXT_ENABLED)
#define JELLYFRAME_NATIVE_NATURAL_IMAGE_LAYOUT 1
#else
#define JELLYFRAME_NATIVE_NATURAL_IMAGE_LAYOUT 0
#endif

namespace jellyframe::native {

inline DisplayCommandType display_command_type(const DisplayCommand& command) {
#if defined(JELLYFRAME_RENDER_CORE_CANONICAL_VALUE_API_VERSION)
    return command.type();
#else
    return command.type;
#endif
}

// Diagnostic names follow the selected public command enum. ABI9 no longer
// exposes PreparedText and adds flat command kinds; do not assume either ABI.
#define JELLYFRAME_NATIVE_OPTIONAL_COMMAND(Name) \
template<class E, class = void> struct has_##Name : std::false_type {}; \
template<class E> struct has_##Name<E, std::void_t<decltype(E::Name)>> : std::true_type {};
JELLYFRAME_NATIVE_OPTIONAL_COMMAND(PreparedText)
JELLYFRAME_NATIVE_OPTIONAL_COMMAND(CssBoxShadow)
JELLYFRAME_NATIVE_OPTIONAL_COMMAND(BaselineText)
JELLYFRAME_NATIVE_OPTIONAL_COMMAND(OriginText)
JELLYFRAME_NATIVE_OPTIONAL_COMMAND(BorderRing)
JELLYFRAME_NATIVE_OPTIONAL_COMMAND(GlyphRun)
#undef JELLYFRAME_NATIVE_OPTIONAL_COMMAND

template<class E> bool is_prepared_text_command(E type) {
    if constexpr (has_PreparedText<E>::value) return type == E::PreparedText;
    return false;
}

template<class E> const char* display_command_name(E type, bool lower_case = false) {
#define JELLYFRAME_NATIVE_COMMAND_CASE(Name, Lower) \
    case E::Name: return lower_case ? Lower : #Name;
#define JELLYFRAME_NATIVE_OPTIONAL_NAME(Name, Lower) \
    if constexpr (has_##Name<E>::value) { if (type == E::Name) return lower_case ? Lower : #Name; }
    JELLYFRAME_NATIVE_OPTIONAL_NAME(PreparedText, "prepared_text")
    JELLYFRAME_NATIVE_OPTIONAL_NAME(CssBoxShadow, "css_box_shadow")
    JELLYFRAME_NATIVE_OPTIONAL_NAME(BaselineText, "baseline_text")
    JELLYFRAME_NATIVE_OPTIONAL_NAME(OriginText, "origin_text")
    JELLYFRAME_NATIVE_OPTIONAL_NAME(BorderRing, "border_ring")
    JELLYFRAME_NATIVE_OPTIONAL_NAME(GlyphRun, "glyph_run")
    switch (type) {
    JELLYFRAME_NATIVE_COMMAND_CASE(FillRect, "fill_rect")
    JELLYFRAME_NATIVE_COMMAND_CASE(StrokeRect, "stroke")
    JELLYFRAME_NATIVE_COMMAND_CASE(LinearGradient, "linear_gradient")
    JELLYFRAME_NATIVE_COMMAND_CASE(RadialGradient, "radial_gradient")
    JELLYFRAME_NATIVE_COMMAND_CASE(ConicGradient, "conic_gradient")
    JELLYFRAME_NATIVE_COMMAND_CASE(BoxShadow, "box_shadow")
    JELLYFRAME_NATIVE_COMMAND_CASE(Text, "text")
    JELLYFRAME_NATIVE_COMMAND_CASE(Image, "image")
    default: return lower_case ? "unknown" : "Unknown";
    }
#undef JELLYFRAME_NATIVE_OPTIONAL_NAME
#undef JELLYFRAME_NATIVE_COMMAND_CASE
}

} // namespace jellyframe::native
