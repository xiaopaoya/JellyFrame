#pragma once

#include "app_package.h"
#include "app_runtime/app_host.h"
#include "app_runtime/core_system_font_catalog.h"

namespace jellyframe::native {

inline constexpr std::size_t kDesktopAppFontCapacity = 4;

inline void attach_desktop_system_font_catalog(AppRuntimeHost& runtime) {
    if (!runtime.attach_core_system_font_catalog_view(kNotoSansScSystemFontCatalog,
                                                      kNotoSansScSystemFontCatalogSize)) {
        throw std::runtime_error("Core system font catalog is invalid; desktop text startup was refused");
    }
}

inline void load_package_fonts_into_runtime(const jellyframe_example::AppPackageManifest& manifest,
                                            jellyframe_example::PackageResourceContext& context,
                                            AppRuntimeHost& runtime,
                                            DiagnosticSink* diagnostics) {
    if (manifest.font_sources.empty()) return;
    runtime.clear_current_fonts();
    for (std::size_t index = 0; index < manifest.font_sources.size(); ++index) {
        const auto& source = manifest.font_sources[index];
        const std::string_view family = index < manifest.font_families.size()
            ? std::string_view(manifest.font_families[index]) : std::string_view{};
        jellyframe_example::PackageResourceView view;
        AppFontLoadResult result;
        if (jellyframe_example::load_package_resource_view(source, manifest.entry, view, &context) && view.stable) {
            result = runtime.attach_current_core_font_view(view.data, view.size, family);
        } else {
            std::string bytes;
            if (!jellyframe_example::load_package_resource(source, manifest.entry, bytes, &context)) {
                report_diagnostic(diagnostics, DiagnosticStage::Package, DiagnosticSeverity::Warning,
                                  "app-font-resource-missing", "Manifest font resource could not be loaded", source);
                continue;
            }
            result = runtime.load_current_core_font(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                                    bytes.size(), family);
        }
        if (!result.loaded()) {
            report_diagnostic(diagnostics, DiagnosticStage::Package, DiagnosticSeverity::Warning,
                              "app-font-resource-invalid",
                              "Manifest font supplement is invalid or cannot be added to the Core font catalog", source);
        }
    }
}

} // namespace jellyframe::native
