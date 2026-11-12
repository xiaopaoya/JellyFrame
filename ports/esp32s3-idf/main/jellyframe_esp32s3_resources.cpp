#include "jellyframe_esp32s3_resources.h"

#include "render_core/css_parser.h"
#include "render_core/style.h"

#include <cctype>
#include <string>
#include <vector>

namespace jellyframe_esp32s3 {
namespace {

bool starts_with(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool is_local_url(std::string_view url) {
    return !url.empty() && url.find(':') == std::string_view::npos && !starts_with(url, "//");
}

std::string base_directory(std::string_view base_url) {
    if (base_url.empty() || base_url.front() != '/') {
        return "/";
    }
    const std::size_t slash = base_url.rfind('/');
    if (slash == std::string_view::npos || slash == 0) {
        return "/";
    }
    return std::string(base_url.substr(0, slash + 1));
}

bool normalize_absolute_path(std::string_view path, std::string& output) {
    if (path.empty() || path.front() != '/') {
        return false;
    }

    std::vector<std::string_view> segments;
    std::size_t index = 1;
    while (index <= path.size()) {
        const std::size_t next = path.find('/', index);
        const std::size_t end = next == std::string_view::npos ? path.size() : next;
        const std::string_view segment = path.substr(index, end - index);
        if (!segment.empty() && segment != ".") {
            if (segment == "..") {
                if (segments.empty()) {
                    return false;
                }
                segments.pop_back();
            } else {
                segments.push_back(segment);
            }
        }
        if (next == std::string_view::npos) {
            break;
        }
        index = next + 1;
    }

    output = "/";
    for (std::size_t segment_index = 0; segment_index < segments.size(); ++segment_index) {
        if (segment_index != 0) {
            output.push_back('/');
        }
        output.append(segments[segment_index]);
    }
    return true;
}

bool resolve_resource_url(std::string_view url, std::string_view base_url, std::string& output) {
    if (!is_local_url(url)) {
        return false;
    }
    if (url.front() == '/') {
        return normalize_absolute_path(url, output);
    }
    const std::string joined = base_directory(base_url) + std::string(url);
    return normalize_absolute_path(joined, output);
}

bool kind_matches(jellyframe::HostResourceKind requested, jellyframe::HostResourceKind available) {
    return requested == jellyframe::HostResourceKind::Other || requested == available;
}

void record_missing(ResourceBundleContext* context) {
    if (context != nullptr && context->stats != nullptr) {
        ++context->stats->missing_loads;
    }
}

void record_rejected(ResourceBundleContext* context) {
    if (context != nullptr && context->stats != nullptr) {
        ++context->stats->rejected_loads;
    }
}

void record_success(ResourceBundleContext* context, std::size_t bytes) {
    if (context != nullptr && context->stats != nullptr) {
        ++context->stats->successful_loads;
        context->stats->loaded_bytes += bytes;
    }
}

} // namespace

bool InstalledResourceSnapshot::rebuild_views() {
    try {
        views.clear();
        views.reserve(entries.size());
        total_bytes = 0;
        for (const InstalledResourceSnapshotEntry& entry : entries) {
            if (entry.url.empty() || entry.bytes.empty()) {
                return false;
            }
            views.push_back(ResourceEntry{entry.url, entry.kind, entry.bytes.data(), entry.bytes.size()});
            total_bytes += entry.bytes.size();
        }
    } catch (...) {
        clear();
        return false;
    }
    bundle = ResourceBundle{views.data(), views.size()};
    return true;
}

void InstalledResourceSnapshot::clear() {
    fonts.clear();
    entries.clear();
    views.clear();
    bundle = {};
    total_bytes = 0;
}

const ResourceBundle& default_resource_bundle() {
    return generated_resource_bundle();
}

ResourceBundleContext make_resource_context(const jellyframe::HostBudgets& budgets,
                                            std::string_view base_url,
                                            ResourceLoadStats* stats) {
    return ResourceBundleContext{&default_resource_bundle(), budgets.max_resource_bytes, base_url, stats};
}

ResourceBundleContext make_resource_context(const jellyframe::HostBudgets& budgets,
                                            std::string_view base_url,
                                            const ResourceBundle& bundle,
                                            ResourceLoadStats* stats) {
    return ResourceBundleContext{&bundle, budgets.max_resource_bytes, base_url, stats};
}

bool resolve_local_resource_url(std::string_view url,
                                std::string_view base_url,
                                std::string& output) {
    return resolve_resource_url(url, base_url, output);
}

bool snapshot_resource_entry_compatible(
    const std::vector<InstalledResourceSnapshotEntry>& entries,
    std::string_view url,
    jellyframe::HostResourceKind kind,
    bool& already_loaded) {
    already_loaded = false;
    for (const InstalledResourceSnapshotEntry& entry : entries) {
        if (entry.url != url) continue;
        if (entry.kind != kind) return false;
        already_loaded = true;
        return true;
    }
    return true;
}

bool stylesheet_rel_token(std::string_view rel) {
    std::string token;
    for (std::size_t index = 0; index <= rel.size(); ++index) {
        if (index == rel.size() || std::isspace(static_cast<unsigned char>(rel[index]))) {
            if (token == "stylesheet")
                return true;
            token.clear();
        } else
            token += static_cast<char>(std::tolower(static_cast<unsigned char>(rel[index])));
    }
    return false;
}

namespace {
// This walk only locates declaration boundaries. Core validates the complete
// prepared declaration; unsupported declarations retain their original bytes.
bool prepare_background(std::string_view declaration, std::string_view base_url,
                        std::string& output) {
    auto colon = declaration.find(':');
    std::string property;
    for (std::size_t index = 0; index < colon && index < declaration.size(); ++index) {
        if (declaration.substr(index, 2) == "/*") {
            auto token_end = declaration.find("*/", index + 2);
            if (token_end == std::string_view::npos)
                break;
            index = token_end + 1;
        } else if (!std::isspace(static_cast<unsigned char>(declaration[index])))
            property +=
                static_cast<char>(std::tolower(static_cast<unsigned char>(declaration[index])));
    }
    if (colon == std::string_view::npos ||
        (property != "background" && property != "background-image")) {
        output.append(declaration);
        return true;
    }
    std::string candidate(declaration.substr(0, colon + 1));
    bool changed = false;
    bool refused = false;
    for (std::size_t index = colon + 1; index < declaration.size();) {
        if (declaration.substr(index, 2) == "/*") {
            auto token_end = declaration.find("*/", index + 2);
            if (token_end == std::string_view::npos) {
                candidate.append(declaration.substr(index));
                break;
            }
            candidate.append(declaration.substr(index, token_end + 2 - index));
            index = token_end + 2;
            continue;
        }
        if (declaration[index] == '"' || declaration[index] == '\'') {
            auto begin = index;
            char quote = declaration[index++];
            while (index < declaration.size()) {
                if (declaration[index] == '\\' && index + 1 < declaration.size()) {
                    index += 2;
                    continue;
                }
                if (declaration[index++] == quote)
                    break;
            }
            candidate.append(declaration.substr(begin, index - begin));
            continue;
        }
        bool url = index + 4 <= declaration.size();
        for (std::size_t name_index = 0; url && name_index < 4; ++name_index)
            url = std::tolower(static_cast<unsigned char>(declaration[index + name_index])) ==
                  "url("[name_index];
        if (!url || (index > colon + 1 &&
                     (std::isalnum(static_cast<unsigned char>(declaration[index - 1])) ||
                      declaration[index - 1] == '-' || declaration[index - 1] == '_'))) {
            candidate += declaration[index++];
            continue;
        }
        std::size_t value_begin = index + 4;
        while (value_begin < declaration.size() &&
               std::isspace(static_cast<unsigned char>(declaration[value_begin])))
            ++value_begin;
        char quote = value_begin < declaration.size() &&
                             (declaration[value_begin] == '"' || declaration[value_begin] == '\'')
                         ? declaration[value_begin++]
                         : 0;
        std::size_t token_end = value_begin;
        while (token_end < declaration.size() && declaration[token_end] != (quote ? quote : ')'))
            ++token_end;
        if (token_end == declaration.size()) {
            output.append(declaration);
            return true;
        }
        auto value = declaration.substr(value_begin, token_end - value_begin);
        if (!quote)
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                value.remove_suffix(1);
        std::size_t close_index = token_end + (quote ? 1 : 0);
        while (close_index < declaration.size() &&
               std::isspace(static_cast<unsigned char>(declaration[close_index])))
            ++close_index;
        if (close_index == declaration.size() || declaration[close_index] != ')') {
            output.append(declaration);
            return true;
        }
        for (unsigned char c : value)
            if (c <= 32 || c == 127 || c == '\\' || c == '?' || c == '#' || c == '%' || c == '\'' ||
                c == '"' || c == '(' || c == ')')
                refused = true;
        std::string canonical;
        if (!resolve_local_resource_url(value, base_url, canonical))
            refused = true;
        if (refused)
            canonical = "/__refused_resource__.bmp";
        candidate += "url(\"" + canonical + "\")";
        changed = true;
        index = close_index + 1;
    }
    if (changed) {
        jellyframe::StyleResolver accepted(jellyframe::CssParser{}.parse("*{" + candidate + "}"));
        if (accepted.background_image_resource_count()) {
            if (refused)
                return false;
            output += candidate;
            return true;
        }
    }
    output.append(declaration);
    return true;
}
} // namespace

bool prepare_stylesheet_resources(std::string_view css, std::string_view base_url,
                                  std::string& output, std::size_t max_bytes) {
    output.clear();
    if (css.size() > max_bytes)
        return false;
    std::string prepared;
    prepared.reserve(css.size());
    std::size_t begin = 0;
    int parentheses = 0;
    int custom_braces = 0;
    for (std::size_t index = 0; index < css.size(); ++index) {
        if (css.substr(index, 2) == "/*") {
            auto token_end = css.find("*/", index + 2);
            if (token_end == std::string_view::npos)
                break;
            index = token_end + 1;
            continue;
        }
        if (css[index] == '"' || css[index] == '\'') {
            char quote = css[index++];
            for (; index < css.size(); ++index) {
                if (css[index] == '\\' && index + 1 < css.size()) {
                    ++index;
                    continue;
                }
                if (css[index] == quote)
                    break;
            }
            continue;
        }
        // Custom properties may contain arbitrary balanced blocks. Treat the
        // whole value as opaque, including apparent background declarations.
        if (custom_braces) {
            if (css[index] == '{')
                ++custom_braces;
            if (css[index] == '}')
                --custom_braces;
            continue;
        }
        if (css[index] == '{') {
            auto prefix = css.substr(begin, index - begin);
            for (;;) {
                while (!prefix.empty() && std::isspace(static_cast<unsigned char>(prefix.front())))
                    prefix.remove_prefix(1);
                if (prefix.substr(0, 2) != "/*")
                    break;
                const auto token_end = prefix.find("*/", 2);
                if (token_end == std::string_view::npos)
                    break;
                prefix.remove_prefix(token_end + 2);
            }
            if (prefix.substr(0, 2) == "--" && prefix.find(':') != std::string_view::npos) {
                custom_braces = 1;
                continue;
            }
        }
        if (css[index] == '(')
            ++parentheses;
        if (css[index] == ')' && parentheses)
            --parentheses;
        if (css[index] == '{') {
            prepared.append(css.substr(begin, index + 1 - begin));
            begin = index + 1;
            parentheses = 0;
        } else if ((css[index] == ';' && !parentheses) || css[index] == '}') {
            if (!prepare_background(css.substr(begin, index - begin), base_url, prepared))
                return false;
            prepared += css[index];
            begin = index + 1;
            parentheses = 0;
        }
        if (prepared.size() > max_bytes)
            return false;
    }
    if (!prepare_background(css.substr(begin), base_url, prepared) || prepared.size() > max_bytes)
        return false;
    output.swap(prepared);
    return true;
}

namespace {
bool prepare_document_style_node(jellyframe::Node& node, std::string_view base_url,
                                 std::size_t& prepared_bytes) {
    const std::string style = node.attribute("style");
    if (!style.empty()) {
        std::string prepared;
        if (!prepare_stylesheet_resources(style, base_url, prepared))
            return false;
        if (prepared.size() > 48u * 1024u - prepared_bytes) return false;
        prepared_bytes += prepared.size();
        node.set_attribute("style", std::move(prepared));
    }
    if (node.tag_name == "style") {
        for (auto& child : node.children) {
            if (child && child->type == jellyframe::NodeType::Text) {
                std::string prepared;
                if (!prepare_stylesheet_resources(child->text, base_url, prepared))
                    return false;
                if (prepared.size() > 48u * 1024u - prepared_bytes) return false;
                prepared_bytes += prepared.size();
                child->text.swap(prepared);
            }
        }
    }
    for (auto& child : node.children)
        if (child && !prepare_document_style_node(*child, base_url, prepared_bytes))
            return false;
    return true;
}

} // namespace

bool prepare_document_styles(jellyframe::Node& document, std::string_view base_url) {
    std::size_t prepared_bytes = 0;
    return prepare_document_style_node(document, base_url, prepared_bytes);
}

void collect_css_background_image_references(std::string css, std::string_view base_url,
                                             std::vector<InstalledResourceReference>& output) {
    std::string prepared;
    if (!prepare_stylesheet_resources(css, base_url, prepared))
        return;
    jellyframe::StyleResolver resolver(jellyframe::CssParser{}.parse(prepared));
    for (std::size_t id = 1; id <= resolver.background_image_resource_count(); ++id) {
        const std::string* url =
            resolver.background_image_resource_url(static_cast<std::uint16_t>(id));
        if (url != nullptr) {
            output.push_back(InstalledResourceReference{jellyframe::HostResourceKind::Image, *url,
                                                        std::string(base_url)});
        }
    }
}

bool load_resource(const jellyframe::HostResourceRequest& request, std::string& output,
                   void* raw_context) {
    output.clear();
    auto* context = static_cast<ResourceBundleContext*>(raw_context);
    if (context == nullptr || context->bundle == nullptr || context->bundle->entries == nullptr) {
        return false;
    }

    std::string resolved_url;
    const std::string_view base_url =
        request.base_url.empty() ? context->base_url : request.base_url;
    if (!resolve_resource_url(request.url, base_url, resolved_url)) {
        record_rejected(context);
        return false;
    }

    for (std::size_t index = 0; index < context->bundle->count; ++index) {
        const ResourceEntry &entry = context->bundle->entries[index];
        if (entry.url != resolved_url || !kind_matches(request.kind, entry.kind)) {
            continue;
        }
        if (entry.bytes == nullptr || entry.size == 0) {
            record_missing(context);
            return false;
        }
        if (context->max_resource_bytes > 0 && entry.size > context->max_resource_bytes) {
            record_rejected(context);
            return false;
        }
        output.assign(reinterpret_cast<const char*>(entry.bytes), entry.size);
        record_success(context, entry.size);
        return true;
    }

    record_missing(context);
    return false;
}

bool load_linked_stylesheet(std::string_view href, std::string& output, void* raw_context) {
    auto* context = static_cast<ResourceBundleContext*>(raw_context);
    const std::string_view base_url = context != nullptr ? context->base_url : std::string_view{};
    if (!load_resource(
        jellyframe::HostResourceRequest{jellyframe::HostResourceKind::Stylesheet, href, base_url},
        output, raw_context)) return false;
    if (context != nullptr && context->prepare_stylesheets) {
        std::string source, prepared;
        if (!resolve_local_resource_url(href, base_url, source) ||
            !prepare_stylesheet_resources(output, source, prepared, context->max_resource_bytes)) {
            output.clear(); record_rejected(context); return false;
        }
        output.swap(prepared);
    }
    return true;
}

bool load_classic_script(std::string_view src, std::string& output, void* raw_context) {
    auto* context = static_cast<ResourceBundleContext*>(raw_context);
    const std::string_view base_url = context != nullptr ? context->base_url : std::string_view{};
    return load_resource(
        jellyframe::HostResourceRequest{jellyframe::HostResourceKind::ClassicScript, src, base_url},
        output,
        raw_context);
}

} // namespace jellyframe_esp32s3
