#include <iostream>

int tokenizer_tests_main();
int dom_mutation_tests_main();
int document_script_tests_main();
int dirty_region_tests_main();
int embedded_framebuffer_tests_main();
int css_parser_tests_main();
int event_tests_main();
int hit_test_tests_main();
int host_tests_main();
int input_tests_main();
int render_tree_tests_main();
int layer_tree_tests_main();
int software_renderer_tests_main();

#if defined(WEARWEB_ENABLE_SCRIPTING)
int script_runtime_tests_main();
#endif

namespace {

int run_test(const char* name, int (*test_main)()) {
    std::cout << "[ RUN      ] " << name << '\n';
    const int result = test_main();
    if (result == 0) {
        std::cout << "[       OK ] " << name << '\n';
    } else {
        std::cout << "[  FAILED  ] " << name << '\n';
    }
    return result == 0 ? 0 : 1;
}

} // namespace

int main() {
    int failed = 0;
    failed += run_test("tokenizer", tokenizer_tests_main);
    failed += run_test("dom_mutation", dom_mutation_tests_main);
    failed += run_test("document_script", document_script_tests_main);
    failed += run_test("dirty_region", dirty_region_tests_main);
    failed += run_test("embedded_framebuffer", embedded_framebuffer_tests_main);
    failed += run_test("css_parser", css_parser_tests_main);
    failed += run_test("event", event_tests_main);
    failed += run_test("hit_test", hit_test_tests_main);
    failed += run_test("host", host_tests_main);
    failed += run_test("input", input_tests_main);
    failed += run_test("render_tree", render_tree_tests_main);
    failed += run_test("layer_tree", layer_tree_tests_main);
    failed += run_test("software_renderer", software_renderer_tests_main);

#if defined(WEARWEB_ENABLE_SCRIPTING)
    failed += run_test("script_runtime", script_runtime_tests_main);
#endif

    if (failed != 0) {
        std::cerr << failed << " test group(s) failed\n";
        return 1;
    }

    std::cout << "all core tests passed\n";
    return 0;
}
