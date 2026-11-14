#pragma once

#include "app_runtime/script_task_prepared_text.h"
#include "render_core/layer_tree.h"
#include "render_core/software_renderer.h"

#include <cstdint>
#include <memory>
#include <string_view>

namespace jellyframe {

// The source itself owns every callback context and image byte. Published sources
// are immutable: resolve(package_url) must keep returning the same handle and
// metadata for that resource until all provider handles drain, and paint must use
// those same bytes for that handle. This lets layout metadata lookup and later
// layer construction resolve independently against one snapshot without changing
// resource identity. Generation is defense against accidental sequential reset,
// not synchronization. Lazy caches must preserve handle identity and synchronize
// reads.
class RuntimeImageProviderSource {
public:
    virtual ~RuntimeImageProviderSource() = default;
    virtual std::uint64_t generation() const noexcept = 0;
    virtual bool contains(std::uint32_t image_handle) const noexcept = 0;
#if JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
    virtual bool resolve(std::string_view package_url, ResolvedImageResource& output) const = 0;
    virtual bool paint(FrameBuffer& target, const ImagePaintRequest& request) const = 0;
#endif
};

enum class RuntimeImageProviderStatus {
    Ready,
    InvalidSource,
    OutOfMemory,
    IdExhausted,
};

class RuntimeImageProvider final {
public:
    using Handle = std::shared_ptr<const RuntimeImageProvider>;
    RuntimeImageProvider(const RuntimeImageProvider&) = delete;
    RuntimeImageProvider& operator=(const RuntimeImageProvider&) = delete;

    // Refusal preserves output. Source must own resources, not wrap a borrowed
    // context behind an unrelated lifetime token.
    static RuntimeImageProviderStatus create(
        std::shared_ptr<const RuntimeImageProviderSource> source, Handle& output) noexcept;

    std::uint64_t id() const noexcept { return id_; }
    bool valid() const noexcept { return source_ && source_->generation() == generation_; }
    bool contains(std::uint32_t image_handle) const noexcept {
        return image_handle && valid() && source_->contains(image_handle);
    }
    ImagePainter painter() const noexcept;
#if JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
    bool resolve(std::string_view package_url, ResolvedImageResource& output) const;
#endif

private:
    RuntimeImageProvider() = default;
    std::shared_ptr<const RuntimeImageProviderSource> source_;
    std::uint64_t generation_ = 0;
    std::uint64_t id_ = 0;
};

} // namespace jellyframe
