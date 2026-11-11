#include "jellyframe_esp32s3_resources.h"

#include <iostream>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace jellyframe_esp32s3 {

const ResourceBundle& generated_resource_bundle() {
    static const ResourceBundle empty{};
    return empty;
}

} // namespace jellyframe_esp32s3

int main() {
    std::ifstream fixture(JELLYFRAME_RESOURCE_REFERENCE_FIXTURE, std::ios::binary);
    if (!fixture.good()) {
        std::cerr << "FAIL resource_reference_tests: cannot open nested stylesheet fixture: "
                  << JELLYFRAME_RESOURCE_REFERENCE_FIXTURE << '\n';
        return 1;
    }
    const std::string css((std::istreambuf_iterator<char>(fixture)), std::istreambuf_iterator<char>());

    std::vector<jellyframe_esp32s3::InstalledResourceReference> references;
    jellyframe_esp32s3::collect_css_background_image_references(
        css, "/apps/demo/styles/nested/theme.css", references);
    if (references.size() != 1 || references.front().kind != jellyframe::HostResourceKind::Image ||
        references.front().url != "/apps/demo/images/background.bmp" ||
        references.front().base_url != "/apps/demo/styles/nested/theme.css") {
        std::cerr << "FAIL resource_reference_tests: Core CSS catalog should retain the package URL "
                     "and stylesheet base URL\n";
        return 1;
    }

    std::string resolved;
    if (!jellyframe_esp32s3::resolve_local_resource_url(
            references.front().url, references.front().base_url, resolved) ||
        resolved != "/apps/demo/images/background.bmp") {
        std::cerr << "FAIL resource_reference_tests: package-absolute CSS URL resolution changed\n";
        return 1;
    }

    std::vector<jellyframe_esp32s3::InstalledResourceReference> unsupported_relative;
    jellyframe_esp32s3::collect_css_background_image_references(
        ".hero { background-image: url(\"../images/background.bmp\"); }",
        "/apps/demo/styles/nested/theme.css", unsupported_relative);
    if (!unsupported_relative.empty()) {
        std::cerr << "FAIL resource_reference_tests: unsupported relative CSS URL entered snapshot list\n";
        return 1;
    }

    const std::uint8_t bmp_fixture[] = {0x42, 0x4d};
    const jellyframe_esp32s3::ResourceEntry bmp_entry{
        "/apps/demo/images/background.bmp", jellyframe::HostResourceKind::Image,
        bmp_fixture, sizeof(bmp_fixture)};
    const jellyframe_esp32s3::ResourceBundle bundle{&bmp_entry, 1};
    const jellyframe::HostBudgets budgets{};
    auto context = jellyframe_esp32s3::make_resource_context(
        budgets, "/apps/demo/index.html", bundle);
    std::string loaded;
    if (!jellyframe_esp32s3::load_resource(
            jellyframe::HostResourceRequest{jellyframe::HostResourceKind::Image,
                                            references.front().url, references.front().base_url},
            loaded, &context) || loaded.size() != sizeof(bmp_fixture)) {
        std::cerr << "FAIL resource_reference_tests: snapshot resource bundle could not provide BMP bytes\n";
        return 1;
    }

    const std::vector<jellyframe_esp32s3::InstalledResourceSnapshotEntry> loaded_entries{
        {"/apps/demo/images/background.bmp", jellyframe::HostResourceKind::Image, {0x42, 0x4d}}};
    bool already_loaded = false;
    if (!jellyframe_esp32s3::snapshot_resource_entry_compatible(
            loaded_entries, "/apps/demo/images/background.bmp",
            jellyframe::HostResourceKind::Image, already_loaded) || !already_loaded) {
        std::cerr << "FAIL resource_reference_tests: matching snapshot resource was not reusable\n";
        return 1;
    }
    if (jellyframe_esp32s3::snapshot_resource_entry_compatible(
            loaded_entries, "/apps/demo/images/background.bmp",
            jellyframe::HostResourceKind::Stylesheet, already_loaded)) {
        std::cerr << "FAIL resource_reference_tests: same URL with a conflicting kind was accepted\n";
        return 1;
    }

    // Inline HTML styles continue to resolve from the entry document.
    if (!jellyframe_esp32s3::resolve_local_resource_url(
            "images/background.bmp", "/apps/demo/index.html", resolved) ||
        resolved != "/apps/demo/images/background.bmp") {
        std::cerr << "FAIL resource_reference_tests: inline image did not resolve from entry document\n";
        return 1;
    }
    return 0;
}
