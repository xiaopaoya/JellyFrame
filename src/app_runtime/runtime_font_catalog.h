#pragma once

#include "app_runtime/script_task_prepared_text.h"

#if JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
#include <memory>

namespace jellyframe {

enum class RuntimeFontStatus {
    Ready, InvalidRequest, InvalidResource, InvalidCatalog, ResourceLimit,
    OutOfMemory, IdExhausted
};

class RuntimeFontResource final {
public:
    using Handle = std::shared_ptr<const RuntimeFontResource>;
    RuntimeFontResource(const RuntimeFontResource&) = delete;
    RuntimeFontResource& operator=(const RuntimeFontResource&) = delete;

    // Complete JFFONT2 only. Inputs stay immutable for the call. Both factories
    // preserve output on refusal; an ID is published only with a ready resource.
    // Copy work is charged in addition to Core resource validation work.
    static RuntimeFontStatus copy(const std::uint8_t* bytes, std::size_t size,
                                  Handle& output, FontResourceLimits limits = {}) noexcept;
    // owner pins the entire immutable byte range through all resource handles.
    // No unpinned borrowed view is admitted. This supports flash/mapped assets
    // without copying their bytes; owner is a lifetime token, not font policy.
    static RuntimeFontStatus retain(const std::uint8_t* bytes, std::size_t size,
                                    const std::shared_ptr<const void>& owner,
                                    Handle& output, FontResourceLimits limits = {}) noexcept;
    std::uint64_t id() const noexcept { return id_; }
    std::size_t byte_size() const noexcept { return size_; }
    std::size_t owned_byte_size() const noexcept { return bytes_ ? size_ : 0; }
    const FontResource& resource() const noexcept { return resource_; }

private:
    RuntimeFontResource() noexcept = default;
    static RuntimeFontStatus create(const std::uint8_t*, std::size_t,
                                    const std::shared_ptr<const void>&, bool,
                                    Handle&, FontResourceLimits) noexcept;
    std::unique_ptr<std::uint8_t[]> bytes_;
    std::shared_ptr<const void> owner_;
    FontResource resource_;
    std::uint64_t id_ = 0;
    std::size_t size_ = 0;
};

struct RuntimeFontRegistration {
    RuntimeFontResource::Handle resource;
    std::uint32_t family_id = 0;
};

struct RuntimeFontCatalogLimits {
    TextSystemLimits text;
    std::size_t max_resource_bytes = 16U * 1024U * 1024U;
    std::size_t max_owned_bytes = 8U * 1024U * 1024U;
};

// Resource bytes, registrations and TextSystem share one immutable lifetime.
// Handles may be read concurrently. Publishing/swapping a current handle is
// the owning session's responsibility; no mutable global current catalog exists.
class RuntimeFontCatalog final {
public:
    using Handle = std::shared_ptr<const RuntimeFontCatalog>;
    RuntimeFontCatalog(const RuntimeFontCatalog&) = delete;
    RuntimeFontCatalog& operator=(const RuntimeFontCatalog&) = delete;
    static constexpr std::size_t max_faces = 64;

    static RuntimeFontStatus create(const RuntimeFontRegistration* registrations,
                                    std::size_t count, Handle& output,
                                    RuntimeFontCatalogLimits limits = {}) noexcept;
    const TextSystem& text_system() const noexcept { return system_; }
    std::size_t size() const noexcept { return count_; }
    std::size_t resource_bytes() const noexcept { return resource_bytes_; }
    std::size_t owned_bytes() const noexcept { return owned_bytes_; }
    std::uint64_t resource_id(std::size_t index) const noexcept {
        return index < count_ ? faces_[index].resource_id : 0;
    }
    // Owned registration value for composing a new immutable catalog. It
    // retains bytes; no borrowed array or mutable TextSystem escapes.
    RuntimeFontRegistration registration(std::size_t index) const noexcept {
        return index < count_ ? registrations_[index] : RuntimeFontRegistration{};
    }

private:
    RuntimeFontCatalog() noexcept = default;
    // Arrays avoid relocations of a published registration table. They also
    // avoid MSVC Debug vector's allocating noexcept iterator-proxy constructor.
    std::unique_ptr<RuntimeFontRegistration[]> registrations_;
    std::unique_ptr<TextFontFace[]> faces_;
    TextSystem system_;
    std::size_t count_ = 0, resource_bytes_ = 0, owned_bytes_ = 0;
};

} // namespace jellyframe
#endif
