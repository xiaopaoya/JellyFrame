#include "app_runtime/app_services.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (condition) {
        return;
    }
    std::cerr << "app_services check failed: " << message << '\n';
    std::abort();
}

AppRuntimeHost make_host() {
    return AppRuntimeHost(AppRuntimeHostOptions{
        4,
        2,
        8,
        4096,
        1,
    });
}

std::vector<HostServiceCompletion> pump(AppRuntimeHost& host) {
    std::vector<HostServiceCompletion> accepted;
    host.pump_frame_completions(accepted);
    return accepted;
}

void network_fetch_requires_capability_and_returns_fixture_handle() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.weather", AppRole::App);

    NetworkFetchMock network;
    check(network.submit_fetch(host, "https://api.example/weather").status ==
              AppServiceSubmitStatus::CapabilityDenied,
          "network capability gate");

    network.set_policy(NetworkFetchPolicy{true, 128, 256});
    const bool fixture_added = network.add_fixture(NetworkFetchFixture{
        "https://api.example/weather",
        200,
        "application/json",
        "{\"temp\":21}",
    });
    check(fixture_added, "network fixture accepted");
    const AppServiceSubmitResult submitted = network.submit_fetch(host, "https://api.example/weather", 1000);
    check(submitted.accepted(), "network submit accepted");
    const bool completed = network.complete_next(host);
    check(completed, "network complete next");

    std::vector<HostServiceCompletion> accepted = pump(host);
    check(accepted.size() == 1, "network completion accepted");
    check(accepted.front().kind == HostServiceJobKind::NetworkFetch, "network completion kind");
    check(accepted.front().status == HostServiceStatus::Completed, "network completion status");
    check(accepted.front().handle != 0, "network response handle");

    const NetworkFetchRecord* response = network.response(accepted.front().handle);
    check(response != nullptr, "network response lookup");
    check(response->app_instance_id == host.current_app_instance_id(), "network response instance");
    check(response->status_code == 200, "network response status code");
    check(response->body == "{\"temp\":21}", "network response body");
    check(network.release_response(host, accepted.front().handle), "network response release");
}

void service_policy_requires_manifest_and_host_approval() {
    HostDeviceCapabilities capabilities;
    capabilities.has_network = true;
    capabilities.network.supports_fetch = true;
    capabilities.network.max_request_bytes = 96;
    capabilities.network.max_response_bytes = 512;
    capabilities.async.max_in_flight_jobs = 5;
    capabilities.media.supports_image_decode = true;
    capabilities.media.max_image_width = 80;
    capabilities.media.max_image_height = 80;
    capabilities.media.max_decoded_image_bytes = 80 * 80 * 2;
    AppServiceHostProfile profile = app_service_host_profile_from_capabilities(
        capabilities, AppPrivateKvPolicy{true, 12, 24, 3, 64});
    check(profile.allow_network_fetch, "profile network allowed");
    check(profile.max_network_url_bytes == 96, "profile network url budget");
    check(profile.allow_storage_kv, "profile storage allowed");
    check(profile.max_storage_value_bytes == 24, "profile storage value budget");
    check(profile.allow_image_decode, "profile image allowed");
    check(profile.max_image_width == 80, "profile image width budget");
    check(profile.max_pending_image_decodes == 5, "profile image pending budget");

    AppServicePolicies policies = app_service_policies_for_app(AppServiceManifestCapabilities{}, profile);
    check(!policies.network.enabled, "network denied without manifest capability");
    check(!policies.storage.enabled, "storage denied without manifest capability");
    check(!policies.image.enabled, "image denied without manifest capability");

    policies = app_service_policies_for_app(AppServiceManifestCapabilities{true, true, true}, profile);
    check(policies.network.enabled, "network allowed with manifest and host");
    check(policies.network.max_response_bytes == 512, "network response budget carried");
    check(policies.storage.enabled, "storage allowed with manifest and host");
    check(policies.storage.max_items_per_app == 3, "storage item budget carried");
    check(policies.image.enabled, "image allowed with manifest and host");
    check(policies.image.max_decoded_bytes == 80 * 80 * 2, "image decoded budget carried");

    capabilities.has_network = false;
    capabilities.media.supports_image_decode = false;
    profile = app_service_host_profile_from_capabilities(capabilities, AppPrivateKvPolicy{});
    policies = app_service_policies_for_app(AppServiceManifestCapabilities{true, true, true}, profile);
    check(!policies.network.enabled, "network denied without host network");
    check(!policies.storage.enabled, "storage denied without host storage");
    check(!policies.image.enabled, "image denied without host image decode");
}

