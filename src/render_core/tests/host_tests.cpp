#include "render_core/budget.h"
#include "render_core/host.h"

#include <cassert>

using namespace jellyframe;

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
    caps.async.runs_jobs_off_ui_thread = true;
    caps.async.supports_cancel = true;
    caps.async.max_in_flight_jobs = 2;
    caps.async.max_completion_events_per_frame = 2;
    caps.media.supports_image_decode = true;
    caps.media.supports_mp3 = true;
    caps.media.preferred_decoded_image_format = HostPixelFormat::Rgb565;
    caps.media.max_image_width = 240;
    caps.media.max_image_height = 240;
    caps.media.max_decoded_image_bytes = 240 * 240 * 2;
    caps.network.max_response_bytes = 16 * 1024;
    caps.app_bundles.max_bundle_bytes = 512 * 1024;
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
    assert(caps.async.runs_jobs_off_ui_thread);
    assert(caps.async.supports_cancel);
    assert(caps.async.max_in_flight_jobs == 2);
    assert(caps.async.max_completion_events_per_frame == 2);
    assert(caps.media.supports_image_decode);
    assert(caps.media.supports_mp3);
    assert(!caps.media.supports_h264);
    assert(caps.media.preferred_decoded_image_format == HostPixelFormat::Rgb565);
    assert(caps.media.max_image_width == 240);
    assert(caps.media.max_image_height == 240);
    assert(caps.media.max_decoded_image_bytes == 240 * 240 * 2);
    assert(!caps.network.supports_fetch);
    assert(!caps.network.allows_remote_page_resources);
    assert(caps.network.max_response_bytes == 16 * 1024);
    assert(!caps.app_bundles.supports_installable_bundles);
    assert(caps.app_bundles.max_bundle_bytes == 512 * 1024);
    assert(caps.budgets.max_dom_nodes == 768);
    caps.budgets.max_css_rules = 99;
    caps.budgets.max_css_declarations_per_rule = 7;
    caps.budgets.max_render_objects = 66;
    caps.budgets.max_layout_boxes = 55;
    caps.budgets.max_layers = 12;
    caps.budgets.max_display_commands = 34;
    caps.budgets.max_dirty_rects = 3;
    caps.budgets.max_detached_dom_nodes = 5;
    caps.budgets.max_input_events_per_frame = 4;
    caps.budgets.max_timer_callbacks_per_frame = 2;
    caps.budgets.max_framebuffer_pixels = 240 * 240;
    assert(caps.has_monotonic_clock);
    assert(!caps.has_filesystem);
    assert(!caps.has_network);
    assert(html_parser_options_from_budgets(caps.budgets).max_nodes == 768);
    assert(css_parser_options_from_budgets(caps.budgets).max_rules == 99);
    assert(css_parser_options_from_budgets(caps.budgets).max_declarations_per_rule == 7);
    assert(render_tree_options_from_budgets(caps.budgets).max_render_objects == 66);
    assert(layout_engine_options_from_budgets(caps.budgets).max_layout_boxes == 55);
    assert(layer_tree_options_from_budgets(caps.budgets).max_layers == 12);
    assert(layer_tree_options_from_budgets(caps.budgets).max_display_commands == 34);
    assert(dirty_region_options_from_budgets(caps.budgets, Rect{0, 0, 1, 1}).max_rects == 3);
    assert(caps.budgets.max_detached_dom_nodes == 5);
    assert(frame_loop_options_from_budgets(caps.budgets).max_input_events_per_frame == 4);
    assert(frame_loop_options_from_budgets(caps.budgets).max_timer_callbacks_per_frame == 2);
    assert(framebuffer_size_fits_budget(240, 240, caps.budgets));
    assert(!framebuffer_size_fits_budget(241, 241, caps.budgets));

    return 0;
}
