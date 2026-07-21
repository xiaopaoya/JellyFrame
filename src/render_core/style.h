#pragma once

#include "render_core/dom.h"
#include "render_core/diagnostics.h"
#include "render_core/geometry.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace jellyframe {

enum class Display : std::uint8_t {
    Block,
    Inline,
    InlineBlock,
    Flex,
    Grid,
    None,
};

enum class TextAlign : std::uint8_t {
    Start,
    Center,
    End,
};

enum class JustifyContent : std::uint8_t {
    Start,
    End,
    Center,
    SpaceAround,
    SpaceBetween,
    SpaceEvenly,
};

enum class AlignItems : std::uint8_t {
    Stretch,
    Start,
    Center,
    End,
    Auto,
};

enum class FlexDirection : std::uint8_t {
    Row,
    Column,
};

enum class ListStyleType : std::uint8_t {
    None,
    Disc,
    Decimal,
};

enum class GeneratedContentKind : std::uint8_t {
    None,
    Text,
    Counter,
};

enum class CssPseudoElement : std::uint8_t {
    None,
    Before,
    After,
};

enum class BackgroundPaintKind : std::uint8_t {
    Solid,
    LinearGradient,
    ConicGradient,
    RadialGradient,
};

enum class AnimatableProperty : std::uint8_t {
    All,
    Opacity,
    Transform,
    BackgroundColor,
    Color,
};

enum class AnimationTimingFunction : std::uint8_t {
    Linear,
    Ease,
    EaseIn,
    EaseOut,
    EaseInOut,
    CubicBezier,
};

struct BackgroundPaint {
    BackgroundPaintKind kind = BackgroundPaintKind::Solid;
    GradientAxis axis = GradientAxis::Vertical;
    int stop_percent = 100;
    Color color{0, 0, 0, 0};
    Color color2{0, 0, 0, 0};
};

inline std::uint16_t pack_background_overlay_color(Color color) {
    const auto compress = [](std::uint8_t component) {
        return static_cast<std::uint16_t>(std::min(15U, (static_cast<unsigned int>(component) + 8U) >> 4U));
    };
    return static_cast<std::uint16_t>(compress(color.r) << 12U) |
        static_cast<std::uint16_t>(compress(color.g) << 8U) |
        static_cast<std::uint16_t>(compress(color.b) << 4U) |
        compress(color.a);
}

inline Color unpack_background_overlay_color(std::uint16_t packed) {
    const auto expand = [](std::uint16_t component) {
        return static_cast<std::uint8_t>((component << 4U) | component);
    };
    return Color{expand((packed >> 12U) & 0xFU), expand((packed >> 8U) & 0xFU),
                 expand((packed >> 4U) & 0xFU), expand(packed & 0xFU)};
}

inline std::uint64_t pack_background_overlay(const BackgroundPaint& paint) {
    constexpr std::uint64_t kValid = 1U;
    return kValid |
        (static_cast<std::uint64_t>(paint.kind) << 1U) |
        (static_cast<std::uint64_t>(paint.axis) << 3U) |
        (static_cast<std::uint64_t>(std::max(0, std::min(16383, paint.stop_percent))) << 6U) |
        (static_cast<std::uint64_t>(pack_background_overlay_color(paint.color)) << 20U) |
        (static_cast<std::uint64_t>(pack_background_overlay_color(paint.color2)) << 36U);
}

inline bool has_background_overlay(std::uint64_t packed) {
    return (packed & 1U) != 0U;
}

inline BackgroundPaint unpack_background_overlay(std::uint64_t packed) {
    BackgroundPaint paint;
    paint.kind = static_cast<BackgroundPaintKind>((packed >> 1U) & 0x3U);
    paint.axis = static_cast<GradientAxis>((packed >> 3U) & 0x7U);
    paint.stop_percent = static_cast<int>((packed >> 6U) & 0x3FFFU);
    paint.color = unpack_background_overlay_color(static_cast<std::uint16_t>((packed >> 20U) & 0xFFFFU));
    paint.color2 = unpack_background_overlay_color(static_cast<std::uint16_t>((packed >> 36U) & 0xFFFFU));
    return paint;
}

// A package background image reuses the existing optional overlay word instead
// of extending Style. Resource ids are local to one StyleResolver and zero is
// reserved for "not present".
inline std::uint64_t pack_background_image_resource(std::uint16_t resource_id) {
    constexpr std::uint64_t kImageResourceMarker = 1ULL << 63U;
    return resource_id == 0 ? 0 :
        kImageResourceMarker | 1U |
        (static_cast<std::uint64_t>(resource_id) << 3U);
}

