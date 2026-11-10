#include "app_runtime/runtime_image_provider.h"

#include <atomic>
#include <new>

namespace jellyframe {

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
        return provider.contains(request.image_handle) && provider.source_->paint(target, request);
    }, const_cast<RuntimeImageProvider*>(this), false};
#else
    return {};
#endif
}

#if JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
bool RuntimeImageProvider::resolve(std::string_view package_url, ResolvedImageResource& output) const {
    output = {};
    if (package_url.empty() || !valid()) return false;
    ResolvedImageResource candidate;
    if (!source_->resolve(package_url, candidate) || !contains(candidate.image_handle)) return false;
    output = candidate;
    return true;
}
#endif

} // namespace jellyframe
