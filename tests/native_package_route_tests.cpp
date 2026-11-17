#include "app_package.h"
#include <array>
#include <cassert>
#include <fstream>
#include <functional>
#include <string>

static void rejects(const std::function<void()>& operation, const char* reason) {
    bool rejected = false;
    try { operation(); }
    catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find(reason) != std::string::npos;
    }
    assert(rejected);
}

int main(int argc, char** argv) {
    using namespace jellyframe_example;
    assert(argc == 3);
    const auto sample = read_text_file_limited(argv[1], 65536);
    assert(manifest_json_is_strict(sample));
    // Source examples target the production Core release. This route test also
    // runs against the bundled legacy Core, so build a scalar fixture for the
    // configured lines rather than relaxing product version checks.
    const auto original = std::string("{\"id\":\"org.jellyframe.route-test\",\"entry\":\"/index.html\",\"runtime\":{") +
        "\"minJellyFrame\":\"" + std::string(kActiveJellyFrameRuntimeVersion) +
        "\",\"minRenderCore\":\"" + std::string(kActiveRenderCoreVersion) + "\",\"script\":\"none\"}}";
    assert(!parse_app_manifest_text(original).id.empty());
    auto shaped = original;
    shaped.insert(shaped.rfind('}'), ",\"textPipeline\":null");
    rejects([&] { (void)parse_app_manifest_text(shaped); }, "does not support package-selected shaped textPipeline");
    // Current desktop scalar loader has no shaped worker connection. It must
    // refuse both loose source declarations and the mandatory container tag.
    std::array<unsigned char, kJfappHeaderSize> header{};
    std::copy_n("JFAPPV0", 7, header.begin());
    header[8] = static_cast<unsigned char>(kJfappHeaderSize);
    header[10] = 1;
    {
        std::ofstream file(argv[2], std::ios::binary);
        file.write(reinterpret_cast<const char*>(header.data()), header.size());
        assert(file.good());
    }
    rejects([&] { (void)load_jfapp_bundle(argv[2], 65536); }, "unsupported .jfapp format version");
    return 0;
}
