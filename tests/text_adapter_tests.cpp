#include "core/text_adapter.h"

#include <iostream>
#include <stdexcept>

using namespace jellyframe;

namespace {

struct ProbeTextBackend {
    int measure_calls = 0;
    int paint_calls = 0;
    int last_font_weight = 0;
};

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool probe_measure(const std::string& text,
                   int font_size,
                   int font_weight,
                   TextMetrics* metrics,
                   void* context) {
    auto* probe = static_cast<ProbeTextBackend*>(context);
    if (probe == nullptr || metrics == nullptr) {
        return false;
    }
    ++probe->measure_calls;
    probe->last_font_weight = font_weight;
    metrics->width = static_cast<int>(text.size()) * font_size;
    metrics->line_height = font_size + 2;
    return true;
}

bool probe_paint(FrameBuffer& target,
                 Rect rect,
                 Color color,
                 const std::string&,
                 int,
                 int font_weight,
                 TextCommandAlign,
                 bool,
                 void* context) {
    auto* probe = static_cast<ProbeTextBackend*>(context);
    if (probe == nullptr || rect.width <= 0 || rect.height <= 0 || color.a == 0) {
        return false;
    }
    ++probe->paint_calls;
    probe->last_font_weight = font_weight;
    if (target.contains(rect.x, rect.y)) {
        target.pixel(rect.x, rect.y) = color;
    }
    return true;
}

void adapter_wraps_measure_and_paint_callbacks() {
    ProbeTextBackend probe;
    HostTextAdapter adapter{probe_measure, probe_paint, &probe};

    const TextMeasureProvider measure = text_measure_provider_from_adapter(adapter);
    TextMetrics metrics = measure_text(measure, "ABC", 10, 700);
    check(metrics.width == 30 && metrics.line_height == 12, "adapter forwards measure");
    check(probe.measure_calls == 1 && probe.last_font_weight == 700, "measure uses host context");

    FrameBuffer target(8, 8, Color{255, 255, 255, 255});
    const TextPainter painter = text_painter_from_adapter(adapter);
    check(painter.paint != nullptr, "adapter creates painter");
    check(painter.paint(target, Rect{1, 1, 4, 4}, Color{1, 2, 3, 255}, "A", 10, 600,
                        TextCommandAlign::Start, true, painter.context),
          "adapter forwards paint");
    check(probe.paint_calls == 1 && probe.last_font_weight == 600, "paint uses host context");
    check(target.pixel(1, 1).r == 1 && target.pixel(1, 1).g == 2, "paint touched framebuffer");
    check(host_text_adapter_ready(adapter), "complete adapter is ready");
}

void incomplete_adapter_degrades_to_core_fallbacks() {
    HostTextAdapter adapter;
    check(!host_text_adapter_ready(adapter), "empty adapter is not ready");
    check(text_measure_provider_from_adapter(adapter).measure == nullptr, "empty adapter has no measure");
    check(text_painter_from_adapter(adapter).paint == nullptr, "empty adapter has no painter");
}

} // namespace

int main() {
    try {
        adapter_wraps_measure_and_paint_callbacks();
        incomplete_adapter_degrades_to_core_fallbacks();
    } catch (const std::exception& error) {
        std::cerr << "text adapter test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "text adapter tests passed\n";
    return 0;
}
