#include "app_runtime/runtime_shaped_text_binding.h"

#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API

#if defined(ESP_PLATFORM)
#include "sdkconfig.h"
#endif

#if defined(ESP_PLATFORM) && (!defined(CONFIG_SPIRAM_USE_MALLOC) || !CONFIG_SPIRAM_USE_MALLOC)
#error "Installed shaped-text bindings require ESP-IDF PSRAM in the general allocator"
#endif

#include <new>
#include <algorithm>

namespace jellyframe {
namespace {

bool digest_present(const RuntimeSha256& digest) noexcept {
    for (const auto byte : digest) if (byte != 0) return true;
    return false;
}

runtime_shaped::Limits bounded_limits(runtime_shaped::Limits requested, const runtime_shaped::Limits& owner) {
    requested.bytes = std::min(requested.bytes, owner.bytes);
    requested.work = std::min(requested.work, owner.work);
    requested.packet_bytes = std::min(requested.packet_bytes, owner.packet_bytes);
    requested.scratch_bytes = std::min(requested.scratch_bytes, owner.scratch_bytes);
    requested.source_bytes = std::min(requested.source_bytes, owner.source_bytes);
    requested.input_scalars = std::min(requested.input_scalars, owner.input_scalars);
    requested.expanded_scalars = std::min(requested.expanded_scalars, owner.expanded_scalars);
    requested.clusters = std::min(requested.clusters, owner.clusters);
    requested.glyphs = std::min(requested.glyphs, owner.glyphs);
    return requested;
}

} // namespace

RuntimeShapedTextStatus RuntimeShapedTextBinding::create(
    const RuntimeShapedTextManifest& manifest, Handle& output,
    RuntimeShapedTextLimits limits) noexcept {
    if (manifest.font_resource.empty() || manifest.shaping_catalog.empty() ||
        manifest.shaping_companion.empty() || manifest.normalization_tables.empty() ||
        manifest.normalization_eligibility.empty() || manifest.resource_id == 0 ||
        manifest.raster_size == 0 || manifest.shaping_glyph_count == 0 ||
        !digest_present(manifest.font_resource_identity) ||
        !digest_present(manifest.source_identity) || !digest_present(manifest.profile_identity) ||
        !digest_present(manifest.catalog_identity) || !digest_present(manifest.companion_identity) ||
        !digest_present(manifest.tables_identity) || !digest_present(manifest.eligibility_identity))
        return RuntimeShapedTextStatus::InvalidRequest;

    try {
        std::shared_ptr<RuntimeShapedTextBinding> draft(new RuntimeShapedTextBinding);
        runtime_shaped::Inputs inputs;
        inputs.font_resource = manifest.font_resource;
        inputs.shaping_catalog = manifest.shaping_catalog;
        inputs.shaping_companion = manifest.shaping_companion;
        inputs.normalization_tables = manifest.normalization_tables;
        inputs.normalization_eligibility = manifest.normalization_eligibility;
        inputs.source_identity = manifest.source_identity;
        inputs.profile_identity = manifest.profile_identity;
        inputs.catalog_identity = manifest.catalog_identity;
        inputs.companion_identity = manifest.companion_identity;
        inputs.tables_identity = manifest.tables_identity;
        inputs.eligibility_identity = manifest.eligibility_identity;
        inputs.shaping_glyph_count = manifest.shaping_glyph_count;
        inputs.resource_id = manifest.resource_id;
        inputs.raster_size = manifest.raster_size;
        inputs.weight = manifest.weight;
        inputs.selection = manifest.selection;
        #if JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API
        inputs.font_resource_identity = manifest.font_resource_identity;
        const auto& inputs_v2 = inputs;
#else
        runtime_shaped::InputsV2 inputs_v2{inputs, manifest.font_resource_identity};
#endif

        // Core treats the incoming counter as remaining budget and restores it
        // on refusal, so start at the configured limit.
        std::uint64_t remaining_work = limits.core.work;
        const auto status = runtime_shaped::create_binding(inputs_v2, limits.core, remaining_work,
                                                        draft->binding_);
        if (status != runtime_shaped::Status::ok) {
            return status == runtime_shaped::Status::limit
                ? RuntimeShapedTextStatus::ResourceLimit
                : status == runtime_shaped::Status::allocation
                    ? RuntimeShapedTextStatus::OutOfMemory
                    : RuntimeShapedTextStatus::CoreRejected;
        }
        draft->limits_ = limits.core;
        output = std::move(draft);
        return RuntimeShapedTextStatus::Ready;
    } catch (const std::bad_alloc&) {
        return RuntimeShapedTextStatus::OutOfMemory;
    }
}

runtime_shaped::Status RuntimeShapedTextBinding::prepare(
    std::int32_t size_q1000, std::string_view text, std::uint64_t& work,
    std::string& packet, runtime_shaped::Metrics& metrics) const noexcept {
    return runtime_shaped::prepare(binding_, size_q1000, text, limits_, work, packet, metrics);
}

#if JELLYFRAME_RUNTIME_SHAPED_PARAGRAPH_API
runtime_shaped::Status RuntimeShapedTextBinding::layout_paragraph(
    std::string_view source, const runtime_shaped::ParagraphOptions& options,
    const runtime_shaped::ParagraphLimits& requested, std::uint64_t& work,
    runtime_shaped::Paragraph& output, runtime_shaped::ParagraphUsage* usage) const noexcept {
    auto limits = requested;
    limits.shaping = bounded_limits(limits.shaping, limits_);
    return runtime_shaped::layout_paragraph(binding_, source, options, limits, work, output, usage);
}
#endif

#if JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
runtime_shaped::Status RuntimeShapedTextBinding::layout_document(const LayoutEngine& engine,
    const RenderObject& root, int width, int height, const runtime_shaped::LayoutOptions& requested,
    std::uint64_t& work, runtime_shaped::LayoutResult& output, runtime_shaped::LayoutUsage* usage) const noexcept {
    auto options = requested;
    options.binding = binding_;
    options.paragraph.shaping = bounded_limits(options.paragraph.shaping, limits_);
    #if JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API
    return engine.layout_contextual(root, width, height, options, work, output, usage);
#else
    return engine.layout_shaped(root, width, height, options, work, output, usage);
#endif
}
#endif

runtime_shaped::Status RuntimeShapedTextBinding::validate_packet(
    std::string_view packet, std::uint64_t& work) const noexcept {
    return runtime_shaped::validate_packet(binding_, packet, limits_, work);
}

} // namespace jellyframe

#endif
