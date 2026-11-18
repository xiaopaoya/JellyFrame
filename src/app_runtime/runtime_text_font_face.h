#pragma once

#include "render_core/text_system.h"
#include <type_traits>
#include <utility>

namespace jellyframe::detail {
template<class Face, class = void> struct HasCssFamilyHash : std::false_type {};
template<class Face> struct HasCssFamilyHash<Face, std::void_t<
    decltype(std::declval<Face&>().css_family_hash)>> : std::true_type {};

// Public Core aggregates differ between scalar ABI4 and contextual ABI9.
// Register their shared fields by name, without depending on aggregate order.
template<class Face = TextFontFace>
Face registered_text_face(std::uint64_t resource_id, std::uint32_t family_id,
                          const FontResource& resource, std::uint32_t family_hash) {
    Face face;
    face.resource_id = resource_id;
    face.family_id = family_id;
    face.resource = resource;
    if constexpr (HasCssFamilyHash<Face>::value) face.css_family_hash = family_hash;
    return face;
}
}