void network_fetch_pending_request_is_cancelled_on_app_switch() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.first", AppRole::App);
    NetworkFetchMock network(NetworkFetchPolicy{true, 128, 256});
    const bool fixture_added = network.add_fixture(NetworkFetchFixture{"app://fixture", 200, "application/json", "{}"});
    check(fixture_added, "network cancel fixture");
    const AppServiceSubmitResult submitted = network.submit_fetch(host, "app://fixture");
    check(submitted.accepted(), "network cancel submit");
    host.launch("org.example.second", AppRole::App);
    check(host.requests().empty(), "network pending request cancelled");
    check(!network.complete_next(host), "network cancelled request not completed");
}

void image_decode_requires_capability_and_returns_surface_handle() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.gallery", AppRole::App);

    ImageDecodeMock images;
    check(images.submit_decode(host, "app://icon.raw").status == AppServiceSubmitStatus::CapabilityDenied,
          "image decode capability gate");

    images.set_policy(ImageDecodePolicy{true, 64, 32, 32, 32 * 32 * 2, 2});
    std::vector<std::uint8_t> pixels(16 * 16 * 2, 0x7f);
    check(images.add_fixture(ImageDecodeFixture{
              "app://icon.raw",
              16,
              16,
              16,
              HostPixelFormat::Rgb565,
              pixels,
          }),
          "image fixture accepted");

    const AppServiceSubmitResult submitted = images.submit_decode(host, "app://icon.raw", 1000);
    check(submitted.accepted(), "image decode submit accepted");
    check(images.complete_next(host), "image decode complete next");

    std::vector<HostServiceCompletion> accepted = pump(host);
    check(accepted.size() == 1, "image decode completion accepted");
    check(accepted.front().kind == HostServiceJobKind::ImageDecode, "image decode completion kind");
    check(accepted.front().status == HostServiceStatus::Completed, "image decode completion status");
    check(accepted.front().handle != 0, "image decode surface handle");
    check(accepted.front().byte_count == pixels.size(), "image decode byte count");

    const AppDecodedSurfaceRecord* surface = images.surface(accepted.front().handle);
    check(surface != nullptr, "image surface lookup");
    check(surface->app_instance_id == host.current_app_instance_id(), "image surface instance");
    check(surface->width == 16 && surface->height == 16, "image surface size");
    check(surface->stride_pixels == 16, "image surface stride");
    check(surface->pixel_format == HostPixelFormat::Rgb565, "image surface format");
    check(surface->pixels.size() == pixels.size(), "image surface pixels carried");
    check(images.release_surface(host, accepted.front().handle), "image surface release");
}

void image_decode_enforces_surface_budgets() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.gallery", AppRole::App);
    ImageDecodeMock images(ImageDecodePolicy{true, 32, 8, 8, 8 * 8 * 2, 1});

    check(!images.add_fixture(ImageDecodeFixture{"app://large", 16, 8, 16, HostPixelFormat::Rgb565, {}}),
          "image fixture rejects wide surface");
    check(!images.add_fixture(ImageDecodeFixture{"app://badstride", 8, 8, 7, HostPixelFormat::Rgb565, {}}),
          "image fixture rejects invalid stride");
    check(!images.add_fixture(ImageDecodeFixture{"app://badbytes", 8, 8, 8, HostPixelFormat::Rgb565, {1, 2, 3}}),
          "image fixture rejects inconsistent raw bytes");
    check(images.add_fixture(ImageDecodeFixture{"app://ok", 8, 8, 8, HostPixelFormat::Rgb565, {}}),
          "image fixture accepts metadata-only host surface");

    const AppServiceSubmitResult first = images.submit_decode(host, "app://missing");
    check(first.accepted(), "image missing submit accepted");
    const AppServiceSubmitResult second = images.submit_decode(host, "app://ok");
    check(second.status == AppServiceSubmitStatus::BudgetExceeded, "image pending budget enforced");
    check(images.complete_next(host), "image missing completion produced");
    std::vector<HostServiceCompletion> accepted = pump(host);
    check(accepted.size() == 1, "image missing completion accepted");
    check(accepted.front().status == HostServiceStatus::Failed, "image missing status");
    check(accepted.front().error_code == 404, "image missing error");
}

