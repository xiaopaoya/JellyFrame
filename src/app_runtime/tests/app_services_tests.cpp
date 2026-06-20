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
    capabilities.media.supports_audio_playback = true;
    capabilities.media.max_audio_streams = 2;
    AppServiceHostProfile profile = app_service_host_profile_from_capabilities(
        capabilities, AppPrivateKvPolicy{true, 12, 24, 3, 64});
    check(profile.allow_network_fetch, "profile network allowed");
    check(profile.max_network_url_bytes == 96, "profile network url budget");
    check(profile.allow_storage_kv, "profile storage allowed");
    check(profile.max_storage_value_bytes == 24, "profile storage value budget");
    check(profile.allow_image_decode, "profile image allowed");
    check(profile.max_image_width == 80, "profile image width budget");
    check(profile.max_pending_image_decodes == 5, "profile image pending budget");
    check(profile.allow_audio_playback, "profile audio allowed");
    check(profile.max_audio_streams == 2, "profile audio stream budget");

    AppServicePolicies policies = app_service_policies_for_app(AppServiceManifestCapabilities{}, profile);
    check(!policies.network.enabled, "network denied without manifest capability");
    check(!policies.storage.enabled, "storage denied without manifest capability");
    check(!policies.image.enabled, "image denied without manifest capability");
    check(!policies.audio.enabled, "audio denied without manifest capability");

    policies = app_service_policies_for_app(AppServiceManifestCapabilities{true, true, true, true}, profile);
    check(policies.network.enabled, "network allowed with manifest and host");
    check(policies.network.max_response_bytes == 512, "network response budget carried");
    check(policies.storage.enabled, "storage allowed with manifest and host");
    check(policies.storage.max_items_per_app == 3, "storage item budget carried");
    check(policies.image.enabled, "image allowed with manifest and host");
    check(policies.image.max_decoded_bytes == 80 * 80 * 2, "image decoded budget carried");
    check(policies.audio.enabled, "audio allowed with manifest and host");
    check(policies.audio.max_audio_streams == 2, "audio stream budget carried");

    capabilities.has_network = false;
    capabilities.media.supports_image_decode = false;
    capabilities.media.supports_audio_playback = false;
    profile = app_service_host_profile_from_capabilities(capabilities, AppPrivateKvPolicy{});
    policies = app_service_policies_for_app(AppServiceManifestCapabilities{true, true, true, true}, profile);
    check(!policies.network.enabled, "network denied without host network");
    check(!policies.storage.enabled, "storage denied without host storage");
    check(!policies.image.enabled, "image denied without host image decode");
    check(!policies.audio.enabled, "audio denied without host playback");
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

void image_surface_cache_requests_resolves_and_releases_surfaces() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.gallery", AppRole::App);
    ImageDecodeMock images(ImageDecodePolicy{true, 64, 16, 16, 16 * 16 * 2, 2});
    check(images.add_fixture(ImageDecodeFixture{"app://icon", 8, 8, 8, HostPixelFormat::Rgb565, {}}),
          "image cache fixture accepted");
    AppImageSurfaceCache cache;

    std::uint32_t handle = 99;
    check(!cache.resolve_or_request(host, images, "app://icon", &handle), "image cache miss submits decode");
    check(handle == 0, "image cache miss clears output handle");
    check(cache.state_for_url("app://icon") == AppImageSurfaceState::Pending, "image cache pending state");
    check(host.requests().size() == 1, "image cache queued one request");
    check(!cache.resolve_or_request(host, images, "app://icon", &handle), "image cache pending does not duplicate request");
    check(host.requests().size() == 1, "image cache still has one request");

    check(images.complete_next(host), "image cache decode completed");
    std::vector<HostServiceCompletion> accepted = pump(host);
    check(accepted.size() == 1, "image cache completion accepted");
    check(cache.handle_completion(accepted.front()), "image cache handles completion");
    check(cache.state_for_url("app://icon") == AppImageSurfaceState::Ready, "image cache ready state");
    check(cache.resolve_or_request(host, images, "app://icon", &handle), "image cache resolves ready surface");
    check(handle == accepted.front().handle, "image cache returns surface handle");
    check(images.surface(handle) != nullptr, "image cache surface exists before release");

    check(cache.release_all(host, images) == 1, "image cache releases one surface");
    check(images.surface(handle) == nullptr, "image cache release drops surface record");
    check(cache.size() == 0, "image cache clear after release");
}

