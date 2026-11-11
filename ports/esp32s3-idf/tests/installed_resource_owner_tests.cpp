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
    snapshot.entries.push_back({"/apps/demo/images/pixel.bmp", jellyframe::HostResourceKind::Image,
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

    jellyframe::ScriptTaskSupervisor supervisor;
    const auto session = begin_installed_script_session(supervisor, 7, active);
    if (!check(session.valid() && supervisor.text_catalog(session) == old_worker.text_catalog &&
               supervisor.image_provider(session) == old_worker.image_provider,
               "supervisor must retain the same worker/UI resource identities")) return 1;

    auto first_owner = active;
    auto first_provider = active->image_provider;
    if (!check(!create_resources({0x42, 0x4d}, font_b, active) && active == first_owner,
               "corrupt image replacement must be refused without replacing the active owner")) return 1;
    auto invalid_font = std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{1, 2, 3});
    if (!check(!create_resources(one_pixel_bmp(), invalid_font, active) && active == first_owner,
               "invalid font replacement must be refused without replacing the active owner")) return 1;

    InstalledAppResources::Handle replacement;
    if (!check(create_resources(one_pixel_bmp(), font_b, replacement),
               "valid replacement should build a fresh owner")) return 1;
    if (!check(replacement->image_provider->id() != old_provider_id &&
               replacement->text_catalog->resource_id(0) != old_font_id,
               "replacement must publish fresh provider and font resource identities")) return 1;
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
