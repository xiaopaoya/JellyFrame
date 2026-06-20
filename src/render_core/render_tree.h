#pragma once

#include "render_core/arena.h"
#include "render_core/animation_timeline.h"
#include "render_core/dom.h"
#include "render_core/style.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace jellyframe {

struct RenderObject;

struct RenderObjectDeleter {
    bool arena_owned = false;
    void operator()(RenderObject* object) const;
};

using RenderObjectPtr = std::unique_ptr<RenderObject, RenderObjectDeleter>;

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
    std::vector<RenderObjectPtr> children;
};

struct RenderTreeOptions {
    std::size_t max_render_objects = 4096;
    DiagnosticSink* diagnostics = nullptr;
    const std::vector<StyleOverride>* style_overrides = nullptr;
};

class RenderTreeBuilder {
public:
    explicit RenderTreeBuilder(const StyleResolver& style_resolver, RenderTreeOptions options = {});

    RenderObjectPtr build(const Node& document) const;
    RenderObjectPtr build(const Node& document, MonotonicArena& arena) const;

private:
    const StyleResolver& style_resolver_;
    RenderTreeOptions options_;

    RenderObjectPtr build_with_arena(const Node& document, MonotonicArena* arena) const;
    RenderObjectPtr build_object(const Node& node,
                                 const Style* parent_style,
                                 std::size_t& render_object_count,
                                 bool& budget_reported,
                                 MonotonicArena* arena) const;
    RenderObjectPtr make_render_object(MonotonicArena* arena) const;
    RenderObjectType render_type_for(const Node& node, const Style& style) const;
};

std::size_t count_render_objects(const RenderObject& root);

} // namespace jellyframe
