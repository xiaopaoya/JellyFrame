#pragma once

#include "jellyframe_esp32s3_image.h"

#include "app_runtime/runtime_font_catalog.h"
#include "app_runtime/runtime_image_provider.h"

#include <memory>
#include <string_view>

namespace jellyframe {
struct ScriptTaskWorkerRuntimeOptions;
struct ScriptTaskFrameRendererOptions;
}

namespace jellyframe_esp32s3 {

#if JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
struct InstalledAppResources final {
    using Handle = std::shared_ptr<const InstalledAppResources>;

    std::shared_ptr<const InstalledResourceSnapshot> snapshot;
    jellyframe::RuntimeFontCatalog::Handle text_catalog;
    jellyframe::RuntimeImageProvider::Handle image_provider;

    void bind_worker_resources(jellyframe::ScriptTaskWorkerRuntimeOptions& options) const noexcept;
    void bind_ui_resources(jellyframe::ScriptTaskFrameRendererOptions& options) const noexcept;
};

#if JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
bool create_installed_app_resources(InstalledResourceSnapshot&& snapshot,
                                    const jellyframe::HostBudgets& budgets,
                                    std::string_view entry_path,
                                    const std::uint8_t* core_font_bytes,
                                    std::size_t core_font_size,
                                    std::shared_ptr<const void> core_font_owner,
                                    BmpImageAdapter::Clock clock,
                                    InstalledAppResources::Handle& output);
#endif
#endif

} // namespace jellyframe_esp32s3
