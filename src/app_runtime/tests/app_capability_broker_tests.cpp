#include "app_runtime/app_capability_broker.h"

#include <cassert>
#include <string>
#include <vector>

using namespace jellyframe;

namespace {

void known_capabilities_are_granted_only_when_host_supports_them() {
    const std::vector<std::string> requested = {
        "network.fetch",
        "storage.kv",
        "sensor.accelerometer",
    };
    const std::vector<std::string> host = {
        "network.fetch",
        "sensor.accelerometer",
    };
    const std::vector<AppCapabilityDecision> decisions =
        evaluate_app_capability_requests(requested, host);

    assert(decisions.size() == 3);
    assert(decisions[0].capability == "network.fetch");
    assert(decisions[0].status == AppCapabilityDecisionStatus::Granted);
    assert(decisions[0].granted());
    assert(decisions[1].capability == "storage.kv");
    assert(decisions[1].status == AppCapabilityDecisionStatus::UnsupportedByHost);
    assert(!decisions[1].granted());
    assert(decisions[2].capability == "sensor.accelerometer");
    assert(decisions[2].status == AppCapabilityDecisionStatus::Granted);
}

void product_specific_capability_requires_host_support() {
    const std::vector<std::string> requested = {
        "vendor.xpao.haptics.tick",
        "vendor.xpao.side-button",
    };
    const std::vector<std::string> host = {
        "vendor.xpao.haptics.tick",
    };
    const std::vector<AppCapabilityDecision> decisions =
        evaluate_app_capability_requests(requested, host);

    assert(decisions.size() == 2);
    assert(decisions[0].status == AppCapabilityDecisionStatus::GrantedProductSpecific);
    assert(decisions[0].granted());
    assert(decisions[1].status == AppCapabilityDecisionStatus::UnknownCapability);
    assert(!decisions[1].granted());
}

void product_specific_channel_can_be_disabled_by_profile() {
    const std::vector<std::string> requested = {"vendor.example.debug-port"};
    const std::vector<std::string> host = {"vendor.example.debug-port"};
    const std::vector<AppCapabilityDecision> decisions =
        evaluate_app_capability_requests(requested, host, AppCapabilityBrokerOptions{false});

    assert(decisions.size() == 1);
    assert(decisions[0].status == AppCapabilityDecisionStatus::UnknownCapability);
}

void duplicates_and_empty_capabilities_are_ignored() {
    const std::vector<std::string> requested = {
        "",
        "network.fetch",
        "network.fetch",
    };
    const std::vector<std::string> host = {"network.fetch"};
    const std::vector<AppCapabilityDecision> decisions =
        evaluate_app_capability_requests(requested, host);

    assert(decisions.size() == 1);
    assert(decisions[0].capability == "network.fetch");
}

void names_are_stable_for_diagnostics() {
    assert(is_known_app_capability("media.audio.mp3"));
    assert(is_known_app_capability("sensor.heart-rate"));
    assert(!is_known_app_capability("vendor.example.private"));
    assert(std::string(app_capability_decision_status_name(AppCapabilityDecisionStatus::Granted)) == "granted");
    assert(std::string(app_capability_decision_status_name(
               AppCapabilityDecisionStatus::GrantedProductSpecific)) == "granted-product-specific");
    assert(std::string(app_capability_decision_status_name(
               AppCapabilityDecisionStatus::UnsupportedByHost)) == "unsupported-by-host");
    assert(std::string(app_capability_decision_status_name(
               AppCapabilityDecisionStatus::UnknownCapability)) == "unknown-capability");
}

} // namespace

int main() {
    known_capabilities_are_granted_only_when_host_supports_them();
    product_specific_capability_requires_host_support();
    product_specific_channel_can_be_disabled_by_profile();
    duplicates_and_empty_capabilities_are_ignored();
    names_are_stable_for_diagnostics();
    return 0;
}
