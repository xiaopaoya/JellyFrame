#include "app_runtime/app_capability_broker.h"

#include <algorithm>
#include <array>
#include <unordered_set>

namespace jellyframe {
namespace {

constexpr std::array<std::string_view, 25> kKnownCapabilities = {
    "compute.jobs",
    "network.fetch",
    "storage.kv",
    "file.read",
    "file.write",
    "file.manage",
    "graphics.canvas2d",
    "image.decode",
    "media.audio.playback",
    "media.microphone",
    "media.camera",
    "media.video.input",
    "media.video.frame",
    "sensor.accelerometer",
    "sensor.gyroscope",
    "sensor.heart-rate",
    "sensor.ambient-light",
    "location.position",
    "system.battery",
    "system.weather",
    "system.activity",
    "connectivity.status",
    "connectivity.companion",
    "system.launcher",
    "system.appManager",
};

} // namespace

const char* app_capability_decision_status_name(AppCapabilityDecisionStatus status) {
    switch (status) {
    case AppCapabilityDecisionStatus::Granted:
        return "granted";
    case AppCapabilityDecisionStatus::GrantedProductSpecific:
        return "granted-product-specific";
    case AppCapabilityDecisionStatus::UnsupportedByHost:
        return "unsupported-by-host";
    case AppCapabilityDecisionStatus::UnknownCapability:
        return "unknown-capability";
    }
    return "unknown";
}

bool is_known_app_capability(std::string_view capability) {
    return std::find(kKnownCapabilities.begin(), kKnownCapabilities.end(), capability) != kKnownCapabilities.end();
}

std::vector<AppCapabilityDecision> evaluate_app_capability_requests(
    const std::vector<std::string>& requested_capabilities,
    const std::vector<std::string>& host_supported_capabilities,
    AppCapabilityBrokerOptions options) {
    std::vector<AppCapabilityDecision> decisions;
    decisions.reserve(requested_capabilities.size());
    std::unordered_set<std::string_view> host_supported;
    host_supported.reserve(host_supported_capabilities.size());
    for (const std::string& capability : host_supported_capabilities) {
        host_supported.insert(capability);
    }
    std::unordered_set<std::string_view> seen;
    seen.reserve(requested_capabilities.size());
    for (const std::string& capability : requested_capabilities) {
        if (capability.empty() || !seen.insert(capability).second) {
            continue;
        }
        const bool known = is_known_app_capability(capability);
        const bool supported = host_supported.find(capability) != host_supported.end();
        AppCapabilityDecisionStatus status = AppCapabilityDecisionStatus::UnknownCapability;
        if (known) {
            status = supported
                ? AppCapabilityDecisionStatus::Granted
                : AppCapabilityDecisionStatus::UnsupportedByHost;
        } else if (supported && options.allow_product_specific) {
            status = AppCapabilityDecisionStatus::GrantedProductSpecific;
        }
        decisions.push_back(AppCapabilityDecision{capability, status});
    }
    return decisions;
}

} // namespace jellyframe
