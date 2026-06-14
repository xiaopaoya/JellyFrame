#pragma once

#include "core/dom.h"
#include "core/geometry.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace wearweb {

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

struct Style {
    Display display = Display::Inline;
    Color color{0, 0, 0, 255};
    bool color_specified = false;
    Color background_color{0, 0, 0, 0};
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
    int line_height = -1;
    bool line_height_specified = false;
    int text_indent = 0;
    bool text_indent_specified = false;
    std::string box_shadow;
    std::string overflow;
    float opacity = 1.0F;
    std::string transform;
    std::string position;
    int z_index = 0;
    bool z_index_auto = true;
    bool box_sizing_border_box = false;
    int column_gap = 0;
    int row_gap = 0;
    int grid_min_track_width = -1;
    int grid_auto_row_min = 0;
    int grid_column_span = 1;
    int grid_row_span = 1;
    TextAlign text_align = TextAlign::Start;
    bool text_align_specified = false;
    JustifyContent justify_content = JustifyContent::Start;
    AlignItems align_items = AlignItems::Stretch;
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
    std::vector<CssDeclaration> declarations;
    CssSpecificity specificity;
    std::vector<CssSelectorPart> selector_parts;
    CssRuleIndexKey index_key;
    std::size_t source_order = 0;
};

class CssStyleSheet {
public:
    using RuleList = std::vector<CssRule>;
    using iterator = RuleList::iterator;
    using const_iterator = RuleList::const_iterator;

    void push_back(CssRule rule);
    std::size_t size() const;
    bool empty() const;
    CssRule& operator[](std::size_t index);
    const CssRule& operator[](std::size_t index) const;
    iterator begin();
    iterator end();
    const_iterator begin() const;
    const_iterator end() const;
    const RuleList& rules() const;

private:
    RuleList rules_;
};

using Stylesheet = CssStyleSheet;

std::vector<CssSelectorPart> parse_css_selector_parts(std::string_view selector);
CssRuleIndexKey build_css_rule_index_key(const std::vector<CssSelectorPart>& selector_parts);

class StyleResolver {
public:
    explicit StyleResolver(Stylesheet stylesheet);

    Style resolve(const Node& node) const;

private:
    Stylesheet stylesheet_;
    std::unordered_map<std::string, std::vector<const CssRule*>> id_rules_;
    std::unordered_map<std::string, std::vector<const CssRule*>> class_rules_;
    std::unordered_map<std::string, std::vector<const CssRule*>> tag_rules_;
    std::vector<const CssRule*> universal_rules_;

    void build_rule_index();
};

} // namespace wearweb