void image_surface_cache_records_failed_decodes_without_retry_loop() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.gallery", AppRole::App);
    ImageDecodeMock images(ImageDecodePolicy{true, 64, 16, 16, 16 * 16 * 2, 2});
    AppImageSurfaceCache cache;

    std::uint32_t handle = 0;
    check(!cache.resolve_or_request(host, images, "app://missing", &handle), "image cache missing submitted");
    check(images.complete_next(host), "image cache missing completed");
    std::vector<HostServiceCompletion> accepted = pump(host);
    check(accepted.size() == 1, "image cache missing completion accepted");
    check(cache.handle_completion(accepted.front()), "image cache records failed completion");
    check(cache.state_for_url("app://missing") == AppImageSurfaceState::Failed, "image cache failed state");
    const std::string detail = cache.diagnostic_detail_for_url("app://missing");
    check(detail.find("state=failed") != std::string::npos,
          "image cache failed completion diagnostic includes failed state");
    check(detail.find("reason=resource-not-found") != std::string::npos,
          "image cache failed completion diagnostic includes stable reason");
    check(detail.find("job=") != std::string::npos,
          "image cache failed completion diagnostic includes job id");
    check(!cache.resolve_or_request(host, images, "app://missing", &handle), "image cache failed does not resolve");
    check(host.requests().empty(), "image cache failed does not retry every frame");
}

void image_surface_cache_keeps_transient_budget_rejections_retryable() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.gallery", AppRole::App);
    ImageDecodeMock images(ImageDecodePolicy{true, 64, 16, 16, 16 * 16 * 2, 1});
    check(images.add_fixture(ImageDecodeFixture{"app://a", 8, 8, 8, HostPixelFormat::Rgb565, {}}),
          "image cache transient fixture a");
    check(images.add_fixture(ImageDecodeFixture{"app://b", 8, 8, 8, HostPixelFormat::Rgb565, {}}),
          "image cache transient fixture b");
    AppImageSurfaceCache cache;
    std::uint32_t handle = 0;
    check(!cache.resolve_or_request(host, images, "app://a", &handle), "image cache transient first pending");
    check(!cache.resolve_or_request(host, images, "app://b", &handle), "image cache transient second rejected");
    check(cache.state_for_url("app://b") == AppImageSurfaceState::Missing,
          "image cache transient rejection remains retryable");
    check(cache.last_submit_status_for_url("app://b") == AppServiceSubmitStatus::BudgetExceeded,
          "image cache records transient rejection status");

    check(images.complete_next(host), "image cache transient first completed");
    std::vector<HostServiceCompletion> accepted = pump(host);
    check(accepted.size() == 1, "image cache transient completion accepted");
    check(cache.handle_completion(accepted.front()), "image cache transient first handled");
    check(!cache.resolve_or_request(host, images, "app://b", &handle), "image cache transient retry submits later");
    check(cache.state_for_url("app://b") == AppImageSurfaceState::Pending,
          "image cache transient retry reaches pending state");
}

void image_surface_cache_records_permanent_request_rejections() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.gallery", AppRole::App);
    ImageDecodeMock images(ImageDecodePolicy{true, 4, 16, 16, 16 * 16 * 2, 1});
    AppImageSurfaceCache cache;
    std::uint32_t handle = 0;
    check(!cache.resolve_or_request(host, images, "app://too-long", &handle), "image cache permanent reject");
    check(cache.state_for_url("app://too-long") == AppImageSurfaceState::Failed,
          "image cache permanent rejection records failed state");
    check(cache.last_submit_status_for_url("app://too-long") == AppServiceSubmitStatus::InvalidInput,
          "image cache permanent rejection records submit status");
    check(cache.last_failure_reason_for_url("app://too-long") == AppImageFailureReason::InvalidSource,
          "image cache permanent rejection exposes failure reason");
    const std::string detail = cache.diagnostic_detail_for_url("app://too-long");
    check(detail.find("state=failed") != std::string::npos,
          "image cache diagnostic includes failed state");
    check(detail.find("reason=invalid-source") != std::string::npos,
          "image cache diagnostic includes stable reason");
}

