#include "jellyframe_esp32s3_installed_resource_owner.h"

#if JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
#include "app_runtime/script_task_frame_renderer.h"
#include "script/script_task_worker_runtime.h"

namespace jellyframe_esp32s3 {

void InstalledAppResources::bind_worker_resources(
    jellyframe::ScriptTaskWorkerRuntimeOptions& options) const noexcept {
    options.text_catalog = text_catalog;
    options.image_provider = image_provider;
}

void InstalledAppResources::bind_ui_resources(
    jellyframe::ScriptTaskFrameRendererOptions& options) const noexcept {
    options.text_catalog = text_catalog;
    options.image_provider = image_provider;
}

} // namespace jellyframe_esp32s3
#endif

#if JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API

#include <array>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include "render_core/text_backend.h"

namespace jellyframe_esp32s3 {
namespace {

class InstalledSnapshotImageSource final : public jellyframe::RuntimeImageProviderSource {
public:
    InstalledSnapshotImageSource(std::shared_ptr<const InstalledResourceSnapshot> snapshot,
                                 const jellyframe::HostBudgets& budgets,
                                 std::string_view base_url,
                                 BmpImageAdapter::Clock clock)
        : snapshot_(std::move(snapshot)), base_url_(base_url) {
        adapter_.configure(budgets, base_url_, snapshot_ ? &snapshot_->bundle : nullptr, clock);
    }

    std::uint64_t generation() const noexcept override { return 1; }

    bool contains(std::uint32_t handle) const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t i = 0; i < handle_count_; ++i) if (handles_[i] == handle) return true;
        return false;
    }

    bool resolve(std::string_view package_url,
                 jellyframe::ResolvedImageResource& output) const override {
        output = {};
        if (package_url.empty() || package_url.size() > 256) return false;
        try {
            std::string resolved_url;
            if (!resolve_local_resource_url(package_url, base_url_, resolved_url)) return false;
            std::lock_guard<std::mutex> lock(mutex_);
            jellyframe::Node image(jellyframe::NodeType::Element);
            image.tag_name = "img";
            image.attributes["src"] = std::move(resolved_url);
            if (!adapter_.resolve(image, jellyframe::ImageResolveKind::Content, 0, output)) return false;
            for (std::size_t i = 0; i < handle_count_; ++i) if (handles_[i] == output.image_handle) return true;
            if (handle_count_ == handles_.size()) { output = {}; return false; }
            handles_[handle_count_++] = output.image_handle;
            return true;
        } catch (const std::bad_alloc&) {
            output = {};
            return false;
        }
    }

    bool preflight() const {
        if (!snapshot_) return false;
        for (const InstalledResourceSnapshotEntry& entry : snapshot_->entries) {
            if (entry.kind != jellyframe::HostResourceKind::Image) continue;
            jellyframe::ResolvedImageResource resolved;
            if (!resolve(entry.url, resolved)) return false;
        }
        return true;
    }

    bool paint(jellyframe::FrameBuffer& target,
               const jellyframe::ImagePaintRequest& request) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        bool known = false;
        for (std::size_t i = 0; i < handle_count_; ++i) known = known || handles_[i] == request.image_handle;
        return known && adapter_.paint(target, request);
    }

private:
    std::shared_ptr<const InstalledResourceSnapshot> snapshot_;
    std::string base_url_;
    mutable std::mutex mutex_;
    mutable BmpImageAdapter adapter_;
    mutable std::array<std::uint32_t, 16> handles_{};
    mutable std::size_t handle_count_ = 0;
};

} // namespace

bool create_installed_app_resources(InstalledResourceSnapshot&& snapshot,
                                    const jellyframe::HostBudgets& budgets,
                                    std::string_view entry_path,
                                    const std::uint8_t* core_font_bytes,
                                    std::size_t core_font_size,
                                    std::shared_ptr<const void> core_font_owner,
                                    BmpImageAdapter::Clock clock,
                                    InstalledAppResources::Handle& output) {
    if (!clock) return false;
    if (snapshot.entries.size() > kInstalledResourceSnapshotMaxEntries) return false;
    std::size_t snapshot_bytes = 0;
    for (const auto& entry : snapshot.entries) {
        if (entry.bytes.size() > kInstalledResourceMaxBytes ||
            entry.bytes.size() > budgets.max_resource_bytes ||
            entry.bytes.size() > kInstalledResourceSnapshotMaxBytes - snapshot_bytes) return false;
        snapshot_bytes += entry.bytes.size();
    }
    try {
        auto candidate = std::make_shared<InstalledAppResources>();
        auto owned_snapshot = std::make_shared<InstalledResourceSnapshot>(std::move(snapshot));
        if (!owned_snapshot->rebuild_views()) return false;
        candidate->snapshot = owned_snapshot;

        using namespace jellyframe;
        RuntimeFontResource::Handle core_font;
        if (RuntimeFontResource::retain(core_font_bytes, core_font_size, core_font_owner, core_font) !=
                RuntimeFontStatus::Ready) return false;
        std::vector<RuntimeFontRegistration> registrations;
        registrations.push_back({std::move(core_font), 0});
        if (owned_snapshot->fonts.size() > kDeviceBundleMaxFonts) return false;
        for (const auto& metadata : owned_snapshot->fonts) {
            if (metadata.source.back() != '\0' || metadata.family.back() != '\0') return false;
            const std::string_view path(metadata.source.data());
            if (path.size() < 8 || path.substr(path.size() - 8) != ".jffont2") return false;
            std::string normalized;
            if (!resolve_local_resource_url(path, "/", normalized) || normalized != path) return false;
            for (const unsigned char ch : std::string_view(metadata.family.data()))
                if (ch < 0x20) return false;
            const InstalledResourceSnapshotEntry* match = nullptr;
            for (const auto& entry : owned_snapshot->entries) {
                if (entry.url != path) continue;
                if (match || entry.kind != HostResourceKind::Font) return false;
                match = &entry;
            }
            if (!match || match->bytes.empty()) return false;
            RuntimeFontResource::Handle resource;
            if (RuntimeFontResource::retain(match->bytes.data(), match->bytes.size(), owned_snapshot, resource) !=
                RuntimeFontStatus::Ready) return false;
            const auto family_hash = normalized_font_family_hash(metadata.family.data());
            registrations.push_back({std::move(resource), family_hash, family_hash});
        }
        if (RuntimeFontCatalog::create(registrations.data(), registrations.size(), candidate->text_catalog) != RuntimeFontStatus::Ready)
            return false;

        auto source = std::make_shared<InstalledSnapshotImageSource>(
            candidate->snapshot, budgets, entry_path, clock);
        if (!source->preflight()) return false;
        if (RuntimeImageProvider::create(std::move(source), candidate->image_provider) !=
                RuntimeImageProviderStatus::Ready) return false;
        output = std::move(candidate);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

} // namespace jellyframe_esp32s3
#endif
