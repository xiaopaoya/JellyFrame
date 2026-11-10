#pragma once

#include "render_core/geometry.h"
#include <type_traits>
#include <utility>

namespace jellyframe::detail {
// Keep the production ABI3 source usable while an external typed Core is probed.
// The adapter reads only the active payload; it does not change wire formats.
template<class T, class = void> struct has_typed_command : std::false_type {};
template<class T> struct has_typed_command<T, std::void_t<decltype(std::declval<const T&>().type())>> : std::true_type {};
template<class T, class = void> struct has_css_shadow_paint : std::false_type {};
template<class T> struct has_css_shadow_paint<T, std::void_t<decltype(std::declval<T&>().css_shadow_paint())>> : std::true_type {};
template<class E, class = void> struct has_css_shadow_type : std::false_type {};
template<class E> struct has_css_shadow_type<E, std::void_t<decltype(E::CssBoxShadow)>> : std::true_type {};
template<class T> auto command_type(const T& c) {
    if constexpr (has_typed_command<T>::value) return c.type();
    else return c.type;
}
template<class T> void set_command_type(T& c, DisplayCommandType type) {
    if constexpr (has_typed_command<T>::value) c.set_type(type);
    else c.type = type;
}
template<class E, class = void> struct has_prepared_command_type : std::false_type {};
template<class E> struct has_prepared_command_type<E, std::void_t<decltype(E::PreparedText)>> : std::true_type {};
template<class E> bool is_prepared_command_type(E type) {
    if constexpr (has_prepared_command_type<E>::value) return type == E::PreparedText;
    else return false;
}
template<class E> bool is_css_shadow_command_type(E type) {
    if constexpr (has_css_shadow_type<E>::value) return type == E::CssBoxShadow;
    else return false;
}
template<class E> bool legacy_command_type(E type) {
    if constexpr (has_prepared_command_type<E>::value) {
        if (type == E::PreparedText) return true;
    }
    switch (type) {
    case DisplayCommandType::FillRect: case DisplayCommandType::StrokeRect:
    case DisplayCommandType::LinearGradient: case DisplayCommandType::ConicGradient:
    case DisplayCommandType::RadialGradient: case DisplayCommandType::BoxShadow:
    case DisplayCommandType::Text:
    case DisplayCommandType::Image: return true;
    default: return false;
    }
}
template<class T> decltype(auto) command_border_radius(T& c) {
    if constexpr (has_typed_command<T>::value) {
        const auto type = command_type(c);
        if (type == DisplayCommandType::Image) return (c.image_paint().border_radius);
        if (type == DisplayCommandType::Text || type == decltype(command_type(c))::PreparedText) return (c.text_paint().border_radius);
        return (c.basic_paint().border_radius);
    } else return (c.border_radius);
}
template<class T> decltype(auto) command_stroke_width(T& c) {
    if constexpr (has_typed_command<T>::value) {
        const auto type = command_type(c);
        if (type == DisplayCommandType::Text || type == decltype(command_type(c))::PreparedText) return (c.text_paint().stroke_width);
        return (c.basic_paint().stroke_width);
    } else return (c.stroke_width);
}
template<class T> decltype(auto) command_font_size(T& c) {
    if constexpr (has_typed_command<T>::value) {
        return (c.text_paint().font_size);
    } else return (c.font_size);
}
template<class T> decltype(auto) command_font_weight(T& c) {
    if constexpr (has_typed_command<T>::value) {
        return (c.text_paint().font_weight);
    } else return (c.font_weight);
}
template<class T> decltype(auto) command_font_family_hash(T& c) {
    if constexpr (has_typed_command<T>::value) {
        return (c.text_paint().font_family_hash);
    } else return (c.font_family_hash);
}
template<class T> decltype(auto) command_text_align(T& c) {
    if constexpr (has_typed_command<T>::value) {
        return (c.text_paint().text_align);
    } else return (c.text_align);
}
template<class T> decltype(auto) command_text_single_line(T& c) {
    if constexpr (has_typed_command<T>::value) {
        return (c.text_paint().text_single_line);
    } else return (c.text_single_line);
}
template<class T> decltype(auto) command_gradient_axis(T& c) {
    if constexpr (has_typed_command<T>::value) {
        return (c.basic_paint().gradient_axis);
    } else return (c.gradient_axis);
}
template<class T> decltype(auto) command_gradient_stop_percent(T& c) {
    if constexpr (has_typed_command<T>::value) {
        return (c.basic_paint().gradient_stop_percent);
    } else return (c.gradient_stop_percent);
}
template<class T> decltype(auto) command_image_handle(T& c) {
    if constexpr (has_typed_command<T>::value) {
        return (c.image_paint().image_handle);
    } else return (c.image_handle);
}
template<class T> decltype(auto) command_object_fit(T& c) {
    if constexpr (has_typed_command<T>::value) {
        return (c.image_paint().object_fit);
    } else return (c.object_fit);
}
template<class T> decltype(auto) command_object_position(T& c) {
    if constexpr (has_typed_command<T>::value) {
        return (c.image_paint().object_position);
    } else return (c.object_position);
}
template<class T> decltype(auto) command_object_position(const T& c) {
    if constexpr (has_typed_command<T>::value) return (c.image_paint().object_position);
    else return (c.object_position);
}
template<class T> decltype(auto) command_image_rendering(T& c) {
    if constexpr (has_typed_command<T>::value) {
        return (c.image_paint().image_rendering);
    } else return (c.image_rendering);
}

struct CommandBackgroundPaintClip {
    int dx = 0;
    int dy = 0;
    int width = 0;
    int height = 0;
};
template<class T, class = void> struct has_background_paint_clip : std::false_type {};
template<class T> struct has_background_paint_clip<T, std::void_t<decltype(std::declval<T&>().background_paint_clip)>> : std::true_type {};
template<class T, class = void> struct has_image_background_paint_clip : std::false_type {};
template<class T> struct has_image_background_paint_clip<T, std::void_t<decltype(std::declval<T&>().image_paint().background_paint_clip)>> : std::true_type {};
template<class T, class = void> struct has_basic_background_paint_clip : std::false_type {};
template<class T> struct has_basic_background_paint_clip<T, std::void_t<decltype(std::declval<T&>().basic_paint().background_paint_clip)>> : std::true_type {};

template<class T> constexpr bool has_background_clip_transport() {
    if constexpr (!has_typed_command<T>::value) return false;
    else return has_image_background_paint_clip<T>::value && has_basic_background_paint_clip<T>::value;
}
template<class T> CommandBackgroundPaintClip command_background_paint_clip(const T& c) {
    if constexpr (has_typed_command<T>::value) {
        const auto type = command_type(c);
        if (type == DisplayCommandType::Image) {
            using Paint = std::remove_reference_t<decltype(c.image_paint())>;
            if constexpr (has_background_paint_clip<Paint>::value) {
                const auto& clip = c.image_paint().background_paint_clip;
                return {clip.dx, clip.dy, clip.width, clip.height};
            }
        } else if (type == DisplayCommandType::LinearGradient || type == DisplayCommandType::ConicGradient ||
                   type == DisplayCommandType::RadialGradient) {
            using Paint = std::remove_reference_t<decltype(c.basic_paint())>;
            if constexpr (has_background_paint_clip<Paint>::value) {
                const auto& clip = c.basic_paint().background_paint_clip;
                return {clip.dx, clip.dy, clip.width, clip.height};
            }
        }
    }
    return {};
}
template<class T> void set_command_background_paint_clip(T& c, CommandBackgroundPaintClip clip) {
    if constexpr (has_typed_command<T>::value) {
        const auto type = command_type(c);
        if (type == DisplayCommandType::Image) {
            using Paint = std::remove_reference_t<decltype(c.image_paint())>;
            if constexpr (has_background_paint_clip<Paint>::value)
                c.image_paint().background_paint_clip = {
                    static_cast<std::int16_t>(clip.dx), static_cast<std::int16_t>(clip.dy),
                    static_cast<std::uint16_t>(clip.width), static_cast<std::uint16_t>(clip.height)};
        } else if (type == DisplayCommandType::LinearGradient || type == DisplayCommandType::ConicGradient ||
                   type == DisplayCommandType::RadialGradient) {
            using Paint = std::remove_reference_t<decltype(c.basic_paint())>;
            if constexpr (has_background_paint_clip<Paint>::value)
                c.basic_paint().background_paint_clip = {
                    static_cast<std::int16_t>(clip.dx), static_cast<std::int16_t>(clip.dy),
                    static_cast<std::uint16_t>(clip.width), static_cast<std::uint16_t>(clip.height)};
        }
    }
}
inline bool command_background_paint_clip_empty(CommandBackgroundPaintClip clip) {
    return clip.dx == 0 && clip.dy == 0 && clip.width == 0 && clip.height == 0;
}

// The legacy records contain fields for every alternative. Unused fields retain
// their original wire defaults instead of being read through inactive union members.
struct LegacyCommandPaintFields {
    int border_radius = 0;
    int stroke_width = 1;
    int font_size = 14;
    int font_weight = 400;
    std::uint32_t font_family_hash = 0;
    TextCommandAlign text_align = TextCommandAlign::Start;
    bool text_single_line = true;
    GradientAxis gradient_axis = GradientAxis::Vertical;
    int gradient_stop_percent = 100;
    std::uint32_t image_handle = 0;
    ObjectFit object_fit = ObjectFit::Fill;
    ObjectPosition object_position;
    ImageRendering image_rendering = ImageRendering::Auto;
};
template<class T> LegacyCommandPaintFields legacy_command_fields(const T& c) {
    LegacyCommandPaintFields p;
    if constexpr (has_typed_command<T>::value) {
        const auto type = command_type(c);
        if (type <= DisplayCommandType::BoxShadow) {
            p.border_radius = c.basic_paint().border_radius;
            p.stroke_width = c.basic_paint().stroke_width;
            p.gradient_axis = c.basic_paint().gradient_axis;
            p.gradient_stop_percent = c.basic_paint().gradient_stop_percent;
        }
        if (type == DisplayCommandType::Text || type == decltype(command_type(c))::PreparedText) {
            p.border_radius = c.text_paint().border_radius;
            p.stroke_width = c.text_paint().stroke_width;
            p.font_size = c.text_paint().font_size;
            p.font_weight = c.text_paint().font_weight;
            p.font_family_hash = c.text_paint().font_family_hash;
            p.text_align = c.text_paint().text_align;
            p.text_single_line = c.text_paint().text_single_line;
        }
        if (type == DisplayCommandType::Image) {
            p.border_radius = c.image_paint().border_radius;
            p.image_handle = c.image_paint().image_handle;
            p.object_fit = c.image_paint().object_fit;
            p.object_position = c.image_paint().object_position;
            p.image_rendering = c.image_paint().image_rendering;
        }
    } else {
        p.border_radius = c.border_radius;
        p.stroke_width = c.stroke_width;
        p.font_size = c.font_size;
        p.font_weight = c.font_weight;
        p.font_family_hash = c.font_family_hash;
        p.text_align = c.text_align;
        p.text_single_line = c.text_single_line;
        p.gradient_axis = c.gradient_axis;
        p.gradient_stop_percent = c.gradient_stop_percent;
        p.image_handle = c.image_handle;
        p.object_fit = c.object_fit;
        p.object_position = c.object_position;
        p.image_rendering = c.image_rendering;
    }
    return p;
}
template<class T> void apply_legacy_command_fields(T& c, const LegacyCommandPaintFields& p) {
    if constexpr (has_typed_command<T>::value) {
        const auto type = command_type(c);
        if (type <= DisplayCommandType::BoxShadow) {
            c.basic_paint().border_radius = p.border_radius;
            c.basic_paint().stroke_width = p.stroke_width;
            c.basic_paint().gradient_axis = p.gradient_axis;
            c.basic_paint().gradient_stop_percent = p.gradient_stop_percent;
        }
        if (type == DisplayCommandType::Text || type == decltype(command_type(c))::PreparedText) {
            c.text_paint().border_radius = p.border_radius;
            c.text_paint().stroke_width = p.stroke_width;
            c.text_paint().font_size = p.font_size;
            c.text_paint().font_weight = p.font_weight;
            c.text_paint().font_family_hash = p.font_family_hash;
            c.text_paint().text_align = p.text_align;
            c.text_paint().text_single_line = p.text_single_line;
        }
        if (type == DisplayCommandType::Image) {
            c.image_paint().border_radius = p.border_radius;
            c.image_paint().image_handle = p.image_handle;
            c.image_paint().object_fit = p.object_fit;
            c.image_paint().object_position = p.object_position;
            c.image_paint().image_rendering = p.image_rendering;
        }
    } else {
        c.border_radius = p.border_radius;
        c.stroke_width = p.stroke_width;
        c.font_size = p.font_size;
        c.font_weight = p.font_weight;
        c.font_family_hash = p.font_family_hash;
        c.text_align = p.text_align;
        c.text_single_line = p.text_single_line;
        c.gradient_axis = p.gradient_axis;
        c.gradient_stop_percent = p.gradient_stop_percent;
        c.image_handle = p.image_handle;
        c.object_fit = p.object_fit;
        c.object_position = p.object_position;
        c.image_rendering = p.image_rendering;
    }
}
template<class T> bool command_has_untransported_payload(const T& c) {
    if constexpr (has_typed_command<T>::value) {
        // Legacy PreparedText stores origin in legacy fields; the new explicit
        // origin cannot be carried by v1-v7 without a new, negotiated format.
        if (command_type(c) == decltype(command_type(c))::PreparedText) return true;
    }
    return false;
}
} // namespace jellyframe::detail