void image_failure_classification_is_specific() {
    check(classify_app_image_failure(AppServiceSubmitStatus::CapabilityDenied,
                                     HostServiceStatus::Unsupported,
                                     0) == AppImageFailureReason::CapabilityDenied,
          "image failure classification capability denied");
    check(classify_app_image_failure(AppServiceSubmitStatus::BudgetExceeded,
                                     HostServiceStatus::BudgetExceeded,
                                     0) == AppImageFailureReason::PendingBudget,
          "image failure classification pending budget");
    check(classify_app_image_failure(AppServiceSubmitStatus::QueueFull,
                                     HostServiceStatus::Failed,
                                     0) == AppImageFailureReason::QueueFull,
          "image failure classification queue full");
    check(classify_app_image_failure(AppServiceSubmitStatus::EmptyInstance,
                                     HostServiceStatus::Cancelled,
                                     0) == AppImageFailureReason::EmptyInstance,
          "image failure classification empty instance");
    check(classify_app_image_failure(AppServiceSubmitStatus::Accepted,
                                     HostServiceStatus::Failed,
                                     404) == AppImageFailureReason::ResourceNotFound,
          "image failure classification resource not found");
    check(classify_app_image_failure(AppServiceSubmitStatus::Accepted,
                                     HostServiceStatus::Failed,
                                     413) == AppImageFailureReason::DecodeBudgetExceeded,
          "image failure classification decode budget");
    check(classify_app_image_failure(AppServiceSubmitStatus::Accepted,
                                     HostServiceStatus::BudgetExceeded,
                                     507) == AppImageFailureReason::SurfaceBudgetExceeded,
          "image failure classification surface budget");
    check(classify_app_image_failure(AppServiceSubmitStatus::Accepted,
                                     HostServiceStatus::Unsupported,
                                     0) == AppImageFailureReason::Unsupported,
          "image failure classification unsupported");
    check(classify_app_image_failure(AppServiceSubmitStatus::Accepted,
                                     HostServiceStatus::Timeout,
                                     0) == AppImageFailureReason::DecodeTimeout,
          "image failure classification timeout");
    check(classify_app_image_failure(AppServiceSubmitStatus::Accepted,
                                     HostServiceStatus::Cancelled,
                                     0) == AppImageFailureReason::DecodeCancelled,
          "image failure classification cancelled");

    const std::string detail = app_image_failure_detail("app://missing",
                                                        AppServiceSubmitStatus::Accepted,
                                                        HostServiceStatus::Failed,
                                                        404);
    check(detail.find("reason=resource-not-found") != std::string::npos,
          "image failure detail carries reason");
    check(detail.find("src=app://missing") != std::string::npos,
          "image failure detail carries src");
}

void image_surface_cache_diagnostic_reports_pending_and_ready() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.gallery", AppRole::App);
    ImageDecodeMock images(ImageDecodePolicy{true, 64, 16, 16, 16 * 16 * 2, 1});
    check(images.add_fixture(ImageDecodeFixture{"app://icon", 8, 8, 8, HostPixelFormat::Rgb565, {}}),
          "image cache diagnostic fixture accepted");
    AppImageSurfaceCache cache;
    std::uint32_t handle = 0;
    check(!cache.resolve_or_request(host, images, "app://icon", &handle),
          "image cache diagnostic submits request");
    const std::string pending = cache.diagnostic_detail_for_url("app://icon");
    check(pending.find("state=pending") != std::string::npos,
          "image cache diagnostic includes pending state");
    check(pending.find("job=") != std::string::npos,
          "image cache diagnostic includes job id");

    check(images.complete_next(host), "image cache diagnostic completes decode");
    std::vector<HostServiceCompletion> accepted = pump(host);
    check(accepted.size() == 1, "image cache diagnostic completion accepted");
    check(cache.handle_completion(accepted.front()), "image cache diagnostic handles completion");
    check(cache.resolve_or_request(host, images, "app://icon", &handle),
          "image cache diagnostic resolves ready handle");
    const std::string ready = cache.diagnostic_detail_for_url("app://icon");
    check(ready.find("state=ready") != std::string::npos,
          "image cache diagnostic includes ready state");
    check(ready.find("reason=none") != std::string::npos,
          "image cache diagnostic includes no-failure reason");
    check(ready.find("handle=") != std::string::npos && ready.find("bytes=") != std::string::npos,
          "image cache diagnostic includes handle and byte count");
}

