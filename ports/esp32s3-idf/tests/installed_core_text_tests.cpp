#include "jellyframe_esp32s3_installed_frame_admission.h"
#include "jellyframe_esp32s3_resources.h"
#include "app_runtime/tests/runtime_font_test_fixture.h"
#include "script/script_task_worker_runtime.h"
#include <cassert>
#include <algorithm>

#if JELLYFRAME_ESP32S3_INSTALLED_FRAME10_ENABLED
using namespace jellyframe;
using namespace jellyframe_esp32s3;

static std::int64_t clock_us() { return 1; }
static bool same(const FrameBuffer& a, const FrameBuffer& b) {
    if (a.pixels.size() != b.pixels.size()) return false;
    for (std::size_t i = 0; i < a.pixels.size(); ++i)
        if (a.pixels[i].r != b.pixels[i].r || a.pixels[i].g != b.pixels[i].g ||
            a.pixels[i].b != b.pixels[i].b || a.pixels[i].a != b.pixels[i].a) return false;
    return true;
}
int main() {
    assert(script_task_app_frame_codec_supports_version(10));
    auto bytes = std::make_shared<const std::vector<std::uint8_t>>(runtime_test_fonts::font_bytes());
    CoreFontResourceView system{bytes->data(), bytes->size()};
    InstalledResourceSnapshot snapshot;
    DeviceBundleFont metadata;
    const std::string path = "/fonts/face.jffont2", family = "Named Family";
    std::copy(path.begin(), path.end(), metadata.source.begin());
    std::copy(family.begin(), family.end(), metadata.family.begin());
    snapshot.fonts.push_back(metadata);
    snapshot.entries.push_back({path, HostResourceKind::Font, runtime_test_fonts::font_bytes()});
    InstalledAppResources::Handle resources;
    assert(create_installed_app_resources(std::move(snapshot), {}, "/index.html", &system, 1,
        bytes, clock_us, resources));
#if !CONFIG_JELLYFRAME_ESP32S3_ENABLE_BMP_IMAGE_ADAPTER
    assert(!resources->image_provider);
    InstalledResourceSnapshot images;
    images.entries.push_back({"/pixel.bmp", HostResourceKind::Image, {1, 2, 3}});
    auto original = resources;
    assert(!create_installed_app_resources(std::move(images), {}, "/index.html", &system, 1,
        bytes, clock_us, resources) && resources == original);
    original.reset();
#endif
    const auto weak = std::weak_ptr<const InstalledResourceSnapshot>(resources->snapshot);
    ScriptTaskSupervisorOptions supervisor_options;
    supervisor_options.frame_mailbox = {4, 4096};
    supervisor_options.frame_leases = {4, 32768, 65536};
    ScriptTaskSupervisor supervisor(supervisor_options);
    auto session = begin_installed_script_session(supervisor, 41, resources);
    assert(session.valid());
    ScriptTaskAppFrameCodecOptions codec{128, 16384, 16, 32768};
    codec.version = 10; codec.max_groups = 32; codec.max_group_depth = 8;
    ScriptTaskAcceptedAppFrame accepted;
    {
        ScriptTaskWorkerRuntimeOptions options;
        resources->bind_worker_resources(options);
        options.viewport = {0, 0, 64, 32}; options.frame_codec = codec;
        ScriptTaskWorkerRuntime worker(session, options);
        assert(worker.initialize(supervisor, "<body><div id='label'>AB</div></body>",
            "body{margin:0;color:#000;background-color:#fff}div{font-family:'Named Family';font-size:16px}") ==
            ScriptTaskWorkerRuntimeInitStatus::Accepted);
        assert(worker.publish_frame(supervisor).accepted());
        assert(take_installed_core_text_frame(supervisor, session, codec, accepted) ==
            ScriptTaskAppFrameTakeStatus::Accepted);
        assert(std::any_of(accepted.frame().display_list.begin(), accepted.frame().display_list.end(),
            [](const auto& c) { return c.type() == DisplayCommandType::PreparedText; }));
        FrameBuffer before(64, 32, {255,255,255,255});
        ScriptTaskFrameRenderer initial_renderer;
        assert(render_installed_core_text_frame(initial_renderer, accepted, before,
            {255,255,255,255}, nullptr, 0, nullptr, nullptr));
        assert(worker.eval_with_supervisor(supervisor, "document.getElementById('label').textContent='A';").ok);
        assert(worker.publish_frame(supervisor).accepted());
        assert(take_installed_core_text_frame(supervisor, session, codec, accepted) ==
            ScriptTaskAppFrameTakeStatus::Accepted);
        FrameBuffer after(64, 32, {255,255,255,255});
        assert(render_installed_core_text_frame(initial_renderer, accepted, after,
            {255,255,255,255}, nullptr, 0, nullptr, nullptr));
        assert(!same(before, after));
        worker.stop();
    }
    FrameBuffer target(64, 32, {255,255,255,255});
    ScriptTaskFrameRenderer renderer;
    ScriptTaskFrameRenderStatus status;
    const auto render = [&](const ScriptTaskFrameRenderer& r, const ScriptTaskAcceptedAppFrame& f) {
        return render_installed_core_text_frame(r, f, target, {255,255,255,255}, nullptr, 0, nullptr, &status);
    };
    assert(render(renderer, accepted));
    assert(std::any_of(target.pixels.begin(), target.pixels.end(), [](Color c) { return c.r < 200; }));
    const auto expected = target;
    resources.reset(); bytes.reset();
    supervisor.begin_teardown(session); supervisor.complete_teardown(session);
    assert(!weak.expired());
    target = FrameBuffer(64,32,{3,4,5,255});
    assert(render(renderer, accepted) && same(target, expected));
    auto bad = accepted.frame();
    for (auto& c : bad.display_list) if (c.type() == DisplayCommandType::PreparedText) {
        c.set_type(DisplayCommandType::Text); c.text = "AB"; break;
    }
    auto malformed = accepted.frame();
    for (auto& c : malformed.display_list) if (c.type() == DisplayCommandType::PreparedText) {
        c.text[0] = 'X'; break;
    }
    auto second = supervisor.begin(42, accepted.text_catalog(), accepted.image_provider());
    ScriptTaskAppFramePublisher publisher(codec);
    assert(!publisher.publish(supervisor, second, malformed).accepted() && same(target, expected));
    auto foreign = accepted.frame();
    for (auto& c : foreign.display_list) if (c.type() == DisplayCommandType::PreparedText) {
        // First canonical glyph resource ID (header 32 bytes).
        for (unsigned i = 0; i < 8; ++i) c.text[32 + i] = char(0xfe);
        break;
    }
    assert(publisher.publish(supervisor, second, foreign).accepted());
    assert(take_installed_core_text_frame(supervisor, second, codec, accepted) ==
        ScriptTaskAppFrameTakeStatus::TextCatalogRejected && same(target, expected));
    assert(publisher.publish(supervisor, second, bad).accepted());
    const auto old_sequence = accepted.sequence();
    assert(take_installed_core_text_frame(supervisor, second, codec, accepted) ==
        ScriptTaskAppFrameTakeStatus::ResourceRejected && accepted.sequence() == old_sequence);
    assert(same(target, expected));
    assert(publisher.publish(supervisor, second, bad).accepted());
    ScriptTaskAcceptedAppFrame raw;
    assert(take_script_task_app_frame(supervisor, second, codec, raw) == ScriptTaskAppFrameTakeStatus::Accepted);
    assert(!render(renderer, raw) && status == ScriptTaskFrameRenderStatus::InvalidFrame && same(target, expected));
    ScriptTaskFrameRendererOptions tiny; tiny.max_temporary_pixels = 1;
    assert(!render(ScriptTaskFrameRenderer({}, tiny), accepted) && same(target, expected));
    supervisor.begin_teardown(second); supervisor.complete_teardown(second);
    raw = {}; accepted = {};
    assert(weak.expired());
    return 0;
}
#else
int main() { return 77; }
#endif
namespace jellyframe_esp32s3 {
const ResourceBundle& generated_resource_bundle() { static const ResourceBundle empty{}; return empty; }
}

