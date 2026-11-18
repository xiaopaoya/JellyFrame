#include "native_contextual.h"
#include "render_core/html_parser.h"
#include "render_core/css_parser.h"
#include "render_core/input.h"
#include "native_image.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <process.h>

#if JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API && JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API && defined(JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT)
#include "runtime_shaped_text_test_fixture.h"
using namespace jellyframe;
using namespace jellyframe_example;

static void write(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    assert(file.good());
}

static std::string hex(const RuntimeSha256& value) {
    std::string result;
    for (auto byte : value) {
        result += "0123456789abcdef"[byte >> 4];
        result += "0123456789abcdef"[byte & 15];
    }
    return result;
}

template<class Operation> static void refuses(Operation operation) {
    bool refused = false;
    try { operation(); } catch (const std::runtime_error&) { refused = true; }
    assert(refused);
}

static int shell(const char* executable, const std::filesystem::path& package,
                 const std::filesystem::path& output) {
    const auto app = package.string(), capture = output.string();
    const char* arguments[] = {executable, "--app", app.c_str(), "--capture", capture.c_str(),
        "--viewport-width", "120", "--viewport-height", "100", nullptr};
    return static_cast<int>(_spawnv(_P_WAIT, executable, arguments));
}

#if defined(_WIN32)
static int pseudo(const char* executable, const std::filesystem::path& package,
                  const std::filesystem::path& output, const std::filesystem::path& report) {
    const auto root = package.string(), html = (package / "index.html").string();
    const auto css = (package / "empty.css").string(), capture = output.string(), json = report.string();
    const char* arguments[] = {executable, html.c_str(), css.c_str(), capture.c_str(), "120", "100",
        "--resource-root", root.c_str(), "--package", "--diagnostics-json", json.c_str(), nullptr};
    return static_cast<int>(_spawnv(_P_WAIT, executable, arguments));
}
#endif

static int shell_frames(const char* executable, const std::filesystem::path& package,
                        const std::filesystem::path& script) {
    const auto app = package.string(), frames = script.string();
    const char* arguments[] = {executable, "--app", app.c_str(), "--frame-script", frames.c_str(), nullptr};
    return static_cast<int>(_spawnv(_P_WAIT, executable, arguments));
}