std::uint32_t cache_ready_image(AppRuntimeHost& host,
                                ImageDecodeMock& images,
                                AppImageSurfaceCache& cache,
                                const std::string& url) {
    std::uint32_t handle = 0;
    check(!cache.resolve_or_request(host, images, url, &handle), "image cache test submitted request");
    check(images.complete_next(host), "image cache test completed decode");
    std::vector<HostServiceCompletion> accepted = pump(host);
    check(accepted.size() == 1, "image cache test completion accepted");
    check(cache.handle_completion(accepted.front()), "image cache test handled completion");
    check(cache.resolve_or_request(host, images, url, &handle), "image cache test resolved ready image");
    check(handle != 0, "image cache test handle ready");
    return handle;
}

void image_surface_cache_evicts_lru_ready_surfaces() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.gallery", AppRole::App);
    ImageDecodeMock images(ImageDecodePolicy{true, 64, 16, 16, 16 * 16 * 2, 3});
    check(images.add_fixture(ImageDecodeFixture{"app://a", 8, 8, 8, HostPixelFormat::Rgb565, {}}),
          "image cache eviction fixture a");
    check(images.add_fixture(ImageDecodeFixture{"app://b", 8, 8, 8, HostPixelFormat::Rgb565, {}}),
          "image cache eviction fixture b");
    check(images.add_fixture(ImageDecodeFixture{"app://c", 8, 8, 8, HostPixelFormat::Rgb565, {}}),
          "image cache eviction fixture c");
    AppImageSurfaceCache cache(AppImageSurfaceCacheOptions{2, 0});

    const std::uint32_t handle_a = cache_ready_image(host, images, cache, "app://a");
    const std::uint32_t handle_b = cache_ready_image(host, images, cache, "app://b");
    const std::uint32_t handle_c = cache_ready_image(host, images, cache, "app://c");
    check(cache.ready_surface_count() == 3, "image cache eviction starts over surface budget");
    check(cache.evict_unreferenced(host, images) == 1, "image cache eviction releases one lru surface");
    check(images.surface(handle_a) == nullptr, "image cache eviction drops oldest surface");
    check(images.surface(handle_b) != nullptr, "image cache eviction keeps newer surface b");
    check(images.surface(handle_c) != nullptr, "image cache eviction keeps newer surface c");
    check(cache.ready_surface_count() == 2, "image cache eviction reaches surface budget");
}

void image_surface_cache_keeps_protected_display_list_surfaces() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.gallery", AppRole::App);
    ImageDecodeMock images(ImageDecodePolicy{true, 64, 16, 16, 16 * 16 * 2, 3});
    check(images.add_fixture(ImageDecodeFixture{"app://a", 8, 8, 8, HostPixelFormat::Rgb565, {}}),
          "image cache protected fixture a");
    check(images.add_fixture(ImageDecodeFixture{"app://b", 8, 8, 8, HostPixelFormat::Rgb565, {}}),
          "image cache protected fixture b");
    AppImageSurfaceCache cache(AppImageSurfaceCacheOptions{1, 0});

    const std::uint32_t handle_a = cache_ready_image(host, images, cache, "app://a");
    const std::uint32_t handle_b = cache_ready_image(host, images, cache, "app://b");
    const std::uint32_t protected_one[] = {handle_a};
    check(cache.evict_unreferenced(host, images, protected_one, 1) == 1,
          "image cache protected eviction releases unprotected surface");
    check(images.surface(handle_a) != nullptr, "image cache protected keeps referenced surface");
    check(images.surface(handle_b) == nullptr, "image cache protected drops unreferenced surface");
    check(cache.ready_surface_count() == 1, "image cache protected reaches budget");

    const std::uint32_t handle_c = cache_ready_image(host, images, cache, "app://b");
    const std::uint32_t protected_both[] = {handle_a, handle_c};
    check(cache.evict_unreferenced(host, images, protected_both, 2) == 0,
          "image cache keeps over-budget surfaces when all are protected");
    check(cache.ready_surface_count() == 2, "image cache remains over budget until a later frame can evict");
    check(images.surface(handle_a) != nullptr, "image cache still keeps protected a");
    check(images.surface(handle_c) != nullptr, "image cache still keeps protected c");
}