inline bool has_background_image_resource(std::uint64_t packed) {
    constexpr std::uint64_t kImageResourceMarker = 1ULL << 63U;
    return has_background_overlay(packed) && (packed & kImageResourceMarker) != 0;
}

inline std::uint16_t background_image_resource_id(std::uint64_t packed) {
    return has_background_image_resource(packed)
        ? static_cast<std::uint16_t>((packed >> 3U) & 0xFFFFU)
        : 0;
}

struct StyleTransition {
    AnimatableProperty property = AnimatableProperty::All;
    std::uint32_t duration_ms = 0;
    std::uint32_t delay_ms = 0;
    AnimationTimingFunction timing = AnimationTimingFunction::Ease;
    std::uint64_t cubic_bezier = 0;
};

enum class AnimationDirection : std::uint8_t {
    Normal,
    Alternate,
};

enum class AnimationFillMode : std::uint8_t {
    None,
    Forwards,
    Backwards,
    Both,
};

struct StyleAnimation {
    std::string name;
    std::uint32_t duration_ms = 0;
    std::uint32_t delay_ms = 0;
    AnimationTimingFunction timing = AnimationTimingFunction::Ease;
    std::uint64_t cubic_bezier = 0;
    std::uint16_t iteration_count = 1;
    bool infinite = false;
    AnimationDirection direction = AnimationDirection::Normal;
    AnimationFillMode fill_mode = AnimationFillMode::None;
};

constexpr std::size_t kMaxStyleTransitions = 4;
constexpr std::size_t kMaxStyleAnimations = 4;

struct Transform2D {
    float translate_x = 0.0F;
    float translate_y = 0.0F;
    float scale_x = 1.0F;
    float scale_y = 1.0F;
    float rotate_degrees = 0.0F;
};

// A single, bounded CSS box-shadow. Keeping this parsed avoids carrying raw CSS
// strings through every render object and makes the paint cost explicit.
struct BoxShadowStyle {
    bool enabled = false;
    bool uses_current_color = true;
    std::int16_t offset_x = 0;
    std::int16_t offset_y = 0;
    std::int16_t blur = 0;
    std::int16_t spread = 0;
    Color color{0, 0, 0, 0};
};

struct TextShadowStyle {
    bool enabled = false;
    bool uses_current_color = true;
    std::int16_t offset_x = 0;
    std::int16_t offset_y = 0;
    std::int16_t blur = 0;
    Color color{0, 0, 0, 0};
};

