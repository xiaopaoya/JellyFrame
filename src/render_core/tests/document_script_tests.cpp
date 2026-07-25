#include "render_core/document_script.h"
#include "render_core/html_parser.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool has_diagnostic_code(const VectorDiagnosticSink& sink, const std::string& code) {
    for (const Diagnostic& diagnostic : sink.diagnostics()) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

bool script_loader(std::string_view src, std::string& output, void*) {
    if (src == "app.js") {
        output = "window.loaded = true;";
        return true;
    }
    return false;
}

void inline_classic_scripts_are_collected() {
    HtmlParser parser;
    auto document = parser.parse(
        "<body>"
        "<script>window.a = 1;</script>"
        "<script type='module'>window.bad = 1;</script>"
        "<script type='text/javascript'>window.b = 2;</script>"
        "<script type='text/javascript; charset=utf-8'>window.c = 3;</script>"
        "</body>");

    const std::vector<DocumentScript> scripts = collect_classic_scripts(*document);
    check(scripts.size() == 3, "only classic inline scripts are collected");
    check(scripts[0].source.find("window.a") != std::string::npos, "first inline source");
    check(scripts[1].source.find("window.b") != std::string::npos, "typed classic source");
    check(scripts[2].source.find("window.c") != std::string::npos, "typed classic source with parameters");
    check(!scripts[0].external && !scripts[1].external && !scripts[2].external, "inline scripts are not external");
}

void external_scripts_use_callback_in_document_order() {
    HtmlParser parser;
    auto document = parser.parse(
        "<body>"
        "<script>window.before = true;</script>"
        "<script src='app.js'></script>"
        "<script src='missing.js'></script>"
        "<script>window.after = true;</script>"
        "</body>");

    const std::vector<DocumentScript> scripts = collect_classic_scripts(*document, script_loader, nullptr);
    check(scripts.size() == 3, "inline and loaded external scripts are collected");
    check(!scripts[0].external && scripts[1].external && !scripts[2].external, "script external flags");
    check(scripts[1].name == "app.js", "external script name is src");
    check(scripts[1].source.find("loaded") != std::string::npos, "external script source loaded");
}

void script_collection_reports_skipped_and_failed_scripts() {
    HtmlParser parser;
    auto document = parser.parse(
        "<body>"
        "<script type='module'>export default 1;</script>"
        "<script src='missing.js'></script>"
        "</body>");

    VectorDiagnosticSink diagnostics;
    const std::vector<DocumentScript> scripts =
        collect_classic_scripts(*document, script_loader, nullptr, &diagnostics);
    check(scripts.empty(), "unsupported and missing scripts are not collected");
    check(has_diagnostic_code(diagnostics, "script-type-unsupported"), "module script skip is reported");
    check(has_diagnostic_code(diagnostics, "script-load-failed"), "missing external script is reported");
}

void deep_script_collection_is_iterative() {
    auto document = make_element("document");
    Node* current = document.get();
    for (int depth = 0; depth < 4096; ++depth) {
        current = &current->append_child(make_element("div"));
    }
    Node& script = current->append_child(make_element("script"));
    script.append_child(make_text("window.deep = true;"));

    const std::vector<DocumentScript> scripts = collect_classic_scripts(*document);
    check(scripts.size() == 1, "deep inline script is collected");
    check(scripts[0].source.find("window.deep") != std::string::npos, "deep script source is preserved");
}

void script_collection_respects_aggregate_resource_limits() {
    HtmlParser parser;
    auto document = parser.parse(
        "<body>"
        "<script>window.first = true;</script>"
        "<script src='app.js'></script>"
        "<script>window.third = true;</script>"
        "</body>");
    VectorDiagnosticSink diagnostics;
    DocumentScriptCollectionOptions options;
    options.max_scripts = 2;
    options.max_total_source_bytes = 1024;
    options.diagnostics = &diagnostics;
    const std::vector<DocumentScript> count_limited =
        collect_classic_scripts(*document, script_loader, nullptr, options);
    check(count_limited.size() == 2, "script count budget retains the first scripts only");
    check(count_limited[0].source.find("first") != std::string::npos, "first script is retained");
    check(count_limited[1].name == "app.js", "second script is retained");
    check(has_diagnostic_code(diagnostics, "script-document-resource-limit"), "script count limit is diagnosed");

    diagnostics.clear();
    options.max_scripts = 8;
    options.max_total_source_bytes = 24;
    const std::vector<DocumentScript> byte_limited =
        collect_classic_scripts(*document, script_loader, nullptr, options);
    check(byte_limited.size() == 1, "script byte budget skips complete later scripts");
    check(byte_limited[0].source.find("first") != std::string::npos, "fitting script source is retained");
    check(has_diagnostic_code(diagnostics, "script-document-resource-limit"), "script byte limit is diagnosed");
}

} // namespace

int main() {
    try {
        inline_classic_scripts_are_collected();
        external_scripts_use_callback_in_document_order();
        script_collection_reports_skipped_and_failed_scripts();
        deep_script_collection_is_iterative();
        script_collection_respects_aggregate_resource_limits();
    } catch (const std::exception& error) {
        std::cerr << "document script test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "document script tests passed\n";
    return 0;
}