void image_surface_cache_evicts_by_decoded_bytes() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.gallery", AppRole::App);
    ImageDecodeMock images(ImageDecodePolicy{true, 64, 16, 16, 16 * 16 * 2, 3});
    check(images.add_fixture(ImageDecodeFixture{"app://a", 8, 8, 8, HostPixelFormat::Rgb565, {}}),
          "image cache bytes fixture a");
    check(images.add_fixture(ImageDecodeFixture{"app://b", 8, 8, 8, HostPixelFormat::Rgb565, {}}),
          "image cache bytes fixture b");
    check(images.add_fixture(ImageDecodeFixture{"app://c", 8, 8, 8, HostPixelFormat::Rgb565, {}}),
          "image cache bytes fixture c");
    AppImageSurfaceCache cache(AppImageSurfaceCacheOptions{0, 8U * 8U * 2U * 2U});

    const std::uint32_t handle_a = cache_ready_image(host, images, cache, "app://a");
    cache_ready_image(host, images, cache, "app://b");
    cache_ready_image(host, images, cache, "app://c");
    check(cache.ready_byte_count() == 8U * 8U * 2U * 3U, "image cache byte count tracks ready surfaces");
    check(cache.evict_unreferenced(host, images) == 1, "image cache byte eviction releases one surface");
    check(images.surface(handle_a) == nullptr, "image cache byte eviction drops oldest surface");
    check(cache.ready_byte_count() == 8U * 8U * 2U * 2U, "image cache byte eviction reaches budget");
}

void audio_command_mock_opens_controls_and_closes_streams() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.timer", AppRole::App);

    AudioCommandMock audio;
    check(audio.submit_open(host, "app://alarm.mp3").status == AppServiceSubmitStatus::CapabilityDenied,
          "audio capability gate");

    audio.set_policy(AudioPlaybackPolicy{true, 64, 1});
    check(audio.add_source(AudioSourceFixture{"app://alarm.mp3", 3000}), "audio source fixture accepted");
    const AppServiceSubmitResult open = audio.submit_open(host, "app://alarm.mp3", 80);
    check(open.accepted(), "audio open submitted");
    check(audio.complete_next(host), "audio open completed");
    std::vector<HostServiceCompletion> accepted = pump(host);
    check(accepted.size() == 1, "audio open completion accepted");
    check(accepted.front().kind == HostServiceJobKind::AudioCommand, "audio open completion kind");
    check(accepted.front().status == HostServiceStatus::Completed, "audio open completion status");
    const std::uint32_t handle = accepted.front().handle;
    check(handle != 0, "audio open returns handle");
    const AudioStreamRecord* stream = audio.stream(handle);
    check(stream != nullptr, "audio stream lookup");
    check(stream->app_instance_id == host.current_app_instance_id(), "audio stream instance");
    check(stream->state == AudioStreamState::Open, "audio stream starts open");
    check(stream->volume == 80, "audio open volume carried");
    check(stream->duration_ms == 3000, "audio duration carried");

    check(audio.submit_play(host, handle).accepted(), "audio play submitted");
    check(audio.complete_next(host), "audio play completed");
    accepted = pump(host);
    check(accepted.size() == 1, "audio play completion accepted");
    stream = audio.stream(handle);
    check(stream != nullptr && stream->state == AudioStreamState::Playing, "audio stream playing");

    check(audio.submit_set_volume(host, handle, 42).accepted(), "audio volume submitted");
    check(audio.complete_next(host), "audio volume completed");
    accepted = pump(host);
    check(accepted.size() == 1, "audio volume completion accepted");
    stream = audio.stream(handle);
    check(stream != nullptr && stream->volume == 42, "audio volume updated");

    check(audio.post_ended(host, handle), "audio ended event posted");
    accepted = pump(host);
    check(accepted.size() == 1, "audio ended completion accepted");
    check(accepted.front().job_id == 0, "audio ended is unsolicited event");
    check(accepted.front().status == HostServiceStatus::Completed, "audio ended status");
    stream = audio.stream(handle);
    check(stream != nullptr && stream->state == AudioStreamState::Ended, "audio stream ended");

    check(audio.submit_close(host, handle).accepted(), "audio close submitted");
    check(audio.complete_next(host), "audio close completed");
    accepted = pump(host);
    check(accepted.size() == 1, "audio close completion accepted");
    check(accepted.front().handle == handle, "audio close completion reports closed handle");
    check(audio.stream(handle) == nullptr, "audio close drops stream record");
    check(host.handles().lookup(handle) == nullptr, "audio close releases handle");
}

