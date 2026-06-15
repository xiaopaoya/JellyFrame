#include "core/host.h"

#include <cassert>

using namespace wearweb;

int main() {
    HostDeviceCapabilities caps;
    caps.display.width = 240;
    caps.display.height = 240;
    caps.display.preferred_pixel_format = HostPixelFormat::Rgb565;
    caps.display.has_full_framebuffer = false;
    caps.input.touch = true;
    caps.input.crown = true;
    caps.memory.total_heap_bytes = 512 * 1024;
    caps.memory.max_single_allocation_bytes = 96 * 1024;
    caps.budgets.max_dom_nodes = 768;

    assert(caps.display.width == 240);
    assert(caps.display.height == 240);
    assert(caps.display.preferred_pixel_format == HostPixelFormat::Rgb565);
    assert(!caps.display.has_full_framebuffer);
    assert(caps.display.supports_partial_present);
    assert(caps.input.touch);
    assert(caps.input.crown);
    assert(!caps.input.keyboard);
    assert(caps.memory.total_heap_bytes == 512 * 1024);
    assert(caps.memory.max_single_allocation_bytes == 96 * 1024);
    assert(caps.budgets.max_dom_nodes == 768);
    assert(caps.has_monotonic_clock);
    assert(!caps.has_filesystem);
    assert(!caps.has_network);

    return 0;
}
