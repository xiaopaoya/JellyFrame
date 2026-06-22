#pragma once

#include "render_core/dom.h"
#include "render_core/diagnostics.h"
#include "render_core/geometry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace jellyframe {

enum class Display {
    Block,
    Inline,
    InlineBlock,
    Flex,
    Grid,
    None,
};

enum class TextAlign {
    Start,
    Center,
    End,
};

enum class JustifyContent {
    Start,
    Center,
    SpaceAround,
    SpaceBetween,
};

enum class AlignItems {
    Stretch,
    Start,
    Center,
    End,
};

enum class ListStyleType {
    None,
    Disc,
    Decimal,
};

enum class GeneratedContentKind {
    None,
    Text,
    Counter,
};

enum class BackgroundPaintKind {
    Solid,
    LinearGradient,
};

enum class AnimatableProperty {
    All,
    Opacity,
    Transform,
    BackgroundColor,
    Color,
};

enum class AnimationTimingFunction {
    Linear,
    Ease,
    EaseIn,
    EaseOut,
    EaseInOut,
};

struct StyleTransition {
    AnimatableProperty property = AnimatableProperty::All;
    std::uint32_t duration_ms = 0;
    std::uint32_t delay_ms = 0;
    AnimationTimingFunction timing = AnimationTimingFunction::Ease;
};

enum class AnimationDirection {
    Normal,
    Alternate,
};

struct StyleAnimation {
    std::string name;
    std::uint32_t duration_ms = 0;
    std::uint32_t delay_ms = 0;
    AnimationTimingFunction timing = AnimationTimingFunction::Ease;
    std::uint16_t iteration_count = 1;
    bool infinite = false;
    AnimationDirection direction = AnimationDirection::Normal;
};

struct Transform2D {
    float translate_x = 0.0F;
    float translate_y = 0.0F;
    float scale_x = 1.0F;
    float scale_y = 1.0F;
};

struct Style {
    Display display = Display::Inline;
    Color color{0, 0, 0, 255};
    bool color_specified = false;
    BackgroundPaintKind background_paint = BackgroundPaintKind::Solid;
    GradientAxis background_gradient_axis = GradientAxis::Vertical;
    Color background_color{0, 0, 0, 0};
    Color background_color2{0, 0, 0, 0};
    EdgeSizes margin;
    bool margin_left_auto = false;
    bool margin_right_auto = false;
    EdgeSizes padding;
    EdgeSizes border_width;
    Color border_color{0, 0, 0, 255};
    int border_radius = 0;
    int width = -1;
    int height = -1;
    int min_width = -1;
    int min_height = -1;
    int max_width = -1;
    int aspect_ratio_width = 0;
    int aspect_ratio_height = 0;
    int font_size = 14;
    bool font_size_specified = false;
    int font_weight = 400;
    bool font_weight_specified = false;
    int line_height = -1;
    bool line_height_specified = false;
    int text_indent = 0;
    bool text_indent_specified = false;
    bool text_decoration_underline = false;
    bool text_decoration_line_through = false;
    bool text_decoration_specified = false;
    std::string text_shadow;
    bool text_shadow_specified = false;
    std::string box_shadow;
    int outline_width = 0;
    Color outline_color{0, 0, 0, 255};
    std::string overflow;
    float opacity = 1.0F;
    std::string transform;
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
    bool flex_wrap = false;
    int flex_grow = 0;
    int flex_shrink = 1;
    int flex_basis = -1;
    int grid_min_track_width = -1;
    int grid_template_column_count = 0;
    std::array<int, 4> grid_template_column_widths{{0, 0, 0, 0}};
    int grid_auto_row_min = 0;
    int grid_column_span = 1;
    int grid_row_span = 1;
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
    TextAlign text_align = TextAlign::Start;
    bool text_align_specified = false;
    JustifyContent justify_content = JustifyContent::Start;
    AlignItems align_items = AlignItems::Stretch;
    std::array<StyleTransition, 4> transitions{};
    std::size_t transition_count = 0;
    std::array<StyleAnimation, 4> animations{};
    std::size_t animation_count = 0;
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

enum class CssSelectorCombinator {
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
    bool pseudo_before = false;
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
};

struct StyleResolverStatistics {
    std::size_t candidate_cache_entries = 0;
    std::size_t candidate_cache_rule_refs = 0;
    std::size_t candidate_cache_hits = 0;
    std::size_t candidate_cache_misses = 0;
    std::size_t candidate_cache_clears = 0;
};

struct InteractionInvalidationHints {
    bool hover = false;
    bool active = false;
    bool focus = false;
};

class StyleResolver {
public:
    explicit StyleResolver(Stylesheet stylesheet, StyleResolverOptions options = {});

    Style resolve(const Node& node) const;
    const CssKeyframesRule* keyframes(std::string_view name) const;
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
    mutable StyleResolverStatistics statistics_;
    InteractionInvalidationHints interaction_hints_;
    bool has_custom_property_declarations_ = false;

    void build_rule_index();
    const std::vector<const CssRule*>& candidate_rules_for(const Node& node) const;
    std::unordered_map<std::string, std::string> custom_properties_for(const Node& node) const;
};

} // namespace jellyframe
