#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace jellyframe {

enum class AppCapabilityDecisionStatus {
    Granted,
    GrantedProductSpecific,
    UnsupportedByHost,
    UnknownCapability,
};

struct AppCapabilityBrokerOptions {
    bool allow_product_specific = true;
};

struct AppCapabilityDecision {
    std::string capability;
    AppCapabilityDecisionStatus status = AppCapabilityDecisionStatus::UnknownCapability;

    bool granted() const {
        return status == AppCapabilityDecisionStatus::Granted ||
            status == AppCapabilityDecisionStatus::GrantedProductSpecific;
    }
};

const char* app_capability_decision_status_name(AppCapabilityDecisionStatus status);
bool is_known_app_capability(std::string_view capability);

std::vector<AppCapabilityDecision> evaluate_app_capability_requests(
    const std::vector<std::string>& requested_capabilities,
    const std::vector<std::string>& host_supported_capabilities,
    AppCapabilityBrokerOptions options = {});

} // namespace jellyframe
