#include "app_runtime/app_installed_bundle.h"

#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API && defined(JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT)
#include "app_runtime/tests/runtime_shaped_text_test_fixture.h"
#endif
#if defined(JELLYFRAME_INSTALLED_PIPELINE_WORKER_TESTS)
#include "script/script_task_worker_runtime.h"
#include "app_runtime/script_task_frame_renderer.h"
#include "app_runtime/tests/runtime_font_test_fixture.h"
#endif

#include <array>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace jellyframe;

namespace {

template <std::size_t Capacity>
void copy_string(std::array<char, Capacity>& destination, const char* source) {
    const std::size_t length = std::strlen(source);
    assert(length < destination.size());
    std::memcpy(destination.data(), source, length);
}

AppRuntimeHost make_host() {
    return AppRuntimeHost(AppRuntimeHostOptions{4, 2, 4, 4096, 1});
}

class FakeLease final : public AppInstalledBundleLease {
public:
    FakeLease(const char* app_id, DeviceBundleAppRole role = DeviceBundleAppRole::App) {
        copy_string(descriptor_.summary.app_id, app_id);
        copy_string(descriptor_.summary.app_name, "Installed Test App");
        copy_string(descriptor_.summary.version_name, "1.0.0");
        copy_string(descriptor_.summary.entry_path, "/index.html");
        descriptor_.summary.role = role;
    }

    bool read_at(std::uint32_t offset, std::uint8_t* output, std::size_t size) const override {
        if (output == nullptr || offset > bundle_.size() || size > bundle_.size() - offset) return false;
        std::memcpy(output, bundle_.data() + offset, size);
        return size != 0;
    }

    const DeviceBundleDescriptor& descriptor() const override {
        return descriptor_;
    }

    void release() override {
        ++release_count;
    }

    bool copy_authenticated_resource_sha256(
        std::string_view app_path,
        std::array<std::uint8_t, 32>& sha256) const override {
        if (!authenticated_metadata || app_path == missing_authenticated_path) {
            sha256.fill(0);
            return false;
        }
        for (const auto& item : authenticated_resources) {
            if (item.first == app_path) {
                sha256 = item.second;
                return true;
            }
        }
        sha256.fill(0);
        return false;
    }

    void set_bundle(std::vector<std::uint8_t> bytes, const DeviceBundleDescriptor& descriptor) {
        bundle_ = std::move(bytes);
        descriptor_ = descriptor;
    }

    bool copy_authenticated_signer_key_id(std::array<std::uint8_t,16>& key) const override {
        key.fill(authenticated_signer ? 7 : 0); return authenticated_signer;
    }

    void set_authenticated_resource(std::string path, const RuntimeSha256& digest) {
        authenticated_resources.emplace_back(std::move(path), digest);
    }

    int release_count = 0;
    bool authenticated_metadata = false;
    bool authenticated_signer = false;
    std::string_view missing_authenticated_path;
    std::vector<std::pair<std::string, RuntimeSha256>> authenticated_resources;

private:
    DeviceBundleDescriptor descriptor_;
    std::vector<std::uint8_t> bundle_;
};

class FakeProvider final : public AppInstalledBundleProvider {
public:
    explicit FakeProvider(FakeLease& lease) : lease_(lease) {}

    DeviceBundleStatus acquire_installed_bundle(std::string_view app_id,
                                                AppInstalledBundleLease*& lease) override {
        ++acquire_count;
        last_requested_app_id = app_id;
        if (forced_status != DeviceBundleStatus::Ok) {
            lease = nullptr;
            return forced_status;
        }
        lease = &lease_;
        return DeviceBundleStatus::Ok;
    }

    FakeLease& lease_;
    DeviceBundleStatus forced_status = DeviceBundleStatus::Ok;
    int acquire_count = 0;
    std::string_view last_requested_app_id;
};

class FakeProtectedLauncher final : public AppProtectedLauncher {
public:
    explicit FakeProtectedLauncher(const FakeLease& lease) : lease_(lease) {}

    bool launch_protected_launcher(AppRuntimeHost& host, AppTeardownReason recovery_reason) override {
        ++launch_count;
        observed_release_count = lease_.release_count;
        observed_reason = recovery_reason;
        host.launch("org.jellyframe.launcher", AppRole::Launcher);
        return result;
    }

    const FakeLease& lease_;
    bool result = true;
    int launch_count = 0;
    int observed_release_count = 0;
    AppTeardownReason observed_reason = AppTeardownReason::None;
};

void put_le16(std::uint8_t* output, std::uint16_t value) {
    output[0] = static_cast<std::uint8_t>(value);
    output[1] = static_cast<std::uint8_t>(value >> 8u);
}

void put_le32(std::uint8_t* output, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) output[i] = static_cast<std::uint8_t>(value >> (i * 8u));
}

