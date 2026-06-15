#include "jellyframe_esp32s3_resources.h"

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

const ResourceBundle& default_resource_bundle() {
    return generated_resource_bundle();
}

ResourceBundleContext make_resource_context(const jellyframe::HostBudgets& budgets,
                                            std::string_view base_url,
                                            ResourceLoadStats* stats) {
    return ResourceBundleContext{&default_resource_bundle(), budgets.max_resource_bytes, base_url, stats};
}

bool load_resource(const jellyframe::HostResourceRequest& request,
                   std::string& output,
                   void* raw_context) {
    output.clear();
    auto* context = static_cast<ResourceBundleContext*>(raw_context);
    if (context == nullptr || context->bundle == nullptr || context->bundle->entries == nullptr) {
        return false;
    }

    std::string resolved_url;
    const std::string_view base_url = request.base_url.empty() ? context->base_url : request.base_url;
    if (!resolve_resource_url(request.url, base_url, resolved_url)) {
        record_rejected(context);
        return false;
    }

    for (std::size_t index = 0; index < context->bundle->count; ++index) {
        const ResourceEntry& entry = context->bundle->entries[index];
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
    return load_resource(
        jellyframe::HostResourceRequest{jellyframe::HostResourceKind::Stylesheet, href, base_url},
        output,
        raw_context);
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
