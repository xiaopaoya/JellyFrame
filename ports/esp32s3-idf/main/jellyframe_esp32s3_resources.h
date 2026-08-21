#pragma once

#include "render_core/host.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace jellyframe_esp32s3 {

struct ResourceEntry {
    std::string_view url;
    jellyframe::HostResourceKind kind = jellyframe::HostResourceKind::Other;
    const std::uint8_t* bytes = nullptr;
    std::size_t size = 0;
};

struct ResourceBundle {
    const ResourceEntry* entries = nullptr;
    std::size_t count = 0;
};

// An installed app is snapshotted before its UI task starts.  The snapshot
// owns every byte it exposes, so the UI never retains a registry lease or a
// raw-partition pointer after launch.
struct InstalledResourceSnapshotEntry {
    std::string url;
    jellyframe::HostResourceKind kind = jellyframe::HostResourceKind::Other;
    std::vector<std::uint8_t> bytes;
};

struct InstalledResourceSnapshot {
    std::vector<InstalledResourceSnapshotEntry> entries;
    std::vector<ResourceEntry> views;
    ResourceBundle bundle{};
    std::size_t total_bytes = 0;

    bool rebuild_views();
    void clear();
};

struct ResourceLoadStats {
    std::uint32_t successful_loads = 0;
    std::uint32_t missing_loads = 0;
    std::uint32_t rejected_loads = 0;
    std::size_t loaded_bytes = 0;
};

struct ResourceBundleContext {
    const ResourceBundle* bundle = nullptr;
    std::size_t max_resource_bytes = 0;
    std::string_view base_url;
    ResourceLoadStats* stats = nullptr;
};

const ResourceBundle& default_resource_bundle();

const ResourceBundle& generated_resource_bundle();

ResourceBundleContext make_resource_context(const jellyframe::HostBudgets& budgets,
                                            std::string_view base_url,
                                            ResourceLoadStats* stats = nullptr);

ResourceBundleContext make_resource_context(const jellyframe::HostBudgets& budgets,
                                            std::string_view base_url,
                                            const ResourceBundle& bundle,
                                            ResourceLoadStats* stats = nullptr);

// Resolves a package-local URL to a normalized absolute package path. This is
// shared by launch-time snapshotting and UI-time resource loading.
bool resolve_local_resource_url(std::string_view url,
                                std::string_view base_url,
                                std::string& output);

bool load_resource(const jellyframe::HostResourceRequest& request,
                   std::string& output,
                   void* raw_context);

bool load_linked_stylesheet(std::string_view href, std::string& output, void* raw_context);

bool load_classic_script(std::string_view src, std::string& output, void* raw_context);

} // namespace jellyframe_esp32s3
