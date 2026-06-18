#include "core/css_parser.h"
#include "core/html_parser.h"
#include "core/render_tree.h"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const RenderObject* find_first_by_tag(const RenderObject& object, const std::string& tag_name) {
    if (object.node != nullptr && object.node->type == NodeType::Element && object.node->tag_name == tag_name) {
        return &object;
    }
    for (const auto& child : object.children) {
        const RenderObject* found = find_first_by_tag(*child, tag_name);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

const RenderObject* find_first_text(const RenderObject& object) {
    if (object.type == RenderObjectType::Text) {
        return &object;
    }
    for (const auto& child : object.children) {
        const RenderObject* found = find_first_text(*child);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

void render_tree_filters_non_rendered_nodes() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<!doctype html><head><title>x</title><script>1 < 2</script></head>"
        "<body><form id='search'><input><button>Go</button></form><template><button>Hidden</button></template></body>");
    StyleResolver resolver(css_parser.parse("#search { width: 320px; } input { width: 240px; }"));
    RenderTreeBuilder builder(resolver);
    auto render_tree = builder.build(*document);

    check(find_first_by_tag(*render_tree, "head") == nullptr, "head skipped");
    check(find_first_by_tag(*render_tree, "script") == nullptr, "script skipped");
    check(find_first_by_tag(*render_tree, "template") == nullptr, "template skipped");
    check(find_first_by_tag(*render_tree, "form") != nullptr, "form rendered");
    check(find_first_by_tag(*render_tree, "input") != nullptr, "input rendered");
    check(find_first_by_tag(*render_tree, "button") != nullptr, "button rendered");
}

void render_tree_carries_computed_style_and_text_inheritance() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><button class='primary'>Go</button></body>");
    StyleResolver resolver(css_parser.parse(".primary { color: #2563eb; padding: 8px; }"));
    RenderTreeBuilder builder(resolver);
    auto render_tree = builder.build(*document);

    const RenderObject* button = find_first_by_tag(*render_tree, "button");
    const RenderObject* text = find_first_text(*render_tree);
    check(button != nullptr, "button object exists");
    check(button->style.padding.top == 8, "button computed padding");
    check(text != nullptr, "text object exists");
    check(text->style.color.b == 0xeb, "text inherited color");
}

void closed_dialog_is_not_rendered_by_default() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><dialog><p>Hidden</p></dialog><dialog open><p>Shown</p></dialog></body>");
    StyleResolver resolver(css_parser.parse(""));
    RenderTreeBuilder builder(resolver);
    auto render_tree = builder.build(*document);

    const RenderObject* dialog = find_first_by_tag(*render_tree, "dialog");
    check(dialog != nullptr, "open dialog rendered");
    check(dialog->node != nullptr && dialog->node->attributes.find("open") != dialog->node->attributes.end(),
          "first rendered dialog is open");
}

void hidden_attribute_skips_rendering() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><p id='gone' hidden>Gone</p><p id='shown'>Shown</p></body>");
    StyleResolver resolver(css_parser.parse(""));
    RenderTreeBuilder builder(resolver);
    auto render_tree = builder.build(*document);

    check(find_first_by_tag(*render_tree, "p") != nullptr, "visible paragraph rendered");
    const RenderObject* text = find_first_text(*render_tree);
    check(text != nullptr && text->node != nullptr && text->node->text == "Shown", "hidden text skipped");
}

void formatting_whitespace_text_is_skipped() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body>\n  <main>\n    <button>Go</button>\n  </main>\n</body>");
    StyleResolver resolver(css_parser.parse(""));
    RenderTreeBuilder builder(resolver);
    auto render_tree = builder.build(*document);

    int text_count = 0;
    const auto count_text = [&](const RenderObject& object, const auto& self) -> void {
        if (object.type == RenderObjectType::Text) {
            ++text_count;
        }
        for (const auto& child : object.children) {
            self(*child, self);
        }
    };
    count_text(*render_tree, count_text);
    check(text_count == 1, "formatting whitespace text skipped");

    auto pre_document = html_parser.parse("<body><pre>\n  keep\n</pre></body>");
    auto pre_render_tree = builder.build(*pre_document);
    const RenderObject* pre_text = find_first_text(*pre_render_tree);
    check(pre_text != nullptr && pre_text->node != nullptr && pre_text->node->text.find("  keep") != std::string::npos,
          "pre whitespace text preserved");
}

void render_tree_respects_object_budget() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><p>A</p><p>B</p><p>C</p></main></body>");
    StyleResolver resolver(css_parser.parse(""));
    RenderTreeBuilder builder(resolver, RenderTreeOptions{4});
    auto render_tree = builder.build(*document);

    int count = 0;
    const auto count_objects = [&](const RenderObject& root, const auto& self) -> void {
        ++count;
        for (const auto& child : root.children) {
            self(*child, self);
        }
    };
    count_objects(*render_tree, count_objects);
    check(count == 4, "render tree is capped by object budget");
}

void render_tree_can_use_monotonic_arena() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><p>A</p><p>B</p></main></body>");
    StyleResolver resolver(css_parser.parse("p { color: #2563eb; }"));
    RenderTreeBuilder builder(resolver);
    MonotonicArena arena(256);
    auto render_tree = builder.build(*document, arena);

    check(render_tree != nullptr, "arena render tree root exists");
    check(arena.used_bytes() > 0, "arena render tree consumes arena storage");
    check(find_first_by_tag(*render_tree, "main") != nullptr, "arena render tree contains element");
    check(find_first_text(*render_tree) != nullptr, "arena render tree contains text");
}

} // namespace

int main() {
    try {
        render_tree_filters_non_rendered_nodes();
        render_tree_carries_computed_style_and_text_inheritance();
        closed_dialog_is_not_rendered_by_default();
        hidden_attribute_skips_rendering();
        formatting_whitespace_text_is_skipped();
        render_tree_respects_object_budget();
        render_tree_can_use_monotonic_arena();
    } catch (const std::exception& error) {
        std::cerr << "render tree test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "render tree tests passed\n";
    return 0;
}
