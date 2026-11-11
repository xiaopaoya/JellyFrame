#pragma once

#include "app_runtime/script_task_prepared_text.h"

// Keep installed-frame selection and resource admission on the same boundary.
#if defined(CONFIG_JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM) && \
    CONFIG_JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM && \
    defined(CONFIG_JELLYFRAME_ESP32S3_ENABLE_BMP_IMAGE_ADAPTER) && \
    CONFIG_JELLYFRAME_ESP32S3_ENABLE_BMP_IMAGE_ADAPTER && \
    JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
#define JELLYFRAME_ESP32S3_INSTALLED_FRAME10_ENABLED 1
#else
#define JELLYFRAME_ESP32S3_INSTALLED_FRAME10_ENABLED 0
#endif

#include "app_runtime/script_task_contract.h"
#include <memory>
#if JELLYFRAME_ESP32S3_INSTALLED_FRAME10_ENABLED
#include "jellyframe_esp32s3_installed_resource_owner.h"
#endif

namespace jellyframe_esp32s3 {
struct InstalledAppResources;

inline jellyframe::ScriptAppSession begin_installed_script_session(
    jellyframe::ScriptTaskSupervisor& supervisor,
    std::uint32_t app_instance_id,
    const std::shared_ptr<const InstalledAppResources>& resources) {
#if JELLYFRAME_ESP32S3_INSTALLED_FRAME10_ENABLED
    if (!resources || !resources->text_catalog || !resources->image_provider) return {};
    return supervisor.begin(app_instance_id, resources->text_catalog, resources->image_provider);
#else
    (void)resources;
    return supervisor.begin(app_instance_id);
#endif
}
} // namespace jellyframe_esp32s3