static std::vector<std::uint8_t> make_bundle(const std::filesystem::path& root,
    const std::string& pipeline, const char* script_mode = "classic") {
    std::string summary = std::string(
        R"({"id":"org.example.contextual-desktop","name":"Desktop","role":"app","versionName":"1","versionCode":1,"entry":"/index.html","minJellyFrame":")") +
        std::string(kActiveJellyFrameRuntimeVersion) + R"(","minRenderCore":")" +
        std::string(kActiveRenderCoreVersion) + R"(","script":"classic","permissions":[],"capabilities":[],"fonts":[],"viewport":{},"budgets":{},"targets":{},"backgroundServices":{},"computeJobsAllowed":false,"videoFrameAllowed":false,"networkAllowed":false,"storageKvAllowed":false,"canvas2dAllowed":false,"audioPlaybackAllowed":false,"sensorAccelerometerAllowed":false,"sensorGyroscopeAllowed":false,"sensorHeartRateAllowed":false,"sensorAmbientLightAllowed":false,"locationPositionAllowed":false,"systemBatteryAllowed":false,"systemWeatherAllowed":false,"systemActivityAllowed":false,"textPipeline":)" + pipeline + "}";
    const auto script_at = summary.find("\"script\":\"classic\"");
    summary.replace(script_at, 18, std::string("\"script\":\"") + script_mode + "\"");
    std::vector<std::pair<std::string, std::string>> resources;
    for (const auto* path : {"/catalog.jfcat", "/companion.jfshr", "/eligibility.bin",
            "/font.jffont2", "/index.html", "/tables.bin"}) {
        resources.emplace_back(path, read_text_file_limited(root / (path + 1), 16U * 1024U * 1024U));
        assert(!resources.back().second.empty());
    }
    const auto index_offset = kJfappHeaderSize + summary.size();
    const auto strings_offset = index_offset + resources.size() * kJfappResourceEntrySize;
    std::string strings, payload;
    for (const auto& resource : resources) { strings += resource.first; payload += resource.second; }
    const auto payload_offset = strings_offset + strings.size();
    std::vector<std::uint8_t> bytes(payload_offset + payload.size());
    std::memcpy(bytes.data(), "JFAPPV0\0", 8);
    runtime_test_shaped_text::put16(bytes, 8, kJfappHeaderSize);
    runtime_test_shaped_text::put16(bytes, 10, 1);
    runtime_test_shaped_text::put32(bytes, 16, kJfappHeaderSize);
    runtime_test_shaped_text::put32(bytes, 20, static_cast<std::uint32_t>(summary.size()));
    runtime_test_shaped_text::put32(bytes, 24, static_cast<std::uint32_t>(index_offset));
    runtime_test_shaped_text::put32(bytes, 28, static_cast<std::uint32_t>(resources.size()));
    runtime_test_shaped_text::put32(bytes, 32, static_cast<std::uint32_t>(strings_offset));
    runtime_test_shaped_text::put32(bytes, 36, static_cast<std::uint32_t>(strings.size()));
    runtime_test_shaped_text::put32(bytes, 40, static_cast<std::uint32_t>(payload_offset));
    runtime_test_shaped_text::put32(bytes, 44, static_cast<std::uint32_t>(payload.size()));
    std::memcpy(bytes.data() + kJfappHeaderSize, summary.data(), summary.size());
    std::memcpy(bytes.data() + strings_offset, strings.data(), strings.size());
    std::memcpy(bytes.data() + payload_offset, payload.data(), payload.size());
    std::size_t string_position = 0, payload_position = 0;
    for (std::size_t index = 0; index < resources.size(); ++index) {
        const auto& resource = resources[index];
        const auto at = index_offset + index * kJfappResourceEntrySize;
        runtime_test_shaped_text::put32(bytes, at, fnv1a_32(resource.first));
        runtime_test_shaped_text::put32(bytes, at + 4, static_cast<std::uint32_t>(string_position));
        runtime_test_shaped_text::put16(bytes, at + 8, static_cast<unsigned>(resource.first.size()));
        runtime_test_shaped_text::put32(bytes, at + 12, static_cast<std::uint32_t>(payload_position));
        runtime_test_shaped_text::put32(bytes, at + 16, static_cast<std::uint32_t>(resource.second.size()));
        runtime_test_shaped_text::put32(bytes, at + 20, crc32_bytes(
            reinterpret_cast<const std::uint8_t*>(resource.second.data()), resource.second.size()));
        string_position += resource.first.size(); payload_position += resource.second.size();
    }
    runtime_test_shaped_text::put32(bytes, 48, crc32_bytes(bytes.data(), bytes.size()));
    return bytes;
}

