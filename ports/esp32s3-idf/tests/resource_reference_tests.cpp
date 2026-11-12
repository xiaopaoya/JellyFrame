#include "jellyframe_esp32s3_resources.h"

#include <iostream>
#include <cassert>
#include "render_core/document_style.h"
#include "render_core/html_parser.h"
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
    if (unsupported_relative.size() != 1 || unsupported_relative[0].url != "/apps/demo/styles/images/background.bmp") {
        std::cerr << "FAIL resource_reference_tests: unsupported relative CSS URL entered snapshot list\n";
        return 1;
    }


    using namespace jellyframe_esp32s3;
    std::string prepared;
    const std::string untouched =
        R"CSS(/* background:url(x) */ .a{content:"url(x); background:url(y)"; --bg:url(z); /*opaque*/ --block:{background:url(z);}; color:red})CSS";
    assert(prepare_stylesheet_resources(untouched, "/styles/main.css", prepared) &&
           prepared == untouched);
    assert(prepare_stylesheet_resources(".a{background-image:url(../../images/a.bmp)}",
                                        "/apps/demo/styles/nested/theme.css", prepared));
    assert(prepared.find("/apps/demo/images/a.bmp") != std::string::npos);
    for (const std::string bad :
         {"../../../escape.bmp", "//remote/a.bmp", "https://x/a.bmp", "data:x", "a.bmp?x",
          "a.bmp#x", "a%20.bmp", "a\\b.bmp", "a b.bmp", ""}) {
        assert(!prepare_stylesheet_resources(".a{background-image:url(\"" + bad + "\")}",
                                             "/x/a.css", prepared));
        assert(prepared.empty());
    }
    assert(prepare_stylesheet_resources(".a{background-image:url('bad)}", "/x/a.css", prepared));
    const std::string ignored = ".a{background:red;background:url(https://bad) unsupported}";
    assert(prepare_stylesheet_resources(ignored, "/x/a.css", prepared) && prepared == ignored);
    assert(!prepare_stylesheet_resources(".a{background:url(a.bmp)}", "/long/base/main.css",
                                         prepared, 29));
    assert(stylesheet_rel_token("alternate StYlEsHeEt") && !stylesheet_rel_token("notstylesheet"));
    // The real document combination callback must carry each sheet's base before
    // source identity is lost through concatenation.
    const std::string first = ".first{background-image:url(icon.bmp)}";
    const std::string second = ".second{background-image:url(icon.bmp)}";
    const ResourceEntry sheets[] = {
        {"/apps/a/theme.css", jellyframe::HostResourceKind::Stylesheet,
         reinterpret_cast<const std::uint8_t*>(first.data()), first.size()},
        {"/apps/b/theme.css", jellyframe::HostResourceKind::Stylesheet,
         reinterpret_cast<const std::uint8_t*>(second.data()), second.size()}};
    const ResourceBundle sheet_bundle{sheets, 2};
    auto sheet_context =
        make_resource_context(jellyframe::HostBudgets{}, "/apps/pages/index.html", sheet_bundle);
    sheet_context.prepare_stylesheets = true;
    auto document = jellyframe::HtmlParser{}.parse(
        R"HTML(<link rel="alternate STYLESHEET" href="../a/theme.css"><link rel="stylesheet" href="../b/theme.css"><style>.inline{background-image:url(inline.bmp)}</style><div style="background-image:url(attr.bmp)"></div>)HTML");
    assert(document && prepare_document_styles(*document, "/apps/pages/index.html"));
    auto combined =
        jellyframe::combine_author_css("", *document, load_linked_stylesheet, &sheet_context);
    std::vector<InstalledResourceReference> combined_refs;
    collect_css_background_image_references(combined, "/apps/pages/index.html", combined_refs);
    assert(combined_refs.size() == 3);
    assert(combined.find("/apps/a/icon.bmp") != std::string::npos);
    assert(combined.find("/apps/b/icon.bmp") != std::string::npos);
    assert(combined.find("/apps/pages/inline.bmp") != std::string::npos);

    const std::uint8_t bmp_fixture[] = {0x42, 0x4d};
    const jellyframe_esp32s3::ResourceEntry bmp_entry{"/apps/demo/images/background.bmp",
                                                      jellyframe::HostResourceKind::Image,
                                                      bmp_fixture, sizeof(bmp_fixture)};
    const jellyframe_esp32s3::ResourceBundle bundle{&bmp_entry, 1};
    const jellyframe::HostBudgets budgets{};
    auto context =
        jellyframe_esp32s3::make_resource_context(budgets, "/apps/demo/index.html", bundle);
    std::string loaded;
    if (!jellyframe_esp32s3::load_resource(
            jellyframe::HostResourceRequest{jellyframe::HostResourceKind::Image,
                                            references.front().url, references.front().base_url},
            loaded, &context) ||
        loaded.size() != sizeof(bmp_fixture)) {
        std::cerr << "FAIL resource_reference_tests: snapshot resource bundle could not provide "
                     "BMP bytes\n";
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
