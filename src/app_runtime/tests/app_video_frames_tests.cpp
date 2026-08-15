#include "app_runtime/app_video_frames.h"

#include <cstdlib>
#include <iostream>
#include <vector>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "app_video_frames check failed: " << message << '\n';
        std::abort();
    }
}

AppRuntimeHost make_host(std::size_t handles = 8, std::size_t bytes = 4096) {
    return AppRuntimeHost(AppRuntimeHostOptions{8, 4, handles, bytes, 1});
}

std::vector<std::uint8_t> rgb565_2x2(std::uint8_t value) {
    return std::vector<std::uint8_t>(8, value);
}

std::vector<HostServiceCompletion> pump(AppRuntimeHost& host) {
    std::vector<HostServiceCompletion> completions;
    host.pump_frame_completions(completions);
    return completions;
}

AppVideoFrameProviderMock ready_provider() {
    AppVideoFrameProviderMock provider(AppVideoFramePolicy{
        true, true, false, HostPixelFormat::Rgb565, 8, 8, 15, 128,
    });
    check(provider.add_fixture(AppVideoFrameFixture{
        "/preview.mjpg", AppVideoFrameCodec::Mjpeg, 2, 2, 2,
        HostPixelFormat::Rgb565, 100, rgb565_2x2(7),
    }), "mjpeg fixture accepted");
    return provider;
}

void video_frame_requires_policy_and_bounds_codec() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.preview", AppRole::App);
    AppVideoFrameProviderMock provider;
    check(provider.request_next_frame(host, {"/preview.mjpg", AppVideoFrameCodec::Mjpeg}).status ==
              AppServiceSubmitStatus::CapabilityDenied,
          "video capability gate");

    provider = ready_provider();
    check(provider.request_next_frame(host, {"/preview.mjpg", AppVideoFrameCodec::H264Baseline}).status ==
              AppServiceSubmitStatus::InvalidInput,
          "h264 policy gate");
    check(provider.request_next_frame(host, {"", AppVideoFrameCodec::Mjpeg}).status ==
              AppServiceSubmitStatus::InvalidInput,
          "empty source rejected");
}

void video_frame_returns_latest_surface_and_drops_old_one() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.preview", AppRole::App);
    AppVideoFrameProviderMock provider = ready_provider();

    check(provider.request_next_frame(host, {"/preview.mjpg", AppVideoFrameCodec::Mjpeg, 100}).accepted(),
          "first frame request accepted");
    check(provider.request_next_frame(host, {"/preview.mjpg", AppVideoFrameCodec::Mjpeg}).status ==
              AppServiceSubmitStatus::QueueFull,
          "same source does not queue stale frames");
    check(provider.complete_next(host), "first frame completed");
    const auto first = pump(host);
    check(first.size() == 1 && first[0].result_handle != 0 && first[0].kind == HostServiceJobKind::VideoFrameDecode,
          "first frame completion returns handle");
    const std::uint32_t old_handle = first[0].result_handle;
    const AppVideoFrameRecord* old_frame = provider.frame(old_handle);
    check(old_frame != nullptr && old_frame->pts_ms == 100 && !old_frame->dropped_previous,
          "first frame record is bounded");

    check(provider.request_next_frame(host, {"/preview.mjpg", AppVideoFrameCodec::Mjpeg}).accepted(),
          "second frame request accepted");
    check(provider.complete_next(host), "second frame completed");
    const auto second = pump(host);
    check(second.size() == 1 && second[0].result_handle != 0, "second frame completion returns handle");
    check(provider.frame(old_handle) == nullptr && !host.handles().contains(old_handle),
          "old latest frame released before replacement");
    const AppVideoFrameRecord* latest = provider.frame(second[0].result_handle);
    check(latest != nullptr && latest->dropped_previous && provider.latest_frame_handle("/preview.mjpg") == second[0].result_handle,
          "latest frame replaces stale frame");
    check(provider.release_frame(host, second[0].result_handle), "latest frame explicitly released");
}

void video_frame_keeps_displayed_frame_when_replacement_has_no_handle_budget() {
    AppRuntimeHost host = make_host(1, 64);
    host.launch("org.example.preview", AppRole::App);
    AppVideoFrameProviderMock provider = ready_provider();
    check(provider.request_next_frame(host, {"/preview.mjpg", AppVideoFrameCodec::Mjpeg}).accepted(),
          "initial frame request accepted");
    check(provider.complete_next(host), "initial frame completed");
    const auto first = pump(host);
    check(first.size() == 1 && first[0].result_handle != 0, "initial frame handle allocated");
    const std::uint32_t old_handle = first[0].result_handle;

    check(provider.request_next_frame(host, {"/preview.mjpg", AppVideoFrameCodec::Mjpeg}).accepted(),
          "replacement request accepted");
    check(provider.complete_next(host), "replacement completion posted");
    const auto replacement = pump(host);
    check(replacement.size() == 1 && replacement[0].status == HostServiceStatus::BudgetExceeded,
          "replacement reports handle budget exhaustion");
    check(provider.frame(old_handle) != nullptr && host.handles().contains(old_handle) &&
              provider.latest_frame_handle("/preview.mjpg") == old_handle,
          "old frame remains visible when replacement cannot allocate");
    check(provider.release_frame(host, old_handle), "old frame released after budget failure");
}

void video_frame_cleans_stale_worker_requests_and_maps_policy() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.preview.one", AppRole::App);
    AppVideoFrameProviderMock provider = ready_provider();
    check(provider.request_next_frame(host, {"/preview.mjpg", AppVideoFrameCodec::Mjpeg}).accepted(),
          "frame request accepted");
    HostServiceRequest request;
    check(host.pop_worker_request(HostServiceJobKind::VideoFrameDecode, request), "worker owns frame request");
    host.launch("org.example.preview.two", AppRole::App);
    check(provider.collect_stale_pending_frames(host) == 1, "stale worker frame request collected");
    check(provider.complete_request(host, request).status == HostServiceStatus::Cancelled,
          "stale completion cannot resurrect old frame");

    HostDeviceCapabilities capabilities;
    capabilities.media.supports_video_decode = true;
    capabilities.media.supports_mjpeg = true;
    capabilities.media.preferred_video_frame_format = HostPixelFormat::Rgb565;
    capabilities.media.max_video_width = 160;
    capabilities.media.max_video_height = 160;
    capabilities.media.max_video_fps = 15;
    capabilities.media.max_video_frame_bytes = 51200;
    const AppServiceHostProfile profile = app_service_host_profile_from_capabilities(capabilities);
    AppServiceManifestCapabilities manifest;
    AppServicePolicies policies = app_service_policies_for_app(manifest, profile);
    check(!policies.video_frame.enabled, "video policy requires manifest request");
    manifest.video_frame = true;
    policies = app_service_policies_for_app(manifest, profile);
    check(policies.video_frame.enabled && policies.video_frame.allow_mjpeg && !policies.video_frame.allow_h264 &&
              policies.video_frame.max_fps == 15,
          "video policy maps explicit host profile");
}

} // namespace

int main() {
    video_frame_requires_policy_and_bounds_codec();
    video_frame_returns_latest_surface_and_drops_old_one();
    video_frame_keeps_displayed_frame_when_replacement_has_no_handle_budget();
    video_frame_cleans_stale_worker_requests_and_maps_policy();
    return 0;
}