struct Style {
    Display display = Display::Inline;
    bool visibility_hidden = false;
    bool visibility_specified = false;
    Color color{0, 0, 0, 255};
    bool color_specified = false;
    BackgroundPaintKind background_paint = BackgroundPaintKind::Solid;
    GradientAxis background_gradient_axis = GradientAxis::Vertical;
    int background_gradient_stop_percent = 100;
    Color background_color{0, 0, 0, 0};
    Color background_color2{0, 0, 0, 0};
    // Packed RGBA4444 top layer of the documented two-layer subset. Zero means absent.
    std::uint64_t background_overlay_packed = 0;
    EdgeSizes margin;
    bool margin_left_auto = false;
    bool margin_right_auto = false;
    EdgeSizes padding;
    EdgeSizes border_width;
    Color border_color{0, 0, 0, 255};
    int border_radius = 0;
    int border_radius_percent = -1;
    int width = -1;
    int height = -1;
    int min_width = -1;
    int min_height = -1;
    int max_width = -1;
    int max_height = -1;
    int width_percent = -1;
    int height_percent = -1;
    int min_width_percent = -1;
    int min_height_percent = -1;
    int max_width_percent = -1;
    int max_height_percent = -1;
    int aspect_ratio_width = 0;
    int aspect_ratio_height = 0;
    int font_size = 14;
    bool font_size_specified = false;
    int font_weight = 400;
    bool font_weight_specified = false;
    std::uint32_t font_family_hash = 0;
    bool font_family_specified = false;
    int line_height = -1;
    bool line_height_specified = false;
    int text_indent = 0;
    bool text_indent_specified = false;
    std::int16_t letter_spacing = 0;
    bool letter_spacing_specified = false;
    TextTransform text_transform = TextTransform::None;
    bool text_transform_specified = false;
    bool text_decoration_underline = false;
    bool text_decoration_line_through = false;
    bool text_decoration_specified = false;
    TextShadowStyle text_shadow;
    bool text_shadow_specified = false;
    BoxShadowStyle box_shadow;
    int outline_width = 0;
    int outline_offset = 0;
    Color outline_color{0, 0, 0, 255};
    std::string overflow;
    bool overflow_wrap_anywhere = false;
    bool overflow_wrap_specified = false;
    bool white_space_nowrap = false;
    bool white_space_specified = false;
    bool text_overflow_ellipsis = false;
    float opacity = 1.0F;
    std::string transform;
    int transform_origin_x_percent = 50;
    int transform_origin_y_percent = 50;
    std::string position;
    int inset_top = 0;
    int inset_right = 0;
    int inset_bottom = 0;
    int inset_left = 0;
    bool inset_top_specified = false;
    bool inset_right_specified = false;
    bool inset_bottom_specified = false;
    bool inset_left_specified = false;
    int z_index = 0;
    bool z_index_auto = true;
    bool box_sizing_border_box = false;
    int column_gap = 0;
    int row_gap = 0;
    FlexDirection flex_direction = FlexDirection::Row;
    bool flex_wrap = false;
    AlignItems align_self = AlignItems::Auto;
    JustifyContent align_content = JustifyContent::Start;
    int flex_grow = 0;
    int flex_shrink = 1;
    int flex_basis = -1;
    std::int16_t flex_order = 0;
    int grid_min_track_width = -1;
    int grid_template_column_count = 0;
    std::array<int, 4> grid_template_column_widths{{0, 0, 0, 0}};
    std::array<std::int16_t, 4> grid_template_row_heights{{0, 0, 0, 0}};
    int grid_auto_row_min = 0;
    std::int16_t grid_column_start = -1;
    std::int16_t grid_row_start = -1;
    std::uint8_t grid_template_row_count = 0;
    std::uint8_t grid_column_span = 1;
    std::uint8_t grid_row_span = 1;
    ObjectFit object_fit = ObjectFit::Fill;
    ObjectPosition object_position;
    ImageRendering image_rendering = ImageRendering::Auto;
    ListStyleType list_style_type = ListStyleType::None;
    bool list_style_type_specified = false;
    GeneratedContentKind before_content_kind = GeneratedContentKind::None;
    std::string before_content_text;
    std::string before_counter_name;
    std::string before_counter_suffix;
    Color before_color{0, 0, 0, 255};
    bool before_color_specified = false;
    int before_font_weight = 400;
    bool before_font_weight_specified = false;
    int before_left = 0;
    bool before_left_specified = false;
    GeneratedContentKind after_content_kind = GeneratedContentKind::None;
    std::string after_content_text;
    std::string after_counter_name;
    std::string after_counter_suffix;
    Color after_color{0, 0, 0, 255};
    bool after_color_specified = false;
    int after_font_weight = 400;
    bool after_font_weight_specified = false;
    int after_left = 0;
    bool after_left_specified = false;
    TextAlign text_align = TextAlign::Start;
    bool text_align_specified = false;
    JustifyContent justify_content = JustifyContent::Start;
    AlignItems align_items = AlignItems::Stretch;
    std::vector<StyleTransition> transitions;
    std::vector<StyleAnimation> animations;
};

struct CssDeclaration {
    std::string property;
    std::string value;
    bool important = false;
};

struct CssSpecificity {
    int ids = 0;
    int classes = 0;
    int elements = 0;
};

enum class CssSelectorCombinator : std::uint8_t {
    Descendant,
    Child,
    AdjacentSibling,
    GeneralSibling,
};

struct CssSelectorPart {
    std::string compound;
    CssSelectorCombinator combinator_to_left = CssSelectorCombinator::Descendant;
};

struct CssRuleIndexKey {
    std::string id;
    std::string class_name;
    std::string tag_name;
    bool universal = false;
};

enum class CssRuleType {
    Style,
};

struct CssRule {
    CssRuleType type = CssRuleType::Style;
    std::string selector;
    CssPseudoElement pseudo_element = CssPseudoElement::None;
    std::vector<CssDeclaration> declarations;
    CssSpecificity specificity;
    std::vector<CssSelectorPart> selector_parts;
    CssRuleIndexKey index_key;
    std::size_t source_order = 0;
};

struct CssKeyframesRule {
    std::string name;
    std::vector<CssDeclaration> from_declarations;
    std::vector<CssDeclaration> to_declarations;
    std::size_t source_order = 0;
};

class CssStyleSheet {
public:
    using RuleList = std::vector<CssRule>;
    using KeyframesList = std::vector<CssKeyframesRule>;
    using iterator = RuleList::iterator;
    using const_iterator = RuleList::const_iterator;

