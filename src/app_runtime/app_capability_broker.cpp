#include "app_runtime/app_capability_broker.h"

#include <algorithm>
#include <array>

namespace jellyframe {
namespace {

constexpr std::array<std::string_view, 16> kKnownCapabilities = {
    "network.fetch",
    "storage.kv",
    "image.decode",
    "media.audio.mp3",
    "media.microphone",
    "media.camera",
    "media.video.input",
    "sensor.accelerometer",
    "sensor.gyroscope",
    "sensor.heart-rate",
    "sensor.ambient-light",
    "location.position",
    "connectivity.status",
    "connectivity.companion",
    "system.launcher",
    "system.appManager",
};

bool contains_string_view(const std::vector<std::string>& values, std::string_view needle) {
    return std::find_if(values.begin(), values.end(), [needle](const std::string& value) {
        return value == needle;
    }) != values.end();
}

bool decision_exists(const std::vector<AppCapabilityDecision>& decisions, const std::string& capability) {
    return std::find_if(decisions.begin(),
                        decisions.end(),
                        [&capability](const AppCapabilityDecision& decision) {
                            return decision.capability == capability;
                        }) != decisions.end();
}

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
    for (const std::string& capability : requested_capabilities) {
        if (capability.empty() || decision_exists(decisions, capability)) {
            continue;
        }
        const bool known = is_known_app_capability(capability);
        const bool host_supported = contains_string_view(host_supported_capabilities, capability);
        AppCapabilityDecisionStatus status = AppCapabilityDecisionStatus::UnknownCapability;
        if (known) {
            status = host_supported
                ? AppCapabilityDecisionStatus::Granted
                : AppCapabilityDecisionStatus::UnsupportedByHost;
        } else if (host_supported && options.allow_product_specific) {
            status = AppCapabilityDecisionStatus::GrantedProductSpecific;
        }
        decisions.push_back(AppCapabilityDecision{capability, status});
    }
    return decisions;
}

} // namespace jellyframe
