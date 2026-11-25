#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#if defined(JELLYFRAME_RENDER_CORE_CONTEXTUAL_TEXT_ENABLED) && JELLYFRAME_RENDER_CORE_CONTEXTUAL_TEXT_ENABLED
#include "render_core/contextual_text.h"
#include "render_core/contextual_text_paragraph.h"
#if JELLYFRAME_RENDER_CORE_CONTEXTUAL_LAYOUT_ENABLED
#include "render_core/contextual_text_layout.h"
#include "render_core/input.h"
#endif
#define JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API 1
#define JELLYFRAME_RUNTIME_SHAPED_TEXT_API 1
#define JELLYFRAME_RUNTIME_SHAPED_PARAGRAPH_API 1
#define JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API JELLYFRAME_RENDER_CORE_CONTEXTUAL_LAYOUT_ENABLED
#elif defined(JELLYFRAME_RENDER_CORE_TEXT_SHAPING_ENABLED) && JELLYFRAME_RENDER_CORE_TEXT_SHAPING_ENABLED && __has_include("render_core/shaped_text_binding.h")
#include "render_core/shaped_text_binding.h"
#if __has_include("render_core/shaped_text_paragraph.h")
#include "render_core/shaped_text_paragraph.h"
#endif
#if __has_include("render_core/shaped_text_layout.h")
#include "render_core/shaped_text_layout.h"
#endif
#define JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API 0
#define JELLYFRAME_RUNTIME_SHAPED_TEXT_API 1
#ifdef JELLYFRAME_SHAPED_TEXT_PARAGRAPH_API_VERSION
#define JELLYFRAME_RUNTIME_SHAPED_PARAGRAPH_API 1
#else
#define JELLYFRAME_RUNTIME_SHAPED_PARAGRAPH_API 0
#endif
#ifdef JELLYFRAME_SHAPED_TEXT_LAYOUT_API_VERSION
#define JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API 1
#else
#define JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API 0
#endif
#else
#define JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API 0
#define JELLYFRAME_RUNTIME_SHAPED_TEXT_API 0
#define JELLYFRAME_RUNTIME_SHAPED_PARAGRAPH_API 0
#define JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API 0
#endif

#if JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API && JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API && \
    defined(JELLYFRAME_CONTEXTUAL_SHARED_INLINE_API_VERSION) && JELLYFRAME_CONTEXTUAL_SHARED_INLINE_API_VERSION == 1 && \
    defined(JELLYFRAME_CONTEXTUAL_HIT_TEST_API_VERSION) && JELLYFRAME_CONTEXTUAL_HIT_TEST_API_VERSION == 1 && \
    defined(JELLYFRAME_CONTEXTUAL_INPUT_CONTROLLER_API_VERSION) && JELLYFRAME_CONTEXTUAL_INPUT_CONTROLLER_API_VERSION == 1
#define JELLYFRAME_RUNTIME_CONTEXTUAL_INLINE_INPUT_API 1
#else
#define JELLYFRAME_RUNTIME_CONTEXTUAL_INLINE_INPUT_API 0
#endif

namespace jellyframe {
#if JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API
namespace runtime_shaped = contextual_text;
#elif JELLYFRAME_RUNTIME_SHAPED_TEXT_API
namespace runtime_shaped = shaped_text;
#endif
// Protocol identity is selected with the producer, never inferred from magic.
inline constexpr std::uint8_t kRuntimeShapedFrameVersion = JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API ? 12 : 11;
inline bool runtime_shaped_packet_header_matches(std::string_view packet) noexcept {
    return packet.size() >= 8 && packet.substr(0, 4) == "JFSP" &&
        // The qualified ABI4 shaped candidate also exports JFSP2. Its older
        // manifest label does not identify the packet version or Core ABI.
        static_cast<unsigned char>(packet[4]) == 2 &&
        packet[5] == 0;
}


using RuntimeSha256 = std::array<unsigned char, 32>;

// Immutable package resource views. Digests must come from the package
// verifier; this value does not derive identity from untrusted asset paths or
// bytes. The bundle owner must keep the views immutable through this call.
struct RuntimeShapedTextManifest {
    std::string_view font_resource;
    std::string_view shaping_catalog;
    std::string_view shaping_companion;
    std::string_view normalization_tables;
    std::string_view normalization_eligibility;
    RuntimeSha256 font_resource_identity{};
    RuntimeSha256 source_identity{};
    RuntimeSha256 profile_identity{};
    RuntimeSha256 catalog_identity{};
    RuntimeSha256 companion_identity{};
    RuntimeSha256 tables_identity{};
    RuntimeSha256 eligibility_identity{};
    std::uint32_t shaping_glyph_count = 0;
    std::uint64_t resource_id = 0;
    std::uint16_t raster_size = 0;
    std::uint16_t weight = 0;
    std::uint16_t selection = 0;
};

enum class RuntimeShapedTextStatus {
    Ready,
    InvalidRequest,
    Unsupported,
    CoreRejected,
    ResourceLimit,
    OutOfMemory
};

#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API
struct RuntimeShapedTextLimits {
    runtime_shaped::Limits core;
};

// Pins Core's immutable validated copy of every package asset. A handle is
// published only when Core accepts all bytes, identities and selection.
class RuntimeShapedTextBinding final {
public:
    using Handle = std::shared_ptr<const RuntimeShapedTextBinding>;
    RuntimeShapedTextBinding(const RuntimeShapedTextBinding&) = delete;
    RuntimeShapedTextBinding& operator=(const RuntimeShapedTextBinding&) = delete;

    static RuntimeShapedTextStatus create(const RuntimeShapedTextManifest& manifest,
                                          Handle& output,
                                          RuntimeShapedTextLimits limits = {}) noexcept;
    bool valid() const noexcept { return binding_.valid(); }
    const runtime_shaped::Binding& core_binding() const noexcept { return binding_; }
    std::uint64_t work_limit() const noexcept { return limits_.work; }
    runtime_shaped::Status prepare(std::int32_t size_q1000, std::string_view text,
                                std::uint64_t& work, std::string& packet,
                                runtime_shaped::Metrics& metrics) const noexcept;
#if JELLYFRAME_RUNTIME_SHAPED_PARAGRAPH_API
    // Clamp per-paragraph shaping limits to this authenticated owner's limits.
    // The resulting paragraph owns a Core Binding, independent of this wrapper.
    runtime_shaped::Status layout_paragraph(std::string_view source,
        const runtime_shaped::ParagraphOptions&, const runtime_shaped::ParagraphLimits&,
        std::uint64_t& work, runtime_shaped::Paragraph& output,
        runtime_shaped::ParagraphUsage* usage = nullptr) const noexcept;
#endif
#if JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
    runtime_shaped::Status layout_document(const LayoutEngine&, const RenderObject&,
        int width, int height, const runtime_shaped::LayoutOptions&, std::uint64_t& work,
        runtime_shaped::LayoutResult&, runtime_shaped::LayoutUsage* usage = nullptr) const noexcept;
#if JELLYFRAME_RUNTIME_CONTEXTUAL_INLINE_INPUT_API
    runtime_shaped::Status layout_inline_document(const LayoutEngine&, const RenderObject&,
        int width, int height, const runtime_shaped::LayoutOptions&, std::uint64_t& work,
        runtime_shaped::LayoutResult&) const noexcept;
#endif
#endif
    runtime_shaped::Status validate_packet(std::string_view packet,
                                        std::uint64_t& work) const noexcept;

private:
    RuntimeShapedTextBinding() noexcept = default;
    runtime_shaped::Binding binding_;
    runtime_shaped::Limits limits_;
};
#endif

} // namespace jellyframe
