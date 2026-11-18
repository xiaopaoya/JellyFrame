#include "host_store_fixture.h"
#include "jellyframe_esp32s3_installed_frame_admission.h"
#include "script/script_task_worker_runtime.h"

namespace {
std::vector<std::uint8_t> read_resource(AppInstalledBundleBinding& binding, const char* path) {
    std::vector<std::uint8_t> bytes(16384);
    std::size_t size = 0;
    CHECK(binding.read_active_resource(path, bytes.data(), bytes.size(), size) == DeviceBundleStatus::Ok);
    CHECK(size > 0);
    bytes.resize(size);
    return bytes;
}

FrameBuffer paint(const ScriptTaskAcceptedAppFrame& frame) {
    constexpr Color white{255, 255, 255, 255};
    FrameBuffer pixels(172, 320, white);
    VectorDiagnosticSink diagnostics;
    ScriptTaskFrameRendererOptions options;
    options.max_temporary_pixels = 2 * pixels.pixels.size();
    options.diagnostics = &diagnostics;
    ScriptTaskFrameRenderStatus status;
    const auto rendered = render_installed_core_text_frame(ScriptTaskFrameRenderer(options), frame, pixels, white,
                                                          nullptr, 0, nullptr, &status);
    if (!rendered) {
        std::fprintf(stderr, "render status=%u\n", unsigned(status));
        for (const auto& row : diagnostics.diagnostics())
            std::fprintf(stderr, "%s: %s (%s)\n", row.code.c_str(), row.message.c_str(), row.detail.c_str());
    }
    CHECK(rendered);
    CHECK(status == ScriptTaskFrameRenderStatus::Accepted);
    return pixels;
}

bool same_color(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// The port stores decoded BMP colors in RGB565.
constexpr Color mark{230, 137, 32, 255};
}  // namespace

int main(int argc, char** argv) {
    CHECK(argc == 3);
    auto store = boot();
    const auto package = read(argv[1]);
    DeviceInstallTransaction transaction({store->max_bundle_bytes(), 1024});
    CHECK(transaction.begin(1, app_id, static_cast<std::uint32_t>(package.size()),
          esp_rom_crc32_le(0, package.data(), package.size()), false, *store).accepted());
    for (std::size_t offset = 0; offset < package.size(); offset += 1024) {
        CHECK(transaction.append(1, static_cast<std::uint32_t>(offset), package.data() + offset,
              std::min<std::size_t>(1024, package.size() - offset), *store).accepted());
    }
    CHECK(transaction.commit(1, *store).accepted());
    const auto before_reboot = crypto_calls;
    store.reset();
    store = boot();
    CHECK(crypto_calls > before_reboot);
    assert_version(*store, 1);

    ScriptTaskAcceptedAppFrame retained;
    std::weak_ptr<const RuntimeFontCatalog> fonts;
    std::weak_ptr<const RuntimeImageProvider> images;
    FrameBuffer original;
    {
        AppRuntimeHost host({4, 2, 4, 4096, 1});
        AppInstalledBundleBinding binding(*store);
        CHECK(binding.launch(host, app_id).launched());
        DeviceBundleInspectionWorkspace workspace;
        const auto pipeline = binding.select_active_text_pipeline(workspace, 16384);
        CHECK(pipeline.status == AppInstalledTextPipelineStatus::Scalar);
        CHECK(installed_text_pipeline_frame_version(pipeline) == 13);

        const auto html = read_resource(binding, "/index.html");
        const auto css = read_resource(binding, "/app.css");
        const auto js = read_resource(binding, "/app.js");
        const auto font = std::make_shared<const std::vector<std::uint8_t>>(read(argv[2]));
        const CoreFontResourceView system{font->data(), font->size()};
        InstalledResourceSnapshot snapshot;
        snapshot.entries.push_back({"/app.css", HostResourceKind::Stylesheet, css});
        snapshot.entries.push_back({"/app.js", HostResourceKind::ClassicScript, js});
        snapshot.entries.push_back({"/mark.bmp", HostResourceKind::Image, read_resource(binding, "/mark.bmp")});
        InstalledAppResources::Handle resources;
        CHECK(create_installed_app_resources(std::move(snapshot), {}, "/index.html", &system, 1,
                                            font, esp_timer_get_time, resources));
        CHECK(resources->text_catalog && resources->image_provider);
        fonts = resources->text_catalog;
        images = resources->image_provider;

        ScriptTaskSupervisorOptions supervisor_options;
        supervisor_options.worker_inbox = {8, 256};
        supervisor_options.frame_mailbox = {4, 256};
        supervisor_options.frame_leases = {4, 65536, 131072};
        ScriptTaskSupervisor supervisor(supervisor_options);
        const auto session = begin_installed_script_session(supervisor, host.current().id, resources);
        CHECK(session.valid());
        ScriptTaskWorkerRuntimeOptions options;
        resources->bind_worker_resources(options);
        options.viewport = {0, 0, 172, 320};
        options.frame_codec = {256, 12288, 48, 65536};
        options.frame_codec.version = installed_text_pipeline_frame_version(pipeline);
        options.frame_codec.max_clips = 64;
        options.frame_codec.max_groups = 64;
        options.frame_codec.max_group_depth = 8;
        options.input_codec = {32, 256};
        {
            ScriptTaskWorkerRuntime worker(session, options);
            CHECK(worker.initialize(supervisor, std::string_view(reinterpret_cast<const char*>(html.data()), html.size()),
                                   std::string_view(reinterpret_cast<const char*>(css.data()), css.size())) ==
                  ScriptTaskWorkerRuntimeInitStatus::Accepted);
            CHECK(worker.eval_with_supervisor(supervisor,
                  std::string_view(reinterpret_cast<const char*>(js.data()), js.size()), "/app.js").ok);
            CHECK(worker.publish_frame(supervisor).accepted());
            CHECK(take_installed_core_text_frame(supervisor, session, options.frame_codec, retained) ==
                  ScriptTaskAppFrameTakeStatus::Accepted);
            CHECK(retained.text_catalog() == resources->text_catalog);
            CHECK(retained.image_provider() == resources->image_provider);
            CHECK(retained.frame().image_provider_id != 0);
            CHECK(retained.frame().shaped_text.empty());
            CHECK(!retained.frame().groups.empty());
            CHECK(std::any_of(retained.frame().display_list.begin(), retained.frame().display_list.end(),
                  [](const auto& command) {
                      PreparedTextView packet;
                      return command.type == DisplayCommandType::GlyphRun && decode_prepared_text(command.text, packet) &&
                             packet.packet_version() == 3;
                  }));
            original = paint(retained);
            CHECK(same_color(original.pixels[32 * 172 + 8], mark));
            CHECK(same_color(original.pixels[60 * 172 + 72], {0x22, 0x55, 0xbb, 255}));
            CHECK(std::count_if(original.pixels.begin(), original.pixels.begin() + 24 * 172,
                  [](Color c) { return c.r < 100 && c.g < 100 && c.b < 100; }) > 10);

            std::uint32_t seq = 0;
            for (auto kind : {ScriptTaskInputKind::PointerDown, ScriptTaskInputKind::PointerUp}) {
                CHECK(post_script_task_input(supervisor, session, ++seq,
                      {kind, 8, 53, 0, 0, 0, kind == ScriptTaskInputKind::PointerDown ? 1 : 0, 0, 0, {}},
                      options.input_codec).accepted());
                const auto step = worker.process_one(supervisor);
                CHECK(step.input_accepted && !step.fatal);
                if (kind == ScriptTaskInputKind::PointerUp) CHECK(step.frame_published);
            }
            ScriptTaskAcceptedAppFrame clicked;
            CHECK(take_installed_core_text_frame(supervisor, session, options.frame_codec, clicked) ==
                  ScriptTaskAppFrameTakeStatus::Accepted);
            const auto updated = paint(clicked);
            CHECK(same_color(updated.pixels[32 * 172 + 8], mark));
            CHECK(same_color(updated.pixels[60 * 172 + 72], {0x12, 0x77, 0x44, 255}));
        }
        CHECK(supervisor.begin_teardown(session).session == session);
        CHECK(supervisor.complete_teardown(session).session == session);
        binding.terminate_current(host, AppTeardownReason::NormalExit);
    }
    store.reset();
    std::fill(flash.begin(), flash.end(), 0xff);
    CHECK(!fonts.expired() && !images.expired());
    const auto replayed = paint(retained);
    CHECK(replayed.pixels.size() == original.pixels.size());
    CHECK(std::equal(replayed.pixels.begin(), replayed.pixels.end(), original.pixels.begin(), same_color));
    retained = {};
    CHECK(fonts.expired() && images.expired());
    std::printf("format0 frame13 PASS bytes=%zu checks=%u crypto_calls=%u\n", package.size(), checks, crypto_calls);
}

namespace jellyframe_esp32s3 {
const ResourceBundle& generated_resource_bundle() {
    static const ResourceBundle empty{};
    return empty;
}
}  // namespace jellyframe_esp32s3