void kv_storage_is_app_private_and_async() {
    AppRuntimeHost host = make_host();
    AppPrivateKvStorageMock storage(AppPrivateKvPolicy{true, 16, 32, 4, 96});
    host.launch("org.example.clock", AppRole::App);

    const AppServiceSubmitResult set = storage.submit_set(host, "theme", "dark");
    check(set.accepted(), "kv set submitted");
    bool completed = storage.complete_next(host);
    check(completed, "kv set completed");
    std::vector<HostServiceCompletion> accepted = pump(host);
    check(accepted.size() == 1, "kv set completion");
    check(accepted.front().status == HostServiceStatus::Completed, "kv set status");

    AppServiceSubmitResult get = storage.submit_get(host, "theme");
    check(get.accepted(), "kv get submitted");
    completed = storage.complete_next(host);
    check(completed, "kv get completed");
    accepted = pump(host);
    check(accepted.size() == 1, "kv get completion");
    check(accepted.front().handle != 0, "kv value handle");
    const AppPrivateKvRecord* value = storage.value(accepted.front().handle);
    check(value != nullptr, "kv value lookup");
    check(value->app_id == "org.example.clock", "kv app namespace");
    check(value->key == "theme", "kv key");
    check(value->value == "dark", "kv value");
    check(storage.release_value(host, accepted.front().handle), "kv value release");

    host.launch("org.example.timer", AppRole::App);
    get = storage.submit_get(host, "theme");
    check(get.accepted(), "kv private get submitted");
    completed = storage.complete_next(host);
    check(completed, "kv private get completed");
    accepted = pump(host);
    check(accepted.size() == 1, "kv private completion");
    check(accepted.front().status == HostServiceStatus::Failed, "kv private miss");
    check(accepted.front().handle == 0, "kv private miss has no handle");
}

void kv_storage_enforces_budgets() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.settings", AppRole::App);
    AppPrivateKvStorageMock storage(AppPrivateKvPolicy{true, 4, 4, 1, 8});
    check(storage.submit_set(host, "toolong", "v").status == AppServiceSubmitStatus::InvalidInput,
          "kv rejects long key");
    check(storage.submit_set(host, "k", "value-too-large").status == AppServiceSubmitStatus::BudgetExceeded,
          "kv rejects large value");

    AppServiceSubmitResult set = storage.submit_set(host, "k", "v");
    check(set.accepted(), "kv budget set submitted");
    bool completed = storage.complete_next(host);
    check(completed, "kv budget set completed");
    std::vector<HostServiceCompletion> accepted = pump(host);
    check(accepted.size() == 1, "kv budget set completion");
    check(accepted.front().status == HostServiceStatus::Completed, "kv budget set status");

    set = storage.submit_set(host, "k2", "v");
    check(set.accepted(), "kv budget overflow submitted");
    completed = storage.complete_next(host);
    check(completed, "kv budget overflow completed");
    accepted = pump(host);
    check(accepted.size() == 1, "kv budget overflow completion");
    check(accepted.front().status == HostServiceStatus::BudgetExceeded, "kv budget overflow status");
}