std::uint32_t test_crc32(const std::uint8_t* bytes, std::size_t size, std::size_t zero_begin = SIZE_MAX) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= i >= zero_begin && i < zero_begin + 4 ? 0 : bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

std::uint32_t test_fnv1a(std::string_view value) {
    std::uint32_t hash = 0x811c9dc5u;
    for (const unsigned char byte : value) hash = (hash ^ byte) * 0x01000193u;
    return hash;
}

std::vector<std::uint8_t> make_shaped_bundle(
    std::vector<std::pair<std::string, std::string>> resources, std::string pipeline = {},
    std::string entry_path = "/catalog.jfcat") {
    std::sort(resources.begin(), resources.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::string summary =
        "{\"entry\":\"/catalog.jfcat\",\"id\":\"org.example.shaped\",\"minJellyFrame\":\"0.6.0\","
        "\"minRenderCore\":\"0.6.0\",\"name\":\"Shaped Test\",\"role\":\"app\",\"script\":\"none\","
        "\"versionCode\":1,\"versionName\":\"1.0.0\"}";
    summary.replace(summary.find("/catalog.jfcat"), std::strlen("/catalog.jfcat"), entry_path);
    if (!pipeline.empty()) {
        const auto at=summary.find("\"none\""); summary.replace(at,6,"\"classic\"");
        summary.pop_back(); summary+=",\"textPipeline\":"+pipeline+"}";
    }
    const std::uint32_t summary_offset = kDeviceBundleHeaderBytes;
    const std::uint32_t index_offset = summary_offset + static_cast<std::uint32_t>(summary.size());
    const std::uint32_t strings_offset = index_offset +
        static_cast<std::uint32_t>(resources.size() * kDeviceBundleResourceEntryBytes);
    std::string strings;
    std::string payload;
    for (const auto& resource : resources) strings += resource.first;
    const std::uint32_t payload_offset = strings_offset + static_cast<std::uint32_t>(strings.size());
    for (const auto& resource : resources) payload += resource.second;
    std::vector<std::uint8_t> bundle(payload_offset + payload.size());
    std::memcpy(bundle.data(), "JFAPPV0\0", 8);
    put_le16(bundle.data() + 8, kDeviceBundleHeaderBytes);
    put_le16(bundle.data() + 10, pipeline.empty() ? kDeviceBundleScalarFormat : kDeviceBundleShapedFormat);
    put_le32(bundle.data() + 16, summary_offset);
    put_le32(bundle.data() + 20, static_cast<std::uint32_t>(summary.size()));
    put_le32(bundle.data() + 24, index_offset);
    put_le32(bundle.data() + 28, static_cast<std::uint32_t>(resources.size()));
    put_le32(bundle.data() + 32, strings_offset);
    put_le32(bundle.data() + 36, static_cast<std::uint32_t>(strings.size()));
    put_le32(bundle.data() + 40, payload_offset);
    put_le32(bundle.data() + 44, static_cast<std::uint32_t>(payload.size()));
    std::memcpy(bundle.data() + summary_offset, summary.data(), summary.size());
    std::memcpy(bundle.data() + strings_offset, strings.data(), strings.size());
    std::memcpy(bundle.data() + payload_offset, payload.data(), payload.size());
    std::uint32_t path_offset = 0;
    std::uint32_t resource_offset = 0;
    for (std::size_t i = 0; i < resources.size(); ++i) {
        std::uint8_t* entry = bundle.data() + index_offset + i * kDeviceBundleResourceEntryBytes;
        const auto& [path, bytes] = resources[i];
        put_le32(entry, test_fnv1a(path));
        put_le32(entry + 4, path_offset);
        put_le16(entry + 8, static_cast<std::uint16_t>(path.size()));
        put_le16(entry + 10, 1);
        put_le32(entry + 12, resource_offset);
        put_le32(entry + 16, static_cast<std::uint32_t>(bytes.size()));
        put_le32(entry + 20, test_crc32(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
        path_offset += static_cast<std::uint32_t>(path.size());
        resource_offset += static_cast<std::uint32_t>(bytes.size());
    }
    put_le32(bundle.data() + 48, test_crc32(bundle.data(), bundle.size(), 48));
    return bundle;
}

#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API && defined(JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT)
void install_shaped_test_assets(FakeLease& lease, runtime_test_shaped_text::Assets& assets, std::string pipeline = {}) {
    const std::array<std::pair<std::string, std::string_view>, 5> resources = {{
        {"/font.jffont2", assets.font}, {"/catalog.jfcat", assets.catalog},
        {"/companion.jfshr", assets.companion}, {"/tables.bin", assets.tables},
        {"/eligibility.bin", assets.eligibility},
    }};
    std::vector<std::pair<std::string, std::string>> bundle_resources;
    for (const auto& item : resources) bundle_resources.emplace_back(item.first, std::string(item.second));
    bundle_resources.emplace_back("/index.html", "<body><div id='label'>A A</div></body>");
    auto bundle = make_shaped_bundle(std::move(bundle_resources),std::move(pipeline),"/index.html");
    DeviceBundleMemoryReader reader(bundle.data(), bundle.size());
    DeviceBundleInspectionWorkspace workspace;
    DeviceBundleDescriptor descriptor;
    DeviceBundleValidationPolicy policy;
    assert(inspect_device_bundle(reader, static_cast<std::uint32_t>(bundle.size()), policy, workspace, descriptor) ==
           DeviceBundleStatus::Ok);
    lease.set_bundle(std::move(bundle), descriptor);
    lease.authenticated_metadata = true;
    for (const auto& item : resources) {
        lease.set_authenticated_resource(std::string(item.first), assets.digest_for(item.first));
    }
}
#endif

void installed_launch_replaces_the_old_instance_before_releasing_its_lease() {
    AppRuntimeHost host = make_host();
    const AppInstance old = host.launch("org.example.old", AppRole::App);
    FakeLease lease("org.example.installed");
    FakeProvider provider(lease);
    AppInstalledBundleBinding binding(provider);

    const AppInstalledBundleLaunchResult result = binding.launch(host, "org.example.installed");
    assert(result.launched());
    assert(result.previous_teardown.app_instance_id == old.id);
    assert(result.previous_teardown.reason == AppTeardownReason::AppSwitch);
    assert(host.current().app_id == "org.example.installed");
    assert(binding.has_active_bundle());
    assert(provider.acquire_count == 1);
    assert(provider.last_requested_app_id == "org.example.installed");
    assert(lease.release_count == 0);

    const AppTeardownResult teardown = binding.terminate_current(host, AppTeardownReason::UserKill);
    assert(teardown.app_instance_id == result.instance.id);
    assert(teardown.reason == AppTeardownReason::UserKill);
    assert(!host.current().active());
    assert(!binding.has_active_bundle());
    assert(lease.release_count == 1);
}

void source_errors_and_identity_mismatches_leave_the_current_app_untouched() {
    AppRuntimeHost host = make_host();
    const AppInstance old = host.launch("org.example.old", AppRole::App);
    FakeLease lease("org.example.actual");
    FakeProvider provider(lease);
    AppInstalledBundleBinding binding(provider);

    AppInstalledBundleLaunchResult mismatch = binding.launch(host, "org.example.requested");
    assert(mismatch.status == AppInstalledBundleStatus::IdentityMismatch);
    assert(host.current().id == old.id);
    assert(lease.release_count == 1);
    assert(!binding.has_active_bundle());

    provider.forced_status = DeviceBundleStatus::ReadFailed;
    AppInstalledBundleLaunchResult unavailable = binding.launch(host, "org.example.requested");
    assert(unavailable.status == AppInstalledBundleStatus::AcquireFailed);
    assert(unavailable.bundle_status == DeviceBundleStatus::ReadFailed);
    assert(host.current().id == old.id);
    assert(lease.release_count == 1);
}

void reloading_an_app_never_releases_a_reused_active_lease() {
    AppRuntimeHost host = make_host();
    FakeLease lease("org.example.reload");
    FakeProvider provider(lease);
    AppInstalledBundleBinding binding(provider);
    const AppInstalledBundleLaunchResult first = binding.launch(host, "org.example.reload");
    assert(first.launched());
    const AppInstalledBundleLaunchResult second = binding.launch(host, "org.example.reload");
    assert(second.launched());
    assert(second.previous_teardown.app_instance_id == first.instance.id);
    assert(second.instance.id != first.instance.id);
    assert(lease.release_count == 0);

    binding.terminate_current(host, AppTeardownReason::NormalExit);
    assert(lease.release_count == 1);
}

void recovery_releases_the_bundle_after_host_teardown_and_before_launcher_start() {
    AppRuntimeHost host = make_host();
    FakeLease lease("org.example.recover");
    FakeProvider provider(lease);
    FakeProtectedLauncher launcher(lease);
    AppInstalledBundleBinding binding(provider, &launcher);
    const AppInstalledBundleLaunchResult launched = binding.launch(host, "org.example.recover");
    assert(launched.launched());

    const AppInstalledBundleRecoveryResult recovered =
        binding.recover_to_protected_launcher(host, AppTeardownReason::RuntimeError);
    assert(recovered.status == AppInstalledBundleStatus::Ok);
    assert(recovered.launcher_started);
    assert(recovered.teardown.app_instance_id == launched.instance.id);
    assert(recovered.teardown.reason == AppTeardownReason::RuntimeError);
    assert(lease.release_count == 1);
    assert(launcher.observed_release_count == 1);
    assert(launcher.observed_reason == AppTeardownReason::RuntimeError);
    assert(host.current().role == AppRole::Launcher);
    assert(host.current().app_id == "org.jellyframe.launcher");
}

void active_resource_queries_require_an_active_lease() {
    FakeLease lease("org.example.resources");
    FakeProvider provider(lease);
    AppInstalledBundleBinding binding(provider);
    std::array<std::uint8_t, 8> output{};
    std::size_t output_size = 9;
    assert(binding.read_active_resource("/index.html", output.data(), output.size(), output_size) ==
           DeviceBundleStatus::ResourceNotFound);
    assert(output_size == 0);
    DeviceBundleInspectionWorkspace workspace;
    DeviceBundleFontTable fonts;
    fonts.count = 1;
    assert(binding.read_active_fonts(workspace, fonts) == DeviceBundleStatus::ResourceNotFound);
    assert(fonts.count == 0);
}

void lease_authentication_metadata_defaults_to_refusal() {
    FakeLease lease("org.example.metadata");
    std::array<std::uint8_t, 32> digest{};
    digest.fill(0xa5);
    assert(!lease.copy_authenticated_resource_sha256("/resource", digest));
    for (const std::uint8_t byte : digest) assert(byte == 0);
    std::array<std::uint8_t, 16> signer{};
    signer.fill(0xa5);
    assert(!lease.copy_authenticated_signer_key_id(signer));
    for (const std::uint8_t byte : signer) assert(byte == 0);
}

void installed_text_pipeline_refuses_unavailable_or_invalid_profile() {
    std::string pipeline = R"({"kind":"jfsp1-block-v11","font":"/font.jffont2","catalog":"/catalog.jfcat","companion":"/companion.jfshr","tables":"/tables.bin","eligibility":"/eligibility.bin","sourceIdentity":"1111111111111111111111111111111111111111111111111111111111111111","profileIdentity":"2222222222222222222222222222222222222222222222222222222222222222","glyphCount":30900,"resourceId":555,"rasterSize":16,"weight":400,"selection":0})";
#if JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API
    pipeline.replace(pipeline.find("jfsp1-block-v11"), 15, "jfsp2-block-v12");
#endif
    auto bytes = make_shaped_bundle({{"/catalog.jfcat", "placeholder"}}, pipeline);
    DeviceBundleInspectionWorkspace workspace;
    DeviceBundleDescriptor descriptor;
    DeviceBundleMemoryReader reader(bytes.data(), bytes.size());
    assert(inspect_device_bundle(reader, static_cast<std::uint32_t>(bytes.size()), {}, workspace, descriptor) ==
           DeviceBundleStatus::Ok);
    auto host = make_host();
    FakeLease lease("org.example.shaped");
    lease.set_bundle(bytes, descriptor);
    FakeProvider provider(lease);
    AppInstalledBundleBinding binding(provider);
    assert(binding.launch(host, "org.example.shaped").launched());
    assert(binding.select_active_text_pipeline(workspace).status == AppInstalledTextPipelineStatus::Unauthenticated);
    lease.authenticated_signer = true;
    const auto refused = binding.select_active_text_pipeline(workspace);
#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API && JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
    assert(refused.status == AppInstalledTextPipelineStatus::BindingRejected && !refused.binding);
#else
    assert(refused.status == AppInstalledTextPipelineStatus::Unsupported);
#endif
    assert(refused.frame_version == 0 && refused.font_family_hash == 0);
    bytes[descriptor.summary_offset] = '[';
    lease.set_bundle(std::move(bytes), descriptor);
    assert(binding.select_active_text_pipeline(workspace).status == AppInstalledTextPipelineStatus::InvalidSummary);
    binding.terminate_current(host, AppTeardownReason::NormalExit);
}

#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API
AppInstalledShapedTextRequest shaped_request(const RuntimeShapedTextManifest& manifest) {
    AppInstalledShapedTextRequest request;
    request.font_path = "/font.jffont2";
    request.shaping_catalog_path = "/catalog.jfcat";
    request.shaping_companion_path = "/companion.jfshr";
    request.normalization_tables_path = "/tables.bin";
    request.normalization_eligibility_path = "/eligibility.bin";
    request.source_identity = manifest.source_identity;
    request.profile_identity = manifest.profile_identity;
    request.shaping_glyph_count = manifest.shaping_glyph_count;
    request.resource_id = manifest.resource_id;
    request.raster_size = manifest.raster_size;
    request.weight = manifest.weight;
    request.selection = manifest.selection;
    return request;
}

void installed_shaped_binding_uses_lease_digests_and_resource_bytes() {
#if defined(JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT)
    runtime_test_shaped_text::Assets assets("regular", 555);
    assert(assets.catalog.size() > 320u * 1024u);
    AppRuntimeHost host = make_host();
    FakeLease lease("org.example.shaped");
    install_shaped_test_assets(lease, assets);
    FakeProvider provider(lease);
    AppInstalledBundleBinding binding(provider);
    assert(binding.launch(host, "org.example.shaped").launched());

    const AppInstalledShapedTextRequest request = shaped_request(assets.manifest);
    const AppInstalledShapedTextResult result = binding.create_active_shaped_text_binding(request);
    assert(result.status == AppInstalledShapedTextStatus::Ready);
    assert(result.binding_status == RuntimeShapedTextStatus::Ready);
    assert(result.binding && result.binding->valid());

    binding.terminate_current(host, AppTeardownReason::NormalExit);
    assert(lease.release_count == 1);
    lease.set_bundle({}, {});
    lease.authenticated_resources.clear();
    assets.clear_source_buffers();
    std::string packet;
    runtime_shaped::Metrics metrics;
    std::uint64_t work = 1000000;
    assert(result.binding->prepare(16000, "A", work, packet, metrics) == runtime_shaped::Status::ok);
    work = 1000000;
    assert(result.binding->validate_packet(packet, work) == runtime_shaped::Status::ok);
#if JELLYFRAME_RUNTIME_SHAPED_PARAGRAPH_API
    runtime_shaped::Paragraph paragraph;
    runtime_shaped::ParagraphOptions options;
    options.width_q1000 = metrics.advance_x;
    runtime_shaped::ParagraphUsage usage;
    work = 32U * 1024U * 1024U;
    assert(result.binding->layout_paragraph("A A A", options, {}, work, paragraph, &usage) ==
           runtime_shaped::Status::ok);
    assert(paragraph.valid() && paragraph.lines().size() == 3 && paragraph.text() == "A A A");
    assert(paragraph.binding().content_identity() == result.binding->core_binding().content_identity());
    assert(usage.shape_calls > 3 && usage.work > 0);
    auto retained = paragraph;
    runtime_shaped::ParagraphLimits limited;
    limited.lines = 1;
    work = 32U * 1024U * 1024U;
    assert(result.binding->layout_paragraph("A A A", options, limited, work, paragraph, &usage) ==
           runtime_shaped::Status::limit);
    assert(paragraph.text().data() == retained.text().data() && usage.work > 0);
#endif
#endif
}


void installed_text_pipeline_selects_authenticated_matching_resources() {
#if defined(JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT)
    runtime_test_shaped_text::Assets assets("regular",555);
    const auto hex=[](const RuntimeSha256& digest) {
        std::string value; for (auto b:digest) {value+="0123456789abcdef"[b>>4];value+="0123456789abcdef"[b&15];} return value;
    };
    auto pipeline=std::string(R"({"kind":"jfsp1-block-v11","font":"/font.jffont2","catalog":"/catalog.jfcat","companion":"/companion.jfshr","tables":"/tables.bin","eligibility":"/eligibility.bin","sourceIdentity":")")+
        hex(assets.manifest.source_identity)+R"(","profileIdentity":")"+hex(assets.manifest.profile_identity)+
        R"(","glyphCount":30900,"resourceId":555,"rasterSize":16,"weight":400,"selection":0,"fontFamilyHash":123})";
#if JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API
    pipeline.replace(pipeline.find("jfsp1-block-v11"), 15, "jfsp2-block-v12");
#endif
    auto host=make_host(); FakeLease lease("org.example.shaped"); FakeProvider provider(lease);
    AppInstalledBundleBinding installed(provider); DeviceBundleInspectionWorkspace workspace;
    assert(installed.select_active_text_pipeline(workspace).status == AppInstalledTextPipelineStatus::NoActiveLease);
    install_shaped_test_assets(lease,assets);
    assert(installed.launch(host,"org.example.shaped").launched());
    assert(installed.select_active_text_pipeline(workspace).status == AppInstalledTextPipelineStatus::Scalar);
    install_shaped_test_assets(lease,assets,pipeline);
    assert(installed.select_active_text_pipeline(workspace).status == AppInstalledTextPipelineStatus::Unauthenticated);
    lease.authenticated_signer=true;
    const auto ready=installed.select_active_text_pipeline(workspace);
    assert(ready.status == AppInstalledTextPipelineStatus::Ready && ready.frame_version == kRuntimeShapedFrameVersion &&
           ready.font_family_hash == 123 && ready.binding && ready.binding->valid());
    auto wrong_protocol = pipeline;
    wrong_protocol.replace(wrong_protocol.find(JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API ? "jfsp2-block-v12" : "jfsp1-block-v11"),
        15, JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API ? "jfsp1-block-v11" : "jfsp2-block-v12");
    install_shaped_test_assets(lease, assets, wrong_protocol);
    const auto unsupported = installed.select_active_text_pipeline(workspace);
    assert(unsupported.status == AppInstalledTextPipelineStatus::Unsupported &&
           !unsupported.binding && unsupported.frame_version == 0);
    install_shaped_test_assets(lease, assets, pipeline);
    const auto limited = installed.select_active_text_pipeline(workspace, 1024);
    assert(limited.status == AppInstalledTextPipelineStatus::BindingRejected && !limited.binding && !limited.frame_version);
    lease.missing_authenticated_path="/tables.bin";
    const auto refused=installed.select_active_text_pipeline(workspace);
    assert(refused.status == AppInstalledTextPipelineStatus::BindingRejected && !refused.binding && !refused.frame_version);
    lease.missing_authenticated_path = {};
#if defined(JELLYFRAME_INSTALLED_PIPELINE_WORKER_TESTS) && JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
    // Exercise the same selection through an actual VM/worker and the UI
    // accepted-frame owner, then destroy the package before repainting.
    const auto default_family = pipeline.substr(0, pipeline.find(",\"fontFamilyHash\"")) + "}";
    install_shaped_test_assets(lease, assets, default_family);
    auto selected = installed.select_active_text_pipeline(workspace, 2u * 1024u * 1024u);
    assert(selected.status == AppInstalledTextPipelineStatus::Ready && selected.font_family_hash == 0);
    std::array<std::uint8_t, 1024> entry{};
    std::size_t entry_size = 0;
    assert(installed.read_active_resource("/index.html", entry.data(), entry.size(), entry_size) == DeviceBundleStatus::Ok);
    ScriptTaskSupervisorOptions supervisor_options;
    supervisor_options.worker_inbox = {8, 4096};
    supervisor_options.frame_mailbox = {4, 4096};
    supervisor_options.frame_leases = {4, 32768, 65536};
    ScriptTaskSupervisor supervisor(supervisor_options);
    runtime_test_fonts::Fonts fonts;
    const auto session = supervisor.begin(host.current().id, fonts.catalog, {}, selected.binding);
    ScriptTaskWorkerRuntimeOptions options;
    options.viewport = {0,0,64,64};
    options.frame_codec = {128,16384,16,32768};
    options.frame_codec.version = selected.frame_version;
    options.frame_codec.max_groups = 16;
    options.text_catalog = fonts.catalog;
    options.shaped_text_binding = selected.binding;
    options.shaped_layout.font_family_hash = selected.font_family_hash;
    ScriptTaskAcceptedAppFrame accepted;
    {
        ScriptTaskWorkerRuntime worker(session, options);
        assert(worker.initialize(supervisor, std::string(reinterpret_cast<char*>(entry.data()),entry_size),
            "*{margin:0;padding:0;font-weight:400}body{background:#ffffff}"
            "div{display:block;width:40px;font-size:16px;line-height:18px;color:#224488}") ==
            ScriptTaskWorkerRuntimeInitStatus::Accepted);
        assert(worker.publish_frame(supervisor).accepted());
        assert(take_script_task_app_frame(supervisor,session,options.frame_codec,accepted) ==
            ScriptTaskAppFrameTakeStatus::Accepted);
        assert(accepted.shaped_text_binding() == selected.binding && !accepted.frame().shaped_text.empty());
        const auto packet = accepted.frame().shaped_text.front().packet;
        assert(worker.eval_with_supervisor(supervisor,"document.getElementById('label').textContent='A';").ok);
        assert(worker.publish_frame(supervisor).accepted());
        ScriptTaskAcceptedAppFrame changed;
        assert(take_script_task_app_frame(supervisor,session,options.frame_codec,changed) ==
            ScriptTaskAppFrameTakeStatus::Accepted);
        assert(changed.frame().shaped_text.front().packet != packet &&
               accepted.frame().shaped_text.front().packet == packet);
        worker.stop();
    }
    installed.terminate_current(host,AppTeardownReason::NormalExit);
    lease.set_bundle({},{});
    assets.clear_source_buffers();
    selected.binding.reset();
    options.shaped_text_binding.reset();
    supervisor.begin_teardown(session);
    supervisor.complete_teardown(session);
    FrameBuffer pixels(64,64,{255,255,255,255});
    ScriptTaskFrameRenderer renderer;
    assert(renderer.render_into(accepted,pixels,{255,255,255,255}));
    assert(std::any_of(pixels.pixels.begin(),pixels.pixels.end(),[](Color color){return color.r < 100;}));
#endif
    installed.terminate_current(host,AppTeardownReason::NormalExit);
    lease.set_bundle({},{}); assets.clear_source_buffers();
    std::uint64_t work=ready.binding->work_limit(); std::string packet; runtime_shaped::Metrics metrics;
    assert(ready.binding->prepare(16000,"A",work,packet,metrics) == runtime_shaped::Status::ok);
#endif
}

void installed_shaped_binding_enforces_aggregate_resource_budget() {
#if defined(JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT)
    runtime_test_shaped_text::Assets assets("regular", 557);
    AppRuntimeHost host = make_host();
    FakeLease lease("org.example.shaped");
    install_shaped_test_assets(lease, assets);
    FakeProvider provider(lease);
    AppInstalledBundleBinding binding(provider);
    assert(binding.launch(host, "org.example.shaped").launched());
    AppInstalledShapedTextRequest request = shaped_request(assets.manifest);
    request.limits.core.bytes = 320u * 1024u;
    const AppInstalledShapedTextResult result = binding.create_active_shaped_text_binding(request);
    assert(result.status == AppInstalledShapedTextStatus::ResourceLimit);
    assert(result.binding_status == RuntimeShapedTextStatus::ResourceLimit);
    assert(!result.binding);
    binding.terminate_current(host, AppTeardownReason::NormalExit);
#endif
}

void installed_shaped_binding_refuses_missing_lease_metadata() {
    AppRuntimeHost host = make_host();
    FakeLease lease("org.example.shaped");
    FakeProvider provider(lease);
    AppInstalledBundleBinding binding(provider);
    assert(binding.launch(host, "org.example.shaped").launched());
    AppInstalledShapedTextRequest request;
    request.font_path = "/font.jffont2";
    request.shaping_catalog_path = "/catalog.jfcat";
    request.shaping_companion_path = "/companion.jfshr";
    request.normalization_tables_path = "/tables.bin";
    request.normalization_eligibility_path = "/eligibility.bin";
    const AppInstalledShapedTextResult ordinary = binding.create_active_shaped_text_binding(request);
    assert(ordinary.status == AppInstalledShapedTextStatus::UnauthenticatedMetadata);
    assert(!ordinary.binding);

    lease.authenticated_metadata = true;
    RuntimeSha256 digest{};
    digest.fill(1);
    lease.set_authenticated_resource(std::string(request.font_path), digest);
    lease.set_authenticated_resource(std::string(request.shaping_catalog_path), digest);
    lease.set_authenticated_resource(std::string(request.shaping_companion_path), digest);
    lease.set_authenticated_resource(std::string(request.normalization_tables_path), digest);
    lease.set_authenticated_resource(std::string(request.normalization_eligibility_path), digest);
    lease.missing_authenticated_path = request.normalization_eligibility_path;

    const AppInstalledShapedTextResult result = binding.create_active_shaped_text_binding(request);
    assert(result.status == AppInstalledShapedTextStatus::UnauthenticatedMetadata);
    assert(!result.binding);
    assert(binding.has_active_bundle());
    assert(lease.release_count == 0);

    binding.terminate_current(host, AppTeardownReason::NormalExit);
    assert(!binding.has_active_bundle());
    assert(lease.release_count == 1);
}

void installed_shaped_binding_refuses_missing_resource_path() {
    AppRuntimeHost host = make_host();
    FakeLease lease("org.example.shaped");
    FakeProvider provider(lease);
    AppInstalledBundleBinding binding(provider);
    assert(binding.launch(host, "org.example.shaped").launched());
    lease.authenticated_metadata = true;
    AppInstalledShapedTextRequest request;
    request.font_path = "/missing-font.jffont2";
    request.shaping_catalog_path = "/catalog.jfcat";
    request.shaping_companion_path = "/companion.jfshr";
    request.normalization_tables_path = "/tables.bin";
    request.normalization_eligibility_path = "/eligibility.bin";
    RuntimeSha256 digest{};
    digest.fill(1);
    lease.set_authenticated_resource(std::string(request.font_path), digest);
    lease.set_authenticated_resource(std::string(request.shaping_catalog_path), digest);
    lease.set_authenticated_resource(std::string(request.shaping_companion_path), digest);
    lease.set_authenticated_resource(std::string(request.normalization_tables_path), digest);
    lease.set_authenticated_resource(std::string(request.normalization_eligibility_path), digest);
    const AppInstalledShapedTextResult result = binding.create_active_shaped_text_binding(request);
    assert(result.status == AppInstalledShapedTextStatus::ResourceReadFailed);
    assert(result.bundle_status == DeviceBundleStatus::ResourceNotFound);
    assert(!result.binding);
    binding.terminate_current(host, AppTeardownReason::NormalExit);
}

void installed_shaped_binding_refuses_invalid_resource_path() {
    AppRuntimeHost host = make_host();
    FakeLease lease("org.example.shaped");
    FakeProvider provider(lease);
    AppInstalledBundleBinding binding(provider);
    assert(binding.launch(host, "org.example.shaped").launched());
    AppInstalledShapedTextRequest request;
    request.font_path = "../font.jffont2";
    request.shaping_catalog_path = "/catalog.jfcat";
    request.shaping_companion_path = "/companion.jfshr";
    request.normalization_tables_path = "/tables.bin";
    request.normalization_eligibility_path = "/eligibility.bin";
    const AppInstalledShapedTextResult result = binding.create_active_shaped_text_binding(request);
    assert(result.status == AppInstalledShapedTextStatus::InvalidArgument);
    assert(!result.binding);
    binding.terminate_current(host, AppTeardownReason::NormalExit);
}

void installed_shaped_binding_refuses_wrong_font_digest() {
#if defined(JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT)
    runtime_test_shaped_text::Assets assets("regular", 556);
    AppRuntimeHost host = make_host();
    FakeLease lease("org.example.shaped");
    install_shaped_test_assets(lease, assets);
    const RuntimeSha256 real_digest = assets.manifest.font_resource_identity;
    RuntimeSha256 wrong_digest = real_digest;
    wrong_digest[0] ^= 0x80;
    lease.authenticated_resources.clear();
    lease.set_authenticated_resource("/font.jffont2", wrong_digest);
    for (const auto& item : std::array<std::pair<std::string_view, std::string_view>, 4>{{
             {"/catalog.jfcat", assets.catalog}, {"/companion.jfshr", assets.companion},
             {"/tables.bin", assets.tables}, {"/eligibility.bin", assets.eligibility}}}) {
        lease.set_authenticated_resource(std::string(item.first), assets.digest_for(item.first));
    }
    FakeProvider provider(lease);
    AppInstalledBundleBinding binding(provider);
    assert(binding.launch(host, "org.example.shaped").launched());
    const AppInstalledShapedTextResult result = binding.create_active_shaped_text_binding(
        shaped_request(assets.manifest));
    assert(result.status == AppInstalledShapedTextStatus::BindingRejected);
    assert(result.binding_status == RuntimeShapedTextStatus::CoreRejected);
    assert(!result.binding);
    binding.terminate_current(host, AppTeardownReason::NormalExit);
#endif
}
#endif

} // namespace

int main() {
    installed_launch_replaces_the_old_instance_before_releasing_its_lease();
    source_errors_and_identity_mismatches_leave_the_current_app_untouched();
    reloading_an_app_never_releases_a_reused_active_lease();
    recovery_releases_the_bundle_after_host_teardown_and_before_launcher_start();
    active_resource_queries_require_an_active_lease();
    lease_authentication_metadata_defaults_to_refusal();
    installed_text_pipeline_refuses_unavailable_or_invalid_profile();
#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API
    installed_shaped_binding_uses_lease_digests_and_resource_bytes();
    installed_text_pipeline_selects_authenticated_matching_resources();
    installed_shaped_binding_enforces_aggregate_resource_budget();
    installed_shaped_binding_refuses_missing_lease_metadata();
    installed_shaped_binding_refuses_missing_resource_path();
    installed_shaped_binding_refuses_invalid_resource_path();
    installed_shaped_binding_refuses_wrong_font_digest();
#endif
    return 0;
}
