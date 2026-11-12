#include "app_runtime/runtime_font_catalog.h"

#if JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
#include "app_runtime/runtime_font_id.h"
#include <cstring>
#include <new>

namespace jellyframe {
namespace {
detail::RuntimeFontIdSource resource_ids;
RuntimeFontStatus font_status(FontResourceStatus status) noexcept {
    return status == FontResourceStatus::ResourceLimit ? RuntimeFontStatus::ResourceLimit : RuntimeFontStatus::InvalidResource;
}
} // namespace

RuntimeFontStatus RuntimeFontResource::copy(const std::uint8_t* bytes, std::size_t size,
                                           Handle& output, FontResourceLimits limits) noexcept {
    return create(bytes, size, {}, true, output, limits);
}
RuntimeFontStatus RuntimeFontResource::retain(const std::uint8_t* bytes, std::size_t size,
                                             const std::shared_ptr<const void>& owner,
                                             Handle& output, FontResourceLimits limits) noexcept {
    if (!owner) return RuntimeFontStatus::InvalidRequest;
    return create(bytes, size, owner, false, output, limits);
}
RuntimeFontStatus RuntimeFontResource::create(const std::uint8_t* bytes, std::size_t size,
                                             const std::shared_ptr<const void>& owner, bool copy_bytes,
                                             Handle& output, FontResourceLimits limits) noexcept {
    if (!bytes || !size) return RuntimeFontStatus::InvalidRequest;
    if (size > limits.max_bytes || (copy_bytes && size > limits.max_work)) return RuntimeFontStatus::ResourceLimit;
    try {
        std::shared_ptr<RuntimeFontResource> draft(new RuntimeFontResource);
        if (copy_bytes) {
            draft->bytes_.reset(new std::uint8_t[size]);
            std::memcpy(draft->bytes_.get(), bytes, size);
            bytes = draft->bytes_.get();
            limits.max_work -= size;
        } else {
            draft->owner_ = owner;
        }
        const auto status = draft->resource_.load(bytes, size, limits);
        if (status != FontResourceStatus::Loaded) return font_status(status);
        if (!resource_ids.take(draft->id_)) return RuntimeFontStatus::IdExhausted;
        draft->size_ = size;
        output = std::move(draft);
        return RuntimeFontStatus::Ready;
    } catch (const std::bad_alloc&) {
        return RuntimeFontStatus::OutOfMemory;
    }
}

RuntimeFontStatus RuntimeFontCatalog::create(const RuntimeFontRegistration* registrations,
                                            std::size_t count, Handle& output,
                                            RuntimeFontCatalogLimits limits) noexcept {
    if (!registrations || !count) return RuntimeFontStatus::InvalidRequest;
    if (count > max_faces || count > limits.text.max_faces) return RuntimeFontStatus::ResourceLimit;
    std::size_t bytes = 0, owned = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& resource = registrations[i].resource;
        if (!resource) return RuntimeFontStatus::InvalidRequest;
        if (resource->byte_size() > limits.max_resource_bytes - bytes ||
            resource->owned_byte_size() > limits.max_owned_bytes - owned) return RuntimeFontStatus::ResourceLimit;
        bytes += resource->byte_size();owned += resource->owned_byte_size();
    }
    try {
        std::shared_ptr<RuntimeFontCatalog> draft(new RuntimeFontCatalog);
        draft->registrations_.reset(new RuntimeFontRegistration[count]);
        draft->faces_.reset(new TextFontFace[count]);
        for (std::size_t i = 0; i < count; ++i) {
            draft->registrations_[i] = registrations[i];
            draft->faces_[i] = {registrations[i].resource->id(), registrations[i].family_id,
                                registrations[i].resource->resource(), registrations[i].css_family_hash};
        }
        const auto status = draft->system_.bind(draft->faces_.get(), count, limits.text);
        if (status != TextSystemStatus::Ok) return status == TextSystemStatus::ResourceLimit
            ? RuntimeFontStatus::ResourceLimit : RuntimeFontStatus::InvalidCatalog;
        draft->count_ = count;draft->resource_bytes_ = bytes;draft->owned_bytes_ = owned;
        output = std::move(draft);
        return RuntimeFontStatus::Ready;
    } catch (const std::bad_alloc&) {
        return RuntimeFontStatus::OutOfMemory;
    }
}
} // namespace jellyframe
#endif
