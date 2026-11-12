#include "jellyframe_esp32s3_installed_resource_owner.h"
#include "jellyframe_esp32s3_installed_frame_admission.h"

#include "app_runtime/script_task_frame_renderer.h"
#include "script/script_task_worker_runtime.h"

#include <atomic>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#if JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
#include "app_runtime/tests/runtime_font_test_fixture.h"
#include "render_core/text_backend.h"
#include "render_core/software_renderer.h"
namespace {

using namespace jellyframe_esp32s3;

std::int64_t host_clock_us() {
    static std::atomic<std::int64_t> ticks{1};
    return ticks.fetch_add(10, std::memory_order_relaxed);
}

void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

std::vector<std::uint8_t> one_pixel_bmp() {
    std::vector<std::uint8_t> bytes(58, 0);
    bytes[0] = 'B';
    bytes[1] = 'M';
    write_u32(bytes, 2, static_cast<std::uint32_t>(bytes.size()));
    write_u32(bytes, 10, 54);
    write_u32(bytes, 14, 40);
    write_u32(bytes, 18, 1);
    write_u32(bytes, 22, 1);
    bytes[26] = 1;
    bytes[28] = 24;
    write_u32(bytes, 34, 4);
    bytes[56] = 0xff;
    return bytes;
}

InstalledResourceSnapshot snapshot_with_image(std::vector<std::uint8_t> image) {
    InstalledResourceSnapshot snapshot;
    std::vector<InstalledResourceReference> references;
    collect_css_background_image_references(
        ".pixel{background-image:url(../../images/pixel.bmp)}",
        "/apps/demo/styles/nested/theme.css", references);
    if (references.size() != 1 || references[0].url != "/apps/demo/images/pixel.bmp") return snapshot;
    snapshot.entries.push_back({references[0].url, jellyframe::HostResourceKind::Image,
                                std::move(image)});
    return snapshot;
}

std::shared_ptr<std::vector<std::uint8_t>> read_font_fixture() {
    std::ifstream input(JELLYFRAME_CORE_SYSTEM_FONT_FIXTURE, std::ios::binary);
    if (!input.good()) return {};
    auto bytes = std::make_shared<std::vector<std::uint8_t>>(
        (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return bytes->empty() ? std::shared_ptr<std::vector<std::uint8_t>>{} : bytes;
}

std::shared_ptr<const void> erase_owner(const std::shared_ptr<std::vector<std::uint8_t>>& bytes) {
    return bytes ? std::shared_ptr<const void>(bytes, bytes->data()) : std::shared_ptr<const void>{};
}

bool create_resources(std::vector<std::uint8_t> image,
                      const std::shared_ptr<std::vector<std::uint8_t>>& font,
                      InstalledAppResources::Handle& output) {
    const jellyframe::HostBudgets budgets{};
    return create_installed_app_resources(snapshot_with_image(std::move(image)), budgets,
        "/apps/demo/index.html", font ? font->data() : nullptr, font ? font->size() : 0,
        erase_owner(font), host_clock_us, output);
}

bool check(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << "FAIL installed_resource_owner_tests: " << message << '\n';
    return false;
}

} // namespace

int run_installed_resource_owner_tests() {
    auto font_a = read_font_fixture();
    auto font_b = read_font_fixture();
    if (!check(font_a && font_b, "could not read valid JFFONT2 fixture")) return 1;

    const auto app_snapshot = [] {
        auto snapshot = snapshot_with_image(one_pixel_bmp());
        for (unsigned weight : {400u, 700u}) {
            jellyframe::DeviceBundleFont metadata;
            const std::string path = "/fonts/face" + std::to_string(weight) + ".jffont2";
            std::copy(path.begin(), path.end(), metadata.source.begin());
            const std::string family = "Named Family";
            std::copy(family.begin(), family.end(), metadata.family.begin());
            snapshot.fonts.push_back(metadata);
            snapshot.entries.push_back({path, jellyframe::HostResourceKind::Font,
                jellyframe::runtime_test_fonts::font_bytes(weight)});
        }
        return snapshot;
    };
    InstalledAppResources::Handle named;
    const auto admit = [&](InstalledResourceSnapshot snapshot) {
        return create_installed_app_resources(std::move(snapshot), {}, "/apps/demo/index.html",
            font_a->data(), font_a->size(), erase_owner(font_a), host_clock_us, named);
    };
    if (!check(admit(app_snapshot()), "declared named-family regular/bold should register")) return 1;
    if (!check(named->text_catalog->size() == 3 && named->text_catalog->registration(0).family_id == 0 &&
        named->text_catalog->registration(1).family_id == jellyframe::normalized_font_family_hash("Named Family") &&
        named->text_catalog->registration(2).family_id == named->text_catalog->registration(1).family_id,
        "firmware fallback must precede app faces in declaration order")) return 1;
    auto prior_named = named;
    auto retained_catalog = named->text_catalog;
    const auto verify_app_text = [&](const jellyframe::RuntimeFontCatalog& catalog) {
        using namespace jellyframe;
        std::uint32_t family = 0;
        if (!catalog.text_system().resolve_family_hash(normalized_font_family_hash("Named Family"), family) || !family)
            return false;
        TextFontRequest request;
        request.families = &family;
        request.family_count = 1;
        for (unsigned weight : {400u, 700u}) {
            request.weight = static_cast<std::uint16_t>(weight);
            TextRunMetrics metrics;
            PreparedTextGlyph glyph;
            if (catalog.text_system().prepare("A", request, &glyph, 1, metrics) != TextSystemStatus::Ok ||
                glyph.resource_id != catalog.resource_id(weight == 400 ? 1 : 2) ||
                metrics.advance != 9600 || metrics.ascent != 12800) return false;
            std::string packet;
            PreparedTextView view;
            if (!encode_prepared_text(&glyph, 1, request.size_q1000, 1, packet) ||
                !decode_prepared_text(packet, view)) return false;
            FrameBuffer target(16, 16, Color{0, 0, 0, 255});
            if (catalog.text_system().paint(view, target, {0, 0, 16, 16}, 4000, 8000,
                                          Color{255, 255, 255, 255}) != TextSystemStatus::Ok) return false;
            bool painted = false;
            for (const auto& pixel : target.pixels) painted = painted || pixel.r != 0;
            if (!painted) return false;
        }
        return true;
    };
    if (!check(verify_app_text(*retained_catalog), "named family must resolve, measure, select weights and paint")) return 1;
    std::weak_ptr<const InstalledResourceSnapshot> named_snapshot = named->snapshot;
    for (unsigned fault = 0; fault < 7; ++fault) {
        auto invalid = app_snapshot();
        if (fault == 0) invalid.entries.back().bytes = {1,2,3};
        if (fault == 1) invalid.entries.pop_back();
        if (fault == 2) invalid.entries.back().kind = jellyframe::HostResourceKind::Image;
        if (fault == 3) invalid.fonts.resize(jellyframe::kDeviceBundleMaxFonts + 1);
        if (fault == 4) invalid.fonts.back().source.fill('x');
        if (fault == 5) invalid.entries.back().bytes.resize(kInstalledResourceMaxBytes + 1);
        if (fault == 6) invalid.fonts.back().family[0] = '\n';
        if (!check(!admit(std::move(invalid)) && named == prior_named,
                   "invalid app metadata/bytes must preserve previous owner")) return 1;
    }
    if (!check(admit(app_snapshot()) && named != prior_named,
               "valid replacement should publish a fresh named catalog")) return 1;
    prior_named.reset();
    if (!check(!named_snapshot.expired(), "catalog alone must pin old app font bytes")) return 1;
    if (!check(verify_app_text(*retained_catalog), "old catalog must still paint after refused and valid replacements")) return 1;
    retained_catalog.reset();
    if (!check(named_snapshot.expired(), "old font snapshot releases after final catalog drains")) return 1;
    named.reset();
    auto aliased = app_snapshot();
    auto alias = aliased.fonts.front();
    alias.family.fill('\0');
    const std::string alias_name = "Family Alias";
    std::copy(alias_name.begin(), alias_name.end(), alias.family.begin());
    aliased.fonts.push_back(alias);
    if (!check(admit(std::move(aliased)), "same source may be declared under another family")) return 1;
    std::uint32_t alias_family = 0;
    jellyframe::TextFontRequest alias_request;
    jellyframe::TextRunMetrics alias_metrics;
    alias_request.families = &alias_family;
    alias_request.family_count = 1;
    if (!check(named->text_catalog->text_system().resolve_family_hash(
                   jellyframe::normalized_font_family_hash(alias_name), alias_family) &&
               named->text_catalog->text_system().measure("A", alias_request, alias_metrics) ==
                   jellyframe::TextSystemStatus::Ok &&
               alias_metrics.primary_resource_id == named->text_catalog->resource_id(3),
               "source alias must resolve and measure through its own registration")) return 1;
    named.reset();

    InstalledAppResources::Handle active;
    if (!check(create_resources(one_pixel_bmp(), font_a, active),
               "valid snapshot and Core font should build an owner")) return 1;
    std::weak_ptr<const InstalledResourceSnapshot> old_snapshot = active->snapshot;
    std::weak_ptr<std::vector<std::uint8_t>> old_font = font_a;
    const std::uint64_t old_provider_id = active->image_provider->id();
    const std::uint64_t old_font_id = active->text_catalog->resource_id(0);
    jellyframe::ResolvedImageResource resolved;
    if (!check(active->image_provider->resolve("/apps/demo/images/pixel.bmp", resolved) &&
               active->image_provider->contains(resolved.image_handle),
               "snapshot-backed provider should resolve its admitted BMP")) return 1;
    if (!check(!active->image_provider->resolve("https://example.test/pixel.bmp", resolved) &&
               resolved.image_handle == 0,
               "provider should reject a non-local URL and clear output")) return 1;

    jellyframe::ScriptTaskWorkerRuntimeOptions old_worker;
    jellyframe::ScriptTaskFrameRendererOptions old_ui;
    active->bind_worker_resources(old_worker);
    active->bind_ui_resources(old_ui);
    if (!check(old_worker.image_provider == old_ui.image_provider &&
               old_worker.text_catalog == old_ui.text_catalog &&
               old_worker.image_provider->id() == old_provider_id &&
               old_worker.text_catalog->resource_id(0) == old_font_id,
               "worker and UI must receive the same provider and font catalog identities")) return 1;

    jellyframe::ScriptTaskSupervisorOptions supervisor_options;
    supervisor_options.frame_mailbox = {4, 4096};
    supervisor_options.frame_leases = {4, 32 * 1024, 64 * 1024};
    jellyframe::ScriptTaskSupervisor supervisor(supervisor_options);
    const auto session = begin_installed_script_session(supervisor, 7, active);
    if (!check(session.valid() && supervisor.text_catalog(session) == old_worker.text_catalog &&
                   supervisor.image_provider(session) == old_worker.image_provider,
               "supervisor must retain the same worker/UI resource identities"))
        return 1;

    for (bool inline_attribute : {false, true}) {
        auto options = old_worker;
        options.viewport = {0, 0, 16, 16};
        options.frame_codec = {128, 16 * 1024, 16, 32 * 1024};
        options.frame_codec.version = 10;
        options.frame_codec.max_groups = 32;
        options.script = jellyframe::script_runtime_options_from_host_budgets(options.budgets);
        unsigned preparation_calls = 0;
        options.prepare_document_user = &preparation_calls;
        options.prepare_document = [](jellyframe::Node& node, void* context) {
            ++*static_cast<unsigned*>(context);
            return prepare_document_styles(node, "/apps/demo/pages/index.html");
        };
        jellyframe::ScriptTaskWorkerRuntime worker(session, options);
        std::string prepared;
        if (!check(prepare_stylesheet_resources(
                       "body{margin:0;display:block}div{display:block;width:16px;height:16px;"
                       "background-size:cover;background-image:url(../../images/pixel.bmp)}",
                       "/apps/demo/styles/nested/theme.css", prepared),
                   "linked CSS prepares"))
            return 1;
        const char* html =
            inline_attribute
                ? "<body><div style='background-image:url(../images/pixel.bmp)'></div></body>"
                : "<body><div></div></body>";
        if (inline_attribute)
            prepared = "body{margin:0;display:block}div{display:block;width:16px;height:16px;"
                       "background-size:cover}";
        if (!check(worker.initialize(supervisor, html, prepared) ==
                       jellyframe::ScriptTaskWorkerRuntimeInitStatus::Accepted,
                   "worker prepares original inline attribute and stylesheet CSS"))
            return 1;
        if (!check(preparation_calls == 1, "worker prepares the original DOM once"))
            return 1;
        if (!check(worker.publish_frame(supervisor).accepted(),
                   "derived background frame publishes"))
            return 1;
        jellyframe::ScriptTaskAcceptedAppFrame accepted;
        if (!check(jellyframe::take_script_task_app_frame(supervisor, session, options.frame_codec,
                                                          accepted) ==
                       jellyframe::ScriptTaskAppFrameTakeStatus::Accepted,
                   "derived background frame accepted"))
            return 1;
        worker.stop();
        jellyframe::ScriptTaskFrameRenderer renderer({}, {}, old_ui);
        jellyframe::ScriptTaskFrameRenderStatus status;
        auto pixels = renderer.render(accepted.frame(), {255, 255, 255, 255}, &status);
        if (!check(status == jellyframe::ScriptTaskFrameRenderStatus::Accepted &&
                       pixels.width > 0 && pixels.height > 0 && pixels.pixels[0].r == 255 &&
                       pixels.pixels[0].g == 0 && pixels.pixels[0].b == 0,
                   "stylesheet and original inline background paint retained BMP pixels"))
            return 1;
    }

    {
        auto options = old_worker;
        options.viewport = {0, 0, 16, 16};
        options.frame_codec.version = 10;
        options.prepare_document = [](jellyframe::Node&, void*) { return false; };
        jellyframe::ScriptTaskWorkerRuntime worker(session, options);
        if (!check(worker.initialize(supervisor, "<body></body>", "") ==
                           jellyframe::ScriptTaskWorkerRuntimeInitStatus::ParseRejected &&
                       !worker.initialized() && !worker.publish_frame(supervisor).accepted(),
                   "preparation refusal publishes no document or frame"))
            return 1;
    }

    auto first_owner = active;
    auto first_provider = active->image_provider;
    if (!check(!create_resources({0x42, 0x4d}, font_b, active) && active == first_owner,
               "corrupt image replacement must be refused without replacing the active owner"))
        return 1;
    auto invalid_font =
        std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{1, 2, 3});
    if (!check(!create_resources(one_pixel_bmp(), invalid_font, active) && active == first_owner,
               "invalid font replacement must be refused without replacing the active owner"))
        return 1;

    InstalledAppResources::Handle replacement;
    if (!check(create_resources(one_pixel_bmp(), font_b, replacement),
               "valid replacement should build a fresh owner"))
        return 1;
    if (!check(replacement->image_provider->id() != old_provider_id &&
                   replacement->text_catalog->resource_id(0) != old_font_id,
               "replacement must publish fresh provider and font resource identities"))
        return 1;
    const auto replacement_snapshot = std::weak_ptr<const InstalledResourceSnapshot>(replacement->snapshot);
    active = replacement;
    jellyframe::ScriptTaskWorkerRuntimeOptions new_worker;
    jellyframe::ScriptTaskFrameRendererOptions new_ui;
    active->bind_worker_resources(new_worker);
    active->bind_ui_resources(new_ui);
    if (!check(new_worker.image_provider == new_ui.image_provider &&
               new_worker.text_catalog == new_ui.text_catalog &&
               new_worker.image_provider->id() == replacement->image_provider->id() &&
               new_worker.image_provider->id() != first_provider->id(),
               "replacement worker and UI must share the replacement owner's handles")) return 1;

    active.reset();
    replacement.reset();
    if (!check(old_snapshot.expired() == false,
               "old snapshot must remain pinned while worker/UI handles retain its provider")) return 1;
    first_provider.reset();
    first_owner.reset();
    font_a.reset();
    old_worker = {};
    old_ui = {};
    if (!check(!old_snapshot.expired() && !old_font.expired(),
               "supervisor must retain resources after worker/UI handles drain")) return 1;
    supervisor.begin_teardown(session);
    supervisor.complete_teardown(session);
    if (!check(old_snapshot.expired() && old_font.expired(),
               "old snapshot and font owner must release after the last worker/UI handle drains")) return 1;

    new_worker = {};
    new_ui = {};
    if (!check(replacement_snapshot.expired(),
               "replacement snapshot must release after teardown drains its handles")) return 1;
    return 0;
}
#else
int run_installed_resource_owner_tests() {
    std::cout << "SKIP installed resource owner requires canonical Core API v1\n";
    return 77;
}
#endif

namespace jellyframe_esp32s3 {
const ResourceBundle& generated_resource_bundle() {
    static const ResourceBundle empty{};
    return empty;
}
} // namespace jellyframe_esp32s3

int main() {
    return run_installed_resource_owner_tests();
}