    void push_back(CssRule rule);
    void push_keyframes(CssKeyframesRule rule);
    std::size_t size() const;
    std::size_t keyframes_size() const;
    bool empty() const;
    CssRule& operator[](std::size_t index);
    const CssRule& operator[](std::size_t index) const;
    iterator begin();
    iterator end();
    const_iterator begin() const;
    const_iterator end() const;
    const RuleList& rules() const;
    const KeyframesList& keyframes() const;
    const CssKeyframesRule* find_keyframes(std::string_view name) const;

private:
    RuleList rules_;
    KeyframesList keyframes_;
};

using Stylesheet = CssStyleSheet;

std::vector<CssSelectorPart> parse_css_selector_parts(std::string_view selector);
CssRuleIndexKey build_css_rule_index_key(const std::vector<CssSelectorPart>& selector_parts);
bool parse_css_transform_2d(std::string_view value, Transform2D& output);
std::string serialize_css_transform_2d(const Transform2D& transform);
bool apply_keyframe_declaration(Style& style, const CssDeclaration& declaration, DiagnosticSink* diagnostics = nullptr);

struct StyleResolverOptions {
    std::size_t max_candidate_cache_entries = 128;
    const Node* hovered_node = nullptr;
    const Node* active_node = nullptr;
    const Node* focused_node = nullptr;
    DiagnosticSink* diagnostics = nullptr;
    std::size_t max_background_image_resources = 128;
};

struct StyleResolverStatistics {
    std::size_t candidate_cache_entries = 0;
    std::size_t candidate_cache_rule_refs = 0;
    std::size_t candidate_cache_hits = 0;
    std::size_t candidate_cache_misses = 0;
    std::size_t candidate_cache_clears = 0;
    std::size_t candidate_cache_bypasses = 0;
};

struct InteractionInvalidationHints {
    bool hover = false;
    bool active = false;
    bool focus = false;
};

using CustomPropertyMap = std::unordered_map<std::string, std::string>;

struct StyleResolveContext {
    std::unordered_map<const Node*, const CustomPropertyMap*> custom_property_cache;
    std::vector<std::unique_ptr<CustomPropertyMap>> custom_property_scopes;
    std::unordered_map<const Node*, std::vector<const CssRule*>> matched_rule_cache;
    const Node* custom_property_scan_root = nullptr;
    bool has_inline_custom_properties = false;
};

class StyleResolver {
public:
    explicit StyleResolver(Stylesheet stylesheet, StyleResolverOptions options = {});

    Style resolve(const Node& node) const;
    Style resolve(const Node& node, StyleResolveContext& context) const;
    const CssKeyframesRule* keyframes(std::string_view name) const;
    const std::string* background_image_resource_url(std::uint16_t resource_id) const;
    std::uint16_t background_image_resource_id_for(std::string_view url) const;
    std::size_t background_image_resource_count() const;
    StyleResolverStatistics statistics() const;
    InteractionInvalidationHints interaction_invalidation_hints() const;
    void set_interaction_state(const Node* hovered_node, const Node* active_node, const Node* focused_node);

private:
    Stylesheet stylesheet_;
    StyleResolverOptions options_;
    std::unordered_map<std::string, std::vector<const CssRule*>> id_rules_;
    std::unordered_map<std::string, std::vector<const CssRule*>> class_rules_;
    std::unordered_map<std::string, std::vector<const CssRule*>> tag_rules_;
    std::vector<const CssRule*> universal_rules_;
    mutable std::unordered_map<std::string, std::vector<const CssRule*>> candidate_cache_;
    // Reused only when the bounded cache is full; it never becomes an entry.
    mutable std::vector<const CssRule*> uncached_candidates_;
    mutable StyleResolverStatistics statistics_;
    InteractionInvalidationHints interaction_hints_;
    bool has_custom_property_declarations_ = false;
    // Lazily populated only by a supported CSS url(). The id is packed in the
    // existing optional background overlay word, so pages without url() do not
    // grow Style or allocate a resource registry.
    mutable std::vector<std::string> background_image_resources_;

    void build_rule_index();
    const std::vector<const CssRule*>& candidate_rules_for(const Node& node) const;
    bool apply_custom_properties_for_node(CustomPropertyMap& inherited,
                                          const Node& node,
                                          const std::vector<const CssRule*>* matched_rules = nullptr) const;
    CustomPropertyMap custom_properties_for(const Node& node) const;
    const CustomPropertyMap& custom_properties_for(const Node& node, StyleResolveContext& context) const;
    const std::vector<const CssRule*>& matching_rules_for(const Node& node, StyleResolveContext& context) const;
    Style resolve_with_custom_properties(const Node& node,
                                         const CustomPropertyMap& custom_properties,
                                         const std::vector<const CssRule*>* matched_rules = nullptr) const;
};

} // namespace jellyframe