void local_storage_shadow_follows_web_storage_subset() {
    AppLocalStorageShadow storage(AppPrivateKvPolicy{true, 8, 16, 3, 48});
    check(storage.set_item("theme", "dark") == AppLocalStorageStatus::Ok, "localStorage set theme");
    check(storage.set_item("mode", "compact") == AppLocalStorageStatus::Ok, "localStorage set mode");
    check(storage.length() == 2, "localStorage length");
    check(storage.used_bytes() == 20, "localStorage byte accounting");

    std::string value;
    check(storage.get_item("theme", &value) == AppLocalStorageStatus::Ok, "localStorage get theme");
    check(value == "dark", "localStorage get value");

    std::string key;
    check(storage.key(0, &key) == AppLocalStorageStatus::Ok, "localStorage key 0");
    check(key == "theme", "localStorage key insertion order");
    check(storage.get_item("missing", &value) == AppLocalStorageStatus::NotFound, "localStorage missing key");

    check(storage.set_item("theme", "light") == AppLocalStorageStatus::Ok, "localStorage update");
    check(storage.length() == 2, "localStorage update keeps length");
    check(storage.used_bytes() == 21, "localStorage update bytes");
    check(storage.remove_item("mode") == AppLocalStorageStatus::Ok, "localStorage remove");
    check(storage.length() == 1, "localStorage remove length");
    storage.clear();
    check(storage.length() == 0, "localStorage clear");
}

void local_storage_shadow_enforces_policy_without_host_io() {
    AppLocalStorageShadow storage;
    check(storage.set_item("k", "v") == AppLocalStorageStatus::Disabled, "localStorage disabled set");
    check(storage.get_item("k", nullptr) == AppLocalStorageStatus::Disabled, "localStorage disabled get");

    storage.set_policy(AppPrivateKvPolicy{true, 3, 4, 1, 8});
    check(storage.set_item("", "v") == AppLocalStorageStatus::InvalidKey, "localStorage empty key");
    check(storage.set_item("long", "v") == AppLocalStorageStatus::InvalidKey, "localStorage long key");
    check(storage.set_item("k", "value") == AppLocalStorageStatus::BudgetExceeded, "localStorage large value");
    check(storage.set_item("k", "v") == AppLocalStorageStatus::Ok, "localStorage budget set");
    check(storage.set_item("k2", "v") == AppLocalStorageStatus::BudgetExceeded, "localStorage item budget");
}

void service_workers_do_not_consume_other_service_requests() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.mixed", AppRole::App);
    NetworkFetchMock network(NetworkFetchPolicy{true, 64, 128});
    AppPrivateKvStorageMock storage(AppPrivateKvPolicy{true, 16, 32, 4, 96});
    check(network.add_fixture(NetworkFetchFixture{"app://mixed", 200, "application/json", "{}"}),
          "mixed fixture accepted");

    const AppServiceSubmitResult set = storage.submit_set(host, "theme", "dark");
    const AppServiceSubmitResult fetch = network.submit_fetch(host, "app://mixed");
    check(set.accepted(), "mixed kv submitted");
    check(fetch.accepted(), "mixed network submitted");
    check(network.complete_next(host), "mixed network completed first");
    check(storage.complete_next(host), "mixed storage completed second");

    std::vector<HostServiceCompletion> accepted = pump(host);
    check(accepted.size() == 2, "mixed first frame completions");
    check(accepted[0].kind == HostServiceJobKind::NetworkFetch, "mixed network completion kept");
    check(accepted[1].kind == HostServiceJobKind::StorageKv, "mixed storage completion kept");
    if (accepted[0].handle != 0) {
        check(network.release_response(host, accepted[0].handle), "mixed network release");
    }
}

} // namespace

int main() {
    network_fetch_requires_capability_and_returns_fixture_handle();
    service_policy_requires_manifest_and_host_approval();
    network_fetch_pending_request_is_cancelled_on_app_switch();
    image_decode_requires_capability_and_returns_surface_handle();
    image_decode_enforces_surface_budgets();
    kv_storage_is_app_private_and_async();
    kv_storage_enforces_budgets();
    local_storage_shadow_follows_web_storage_subset();
    local_storage_shadow_enforces_policy_without_host_io();
    service_workers_do_not_consume_other_service_requests();
    return 0;
}
