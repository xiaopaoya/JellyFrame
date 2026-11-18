#include "app_runtime/runtime_image_provider.h"

#include <atomic>
#include <new>

namespace jellyframe {

#if JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API && !JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
namespace {
RuntimeImagePaintRequest integer_image_request(Rect rect, Rect clip, std::uint32_t handle,
                                               ObjectFit fit, ObjectPosition position, ImageRendering rendering) {
    return {rect, rect, {{}, {}, {rect.width, 1}, {rect.height, 1}}, clip, handle, fit, position, rendering};
}
} // namespace
#endif

RuntimeImageProviderStatus RuntimeImageProvider::create(
    std::shared_ptr<const RuntimeImageProviderSource> source, Handle& output) noexcept {
    if (!source) return RuntimeImageProviderStatus::InvalidSource;
    const auto source_generation = source->generation();
    if (!source_generation) return RuntimeImageProviderStatus::InvalidSource;
    try {
        auto candidate = std::shared_ptr<RuntimeImageProvider>(new RuntimeImageProvider);
        // Zero is permanent exhaustion. A destroyed/recreated provider cannot
        // reuse a namespace, even when its source reuses numeric image handles.
        static std::atomic<std::uint64_t> next_id{1};
        auto id = next_id.load(std::memory_order_relaxed);
        do {
            if (!id) return RuntimeImageProviderStatus::IdExhausted;
        } while (!next_id.compare_exchange_weak(id, id == UINT64_MAX ? 0 : id + 1,
                                               std::memory_order_relaxed));
        candidate->id_ = id;
        candidate->generation_ = source_generation;
        candidate->source_ = std::move(source);
        if (!candidate->valid()) return RuntimeImageProviderStatus::InvalidSource;
        output = std::move(candidate);
        return RuntimeImageProviderStatus::Ready;
    } catch (const std::bad_alloc&) {
        return RuntimeImageProviderStatus::OutOfMemory;
    }
}

ImagePainter RuntimeImageProvider::painter() const noexcept {
#if JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
    return {[](FrameBuffer& target, const ImagePaintRequest& request, void* context) {
        const auto& provider = *static_cast<const RuntimeImageProvider*>(context);
        return provider.paint(target, request);
    }, const_cast<RuntimeImageProvider*>(this), false};
#elif JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API
    ImagePainter result;
    result.context = const_cast<RuntimeImageProvider*>(this);
    result.paint = [](FrameBuffer& target, Rect rect, std::uint32_t handle, ObjectFit fit,
                      ObjectPosition position, ImageRendering rendering, void* context) {
        const auto& provider = *static_cast<const RuntimeImageProvider*>(context);
        return provider.paint(target, integer_image_request(rect, rect, handle, fit, position, rendering));
    };
    result.paint_clipped = [](FrameBuffer& target, Rect rect, Rect clip, std::uint32_t handle,
                              ObjectFit fit, ObjectPosition position, ImageRendering rendering, void* context) {
        const auto& provider = *static_cast<const RuntimeImageProvider*>(context);
        return provider.paint(target, integer_image_request(rect, clip, handle, fit, position, rendering));
    };
    result.paint_fractional_clipped = [](FrameBuffer& target, const ImagePaintFractionalRequest& request,
                                         void* context) {
        const auto& provider = *static_cast<const RuntimeImageProvider*>(context);
        return provider.paint(target, request);
    };
    result.writes_only_within_rect = true;
    result.supports_tiled_replay = true;
    return result;
#else
    return {};
#endif
}

#if JELLYFRAME_RUNTIME_IMAGE_API
bool RuntimeImageProvider::resolve(std::string_view package_url, RuntimeResolvedImage& output) const {
    output = {};
    if (package_url.empty() || !valid()) return false;
    RuntimeResolvedImage candidate;
    if (!source_->resolve(package_url, candidate) || !contains(candidate.image_handle)) return false;
    output = candidate;
    return true;
}

bool RuntimeImageProvider::paint(FrameBuffer& target, const RuntimeImagePaintRequest& request) const {
    return contains(request.image_handle) && source_->paint(target, request);
}
#endif

#if JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API && !JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
bool RuntimeImageProvider::intrinsic_size(std::uint32_t handle, int& width, int& height) const {
    int w = 0, h = 0;
    if (!contains(handle) || !source_->intrinsic_size(handle, w, h) || w <= 0 || h <= 0) return false;
    width = w;
    height = h;
    return true;
}
#endif

} // namespace jellyframe