void audio_command_mock_enforces_stream_budget_and_lifecycle_cleanup() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.music", AppRole::App);
    AudioCommandMock audio(AudioPlaybackPolicy{true, 64, 1});
    check(audio.add_source(AudioSourceFixture{"app://tone-a.mp3", 100}), "audio budget fixture a");
    check(audio.add_source(AudioSourceFixture{"app://tone-b.mp3", 100}), "audio budget fixture b");

    const AppServiceSubmitResult first = audio.submit_open(host, "app://tone-a.mp3");
    check(first.accepted(), "audio first open accepted");
    const AppServiceSubmitResult second = audio.submit_open(host, "app://tone-b.mp3");
    check(second.status == AppServiceSubmitStatus::BudgetExceeded, "audio pending open counts against budget");
    check(audio.complete_next(host), "audio budget first completed");
    std::vector<HostServiceCompletion> accepted = pump(host);
    check(accepted.size() == 1 && accepted.front().handle != 0, "audio budget first handle");
    const std::uint32_t old_handle = accepted.front().handle;

    check(audio.submit_open(host, "app://tone-b.mp3").status == AppServiceSubmitStatus::BudgetExceeded,
          "audio active stream counts against budget");
    host.launch("org.example.next", AppRole::App);
    check(host.handles().lookup(old_handle) == nullptr, "audio lifecycle releases old app handle");
    check(audio.collect_released_streams(host) == 1, "audio mock collects stale stream records");
    check(audio.stream(old_handle) == nullptr, "audio stale stream removed");

    const AppServiceSubmitResult next = audio.submit_open(host, "app://tone-b.mp3");
    check(next.accepted(), "audio next app open accepted after cleanup");
    check(audio.complete_next(host), "audio next app open completed");
    accepted = pump(host);
    check(accepted.size() == 1 && accepted.front().handle != 0, "audio next app handle");
    const std::uint32_t next_handle = accepted.front().handle;
    check(audio.release_app_streams(host, host.current_app_instance_id()) == 1, "audio release app streams");
    check(audio.stream(next_handle) == nullptr, "audio release app stream record");
    check(host.handles().lookup(next_handle) == nullptr, "audio release app handle");
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
    image_surface_cache_requests_resolves_and_releases_surfaces();
    image_surface_cache_records_failed_decodes_without_retry_loop();
    image_surface_cache_keeps_transient_budget_rejections_retryable();
    image_surface_cache_records_permanent_request_rejections();
    image_failure_classification_is_specific();
    image_surface_cache_diagnostic_reports_pending_and_ready();
    image_surface_cache_evicts_lru_ready_surfaces();
    image_surface_cache_keeps_protected_display_list_surfaces();
    image_surface_cache_evicts_by_decoded_bytes();
    audio_command_mock_opens_controls_and_closes_streams();
    audio_command_mock_enforces_stream_budget_and_lifecycle_cleanup();
    kv_storage_is_app_private_and_async();
    kv_storage_enforces_budgets();
    local_storage_shadow_follows_web_storage_subset();
    local_storage_shadow_enforces_policy_without_host_io();
    service_workers_do_not_consume_other_service_requests();
    return 0;
}