int main(int argc, char** argv) {
    assert(argc >= 2);
    const std::filesystem::path root(argv[1]);
    std::filesystem::create_directories(root);
    runtime_test_shaped_text::Assets assets("regular", 555);
    write(root / "font.jffont2", assets.font);
    write(root / "catalog.jfcat", assets.catalog);
    write(root / "companion.jfshr", assets.companion);
    write(root / "tables.bin", assets.tables);
    write(root / "eligibility.bin", assets.eligibility);
    const auto pipeline = std::string(
        R"({"kind":"jfsp2-block-v12","font":"/font.jffont2","catalog":"/catalog.jfcat","companion":"/companion.jfshr","tables":"/tables.bin","eligibility":"/eligibility.bin","sourceIdentity":")") +
        hex(assets.manifest.source_identity) + R"(","profileIdentity":")" +
        hex(assets.manifest.profile_identity) +
        R"(","glyphCount":30900,"resourceId":555,"rasterSize":16,"weight":400,"selection":0})";
    const std::string manifest = std::string(R"({"id":"org.example.contextual-desktop","entry":"/index.html","runtime":{"minJellyFrame":")") +
        std::string(kActiveJellyFrameRuntimeVersion) + R"(","minRenderCore":")" +
        std::string(kActiveRenderCoreVersion) + R"(","script":"classic"},"textPipeline":)" + pipeline + "}";
    write(root / "jellyframe.app.json", manifest);
    const std::string html = "<body><div id='outer'><p id='text'>AV A A A A A</p></div></body>";
    const std::string css = "*{margin:0;padding:0;border:0}body{background:#ffffff}"
        "div{display:block;width:75px;margin-left:5px;background:#aaccee}"
        "p{display:block;font-size:16.5px;line-height:20px;color:#123456}";
    write(root / "index.html", "<style>" + css + "</style>" + html);
    refuses([&] { (void)load_app_package(root, 65536); });
    auto package = load_app_package(root, 65536, true);
    PackageResourceContext resources;
    resources.root = root;
    auto selected = native::load_desktop_contextual_text(package.manifest, resources);
    assert(selected.binding && selected.binding->valid());
    auto bad_script_manifest = package.manifest;
    bad_script_manifest.script_mode = "none";
    refuses([&] { (void)native::load_desktop_contextual_text(bad_script_manifest, resources); });
    assert(native::desktop_resource_digest(assets.font) == assets.manifest.font_resource_identity);
    auto document = HtmlParser{}.parse(html);
    StyleResolver resolver(CssParser{}.parse(css));
    auto frame = native::build_desktop_contextual_frame(*document, resolver, selected, 120, 100);
    assert(frame.capture.valid() && frame.capture.layer_tree()->box == nullptr);
    assert(frame.layout.paragraphs().size() == 1);
    assert(frame.layout.paragraphs()[0].paragraph.lines().size() > 1);
    auto pixels = native::paint_desktop_contextual_frame(frame, 120, 100, {255,255,255,255}, 12000);
    assert(pixels.width == 120 && pixels.height == 100);
    bool ink = false;
    for (const auto& color : pixels.pixels) if (color.r == 0x12 && color.g == 0x34 && color.b == 0x56) ink = true;
    assert(ink);

    // A real native click traverses the separate retained geometry owner.
    int clicks = 0;
    document->add_event_listener("click", [&](Event&) { ++clicks; });
    {
        InputController input(*frame.input_layer, {}, false);
        input.pointer_down({8, 8, PointerButton::Primary, 1});
        input.pointer_up({8, 8, PointerButton::Primary, 0});
    }
    assert(clicks == 1);
    const auto before = pixels.pixels;
    refuses([&] { pixels = native::paint_desktop_contextual_frame(frame, 120, 100,
        {255,255,255,255}, 12000, 0); });
    assert(std::memcmp(before.data(), pixels.pixels.data(), before.size() * sizeof(Color)) == 0);
    // Background replay begins before this packet identity failure. Assigning
    // only the fully returned staging frame preserves the published pixels.
    auto original_binding = frame.binding;
    const std::weak_ptr<const RuntimeShapedTextBinding> retained_owner = frame.binding;
    runtime_test_shaped_text::Assets replacement("regular", 556);
    frame.binding = runtime_test_shaped_text::create_binding(replacement);
    refuses([&] { pixels = native::paint_desktop_contextual_frame(frame, 120, 100,
        {255,255,255,255}, 12000); });
    assert(std::memcmp(before.data(), pixels.pixels.data(), before.size() * sizeof(Color)) == 0);
    frame.binding = original_binding;
    refuses([&] { (void)native::paint_desktop_contextual_frame(frame, 120, 100,
        {255,255,255,255}, 10); });
    auto invalid_document = HtmlParser{}.parse(html);
    StyleResolver invalid_resolver(CssParser{}.parse(css + "div{width:75.5px}"));
    refuses([&] { (void)native::build_desktop_contextual_frame(*invalid_document,
        invalid_resolver, selected, 120, 100); });
    auto invalid_selected = selected;
    invalid_selected.layout_options.font_family_hash = 123;
    refuses([&] { (void)native::build_desktop_contextual_frame(*document,
        resolver, invalid_selected, 120, 100); });
    if (argc > 2) {
        assert(shell(argv[2], root, root / "desktop.bmp") == 0);
        assert(std::filesystem::file_size(root / "desktop.bmp") > 12000);
        ImageDecodeFixture capture;
        assert(native::decode_bmp_to_fixture("capture", read_text_file_limited(root / "desktop.bmp", 65536),
            capture, nullptr));
        assert(capture.pixels.size() == before.size() * sizeof(Color));
        assert(std::memcmp(before.data(), capture.pixels.data(), before.size() * sizeof(Color)) == 0);
        if (argc > 3) {
            const auto output = root / "pseudo.bmp", report = root / "pseudo.json";
            write(root / "empty.css", "");
            assert(pseudo(argv[3], root, output, report) == 0);
            assert(native::decode_bmp_to_fixture("pseudo", read_text_file_limited(output, 65536),
                capture, nullptr));
            assert(capture.pixels.size() == before.size() * sizeof(Color));
            assert(std::memcmp(before.data(), capture.pixels.data(), capture.pixels.size()) == 0);
            assert(read_text_file_limited(report, 65536).find("\"core-contextual-v12-local\"") != std::string::npos);

            const auto refused_output = root / "pseudo-refused.bmp";
            const auto refused_report = root / "pseudo-refused.json";
            std::filesystem::remove(refused_output);
            std::filesystem::remove(refused_report);
            write(root / "font.jffont2", "invalid font");
            const auto refused = pseudo(argv[3], root, refused_output, refused_report);
            write(root / "font.jffont2", assets.font);
            assert(refused == 1 && !std::filesystem::exists(refused_output) &&
                !std::filesystem::exists(refused_report));
        }
        const auto bundle = make_bundle(root, pipeline);
        const auto bundle_path = root / "contextual.jfapp";
        write(bundle_path, {reinterpret_cast<const char*>(bundle.data()), bundle.size()});
        assert(bundle.size() > 512U * 1024U);
        assert(shell(argv[2], bundle_path, root / "bundle.bmp") == 0);
        assert(read_text_file_limited(root / "bundle.bmp", 65536) ==
               read_text_file_limited(root / "desktop.bmp", 65536));
        const auto bad_script_bundle = make_bundle(root, pipeline, "none");
        write(root / "bad-script.jfapp", {reinterpret_cast<const char*>(bad_script_bundle.data()), bad_script_bundle.size()});
        assert(shell(argv[2], root / "bad-script.jfapp", root / "bad-script.bmp") != 0);
        assert(!std::filesystem::exists(root / "bad-script.bmp"));
        auto wrong_format = bundle;
        wrong_format[10] = 0;
        std::fill_n(wrong_format.data() + 48, 4, 0);
        write(root / "wrong-format.jfapp", {reinterpret_cast<const char*>(wrong_format.data()), wrong_format.size()});
        refuses([&] { (void)load_app_package(root / "wrong-format.jfapp", 20U * 1024U * 1024U, true); });
        refuses([&] { (void)native::load_desktop_app_package(root / "wrong-format.jfapp", 512U * 1024U); });
        {
            std::ofstream oversized(root / "oversized.jfapp", std::ios::binary);
            oversized.write(reinterpret_cast<const char*>(bundle.data()), kJfappHeaderSize);
            oversized.seekp(20U * 1024U * 1024U);
            oversized.put('\0');
        }
        refuses([&] { (void)native::load_desktop_app_package(root / "oversized.jfapp", 512U * 1024U); });
        std::filesystem::remove(root / "oversized.jfapp");
#if defined(JELLYFRAME_ENABLE_SCRIPTING)
        const Rect initial_rect = frame.layout.paragraphs()[0].owner->rect;
        auto updated_document = HtmlParser{}.parse(
            "<body><div id='outer'><p id='text' style='width:42px;margin-top:7px'>A</p></div></body>");
        auto updated_frame = native::build_desktop_contextual_frame(*updated_document, resolver, selected, 120, 100);
        const Rect updated_rect = updated_frame.layout.paragraphs()[0].owner->rect;
        assert(initial_rect.x == 5 && initial_rect.width == 75 && initial_rect.height > updated_rect.height);
        assert(updated_rect.x == 5 && updated_rect.y == 7 && updated_rect.width == 42 && updated_rect.height > 0);
        const auto geometry_matches = [](const Rect& rect) {
            return "rect.x===" + std::to_string(rect.x) + "&&rect.y===" + std::to_string(rect.y) +
                "&&rect.width===" + std::to_string(rect.width) + "&&rect.height===" + std::to_string(rect.height);
        };
        // Register the snapshot read before the first completed host frame.
        // Each click must observe the preceding contextual layout before its
        // mutation can produce the independently checked packet/pixel frame.
        write(root / "index.html", "<style>" + css + "</style>" + html +
            "<script>var text=document.getElementById('text');var step=0;"
            "text.getBoundingClientRect();text.addEventListener('click',function(){"
            "var rect=text.getBoundingClientRect();if(step===0&&(" + geometry_matches(initial_rect) + ")){"
            "this.textContent='A';this.style.width='42px';this.style.marginTop='7px';step=1;"
            "}else if(step===1&&(" + geometry_matches(updated_rect) + ")){"
            "this.textContent='AV';this.style.width='30px';step=2;"
            "}else{this.textContent='FAIL';this.style.width='20px';step=99;}});</script>");
        const auto frame_dir = (root / "frames").generic_string();
        write(root / "capture.jfcapture", "output-dir " + frame_dir +
            "\nframes 4\nstep-ms 33\nviewport 120 100\nevent 1 click 8 8\nevent 2 click 8 8\n");
        assert(shell_frames(argv[2], root, root / "capture.jfcapture") == 0);
        const auto first = read_text_file_limited(root / "frames/frame_000.bmp", 65536);
        const auto changed = read_text_file_limited(root / "frames/frame_001.bmp", 65536);
        const auto checked_update = read_text_file_limited(root / "frames/frame_002.bmp", 65536);
        const auto stable = read_text_file_limited(root / "frames/frame_003.bmp", 65536);
        assert(!first.empty() && !changed.empty() && !checked_update.empty());
        assert(changed != first && checked_update != changed && stable == checked_update);
        assert(native::decode_bmp_to_fixture("first", first, capture, nullptr));
        assert(capture.pixels.size() == before.size() * sizeof(Color));
        assert(std::memcmp(before.data(), capture.pixels.data(), capture.pixels.size()) == 0);
        auto updated_pixels = native::paint_desktop_contextual_frame(updated_frame, 120, 100,
            {255,255,255,255}, 12000);
        assert(native::decode_bmp_to_fixture("changed", changed, capture, nullptr));
        assert(capture.pixels.size() == updated_pixels.pixels.size() * sizeof(Color));
        assert(std::memcmp(updated_pixels.pixels.data(), capture.pixels.data(),
            capture.pixels.size()) == 0);
        auto checked_document = HtmlParser{}.parse(
            "<body><div id='outer'><p id='text' style='width:30px;margin-top:7px'>AV</p></div></body>");
        auto checked_frame = native::build_desktop_contextual_frame(*checked_document, resolver, selected, 120, 100);
        auto checked_pixels = native::paint_desktop_contextual_frame(checked_frame, 120, 100,
            {255,255,255,255}, 12000);
        assert(native::decode_bmp_to_fixture("checked-update", checked_update, capture, nullptr));
        assert(capture.pixels.size() == checked_pixels.pixels.size() * sizeof(Color));
        assert(std::memcmp(checked_pixels.pixels.data(), capture.pixels.data(), capture.pixels.size()) == 0);
#endif
    }

    // Source package storage, original bytes and DOM can all disappear after
    // capture. Replay consumes only the frozen value-owned packet/backgrounds.
    selected = {};
    invalid_selected = {};
    original_binding.reset();
    assets.clear_source_buffers();
    frame.input_layer.reset();
    frame.render_tree.reset();
    document.reset();
    const auto retained = native::paint_desktop_contextual_frame(frame, 120, 100,
        {255,255,255,255}, 12000);
    assert(std::memcmp(before.data(), retained.pixels.data(), before.size() * sizeof(Color)) == 0);
    assert(!retained_owner.expired());
    frame = {};
    assert(retained_owner.expired());

    write(root / "catalog.jfcat", "broken");
    refuses([&] { (void)native::load_desktop_contextual_text(package.manifest, resources); });
    if (argc > 2) assert(shell(argv[2], root, root / "refused.bmp") != 0);
    assert(!std::filesystem::exists(root / "refused.bmp"));
    std::cout << "Desktop contextual package entry, nested layout, typed paint, input, refusal and retained replay passed\n";
}
#elif !JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API || !JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
int main(int argc, char** argv) {
    assert(!jellyframe::native::contextual_desktop_available);
    if (argc > 1) {
        const std::filesystem::path root(argv[1]);
        std::filesystem::create_directories(root);
        std::ofstream manifest(root / "jellyframe.app.json");
        manifest << "{\"textPipeline\":{\"kind\":\"jfsp2-block-v12\"}}";
        manifest.close();
        bool refused = false;
        try { (void)jellyframe::native::load_desktop_app_package(root, 65536); }
        catch (const std::runtime_error& error) {
            refused = std::string(error.what()).find("does not support package-selected shaped textPipeline") != std::string::npos;
        }
        assert(refused);
    }
    std::cout << "Desktop contextual route unavailable and refused\n";
}
#else
int main() {
    std::cout << "Desktop contextual tests require offline Core fixture assets\n";
    return 77;
}
#endif
