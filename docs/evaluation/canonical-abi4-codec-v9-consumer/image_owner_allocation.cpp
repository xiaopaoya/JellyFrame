#include "app_runtime/script_task_canonical_codec.h"
#include "app_runtime/script_task_frame_renderer.h"
#include <cassert>
#include <cstdlib>
#include <new>
#include <iostream>
static long fault = -1, calls = 0;
void* operator new(std::size_t bytes) {
    ++calls;
    if (fault == 0) { fault = -1; throw std::bad_alloc(); }
    if (fault > 0) --fault;
    if (void* p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
using namespace jellyframe;
class Images final : public RuntimeImageProviderSource {
public:
    const Color pixel{17,33,91,255};
    std::uint64_t generation() const noexcept override { return 1; }
    bool contains(std::uint32_t h) const noexcept override { return h == 73; }
    bool resolve(std::string_view url, ResolvedImageResource& out) const override { out = {}; if (url != "/image.bmp") return false; out.image_handle = 73; return true; }
    bool paint(FrameBuffer& target, const ImagePaintRequest&) const override { for (auto& p : target.pixels) p = pixel; return true; }
};
int main() {
    const auto source = std::make_shared<Images>();
    RuntimeImageProvider::Handle owner;
    assert(RuntimeImageProvider::create(source, owner) == RuntimeImageProviderStatus::Ready);
    const auto old = owner;
    for (long n = 0; n < 2; ++n) {
        fault = n;
        assert(RuntimeImageProvider::create(source, owner) == RuntimeImageProviderStatus::OutOfMemory);
        fault = -1; assert(owner == old && owner->contains(73));
    }
    ScriptTaskAppFrame frame; frame.viewport = {0,0,4,4}; frame.image_provider_id = owner->id();
    ScriptTaskFrameGroup root; root.parent = kScriptTaskNoParentClip; root.type = LayerType::Root; root.bounds = frame.viewport; root.command_count = 1;
    frame.groups.push_back(root);
    DisplayCommand command; command.set_type(DisplayCommandType::Image); command.rect = frame.viewport; command.image_paint().image_handle = 73; frame.display_list.push_back(command);
    ScriptTaskFrameRendererOptions options; options.image_provider = owner; ScriptTaskFrameRenderer renderer({}, {}, options);
    FrameBuffer target(4,4,{1,2,3,4});
    calls = 0; assert(renderer.render_into(frame, target, {})); const long allocations = calls;
    assert(allocations > 0);
    for (long n = 0; n < allocations; ++n) {
        target = FrameBuffer(4,4,{1,2,3,4});
        fault = n;
        const bool painted = renderer.render_into(frame, target, {});
        fault = -1;
        assert(!painted);
        for (const auto p : target.pixels) assert(p.r == 1 && p.g == 2 && p.b == 3 && p.a == 4);
        assert(owner->contains(73));
    }
    assert(renderer.render_into(frame, target, {}));
    std::cout << "image owner factory 2 allocation faults and replay " << allocations << " allocation faults preserve owner/pixels; retry passed\n";
}
