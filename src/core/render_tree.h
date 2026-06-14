#pragma once

#include "core/dom.h"
#include "core/style.h"

#include <memory>
#include <vector>

namespace wearweb {

enum class RenderObjectType {
    View,
    Block,
    Inline,
    Text,
};

struct RenderObject {
    RenderObjectType type = RenderObjectType::Block;
    const Node* node = nullptr;
    Style style;
    std::vector<std::unique_ptr<RenderObject>> children;
};

class RenderTreeBuilder {
public:
    explicit RenderTreeBuilder(const StyleResolver& style_resolver);

    std::unique_ptr<RenderObject> build(const Node& document) const;

private:
    const StyleResolver& style_resolver_;

    std::unique_ptr<RenderObject> build_object(const Node& node, const Style* parent_style) const;
    RenderObjectType render_type_for(const Node& node, const Style& style) const;
};

} // namespace wearweb

