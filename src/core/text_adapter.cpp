#include "core/text_adapter.h"

namespace jellyframe {
namespace {

bool measure_bridge(const std::string& text,
                    int font_size,
                    int font_weight,
                    TextMetrics* metrics,
                    void* context) {
    const auto* adapter = static_cast<const HostTextAdapter*>(context);
    if (adapter == nullptr || adapter->measure == nullptr) {
        return false;
    }
    return adapter->measure(text, font_size, font_weight, metrics, adapter->context);
}

bool paint_bridge(FrameBuffer& target,
                  Rect rect,
                  Color color,
                  const std::string& text,
                  int font_size,
                  int font_weight,
                  TextCommandAlign align,
                  bool single_line,
                  void* context) {
    const auto* adapter = static_cast<const HostTextAdapter*>(context);
    if (adapter == nullptr || adapter->paint == nullptr) {
        return false;
    }
    return adapter->paint(target, rect, color, text, font_size, font_weight, align, single_line, adapter->context);
}

} // namespace

TextMeasureProvider text_measure_provider_from_adapter(const HostTextAdapter& adapter) {
    return TextMeasureProvider{adapter.measure != nullptr ? measure_bridge : nullptr,
                               adapter.measure != nullptr ? const_cast<HostTextAdapter*>(&adapter) : nullptr};
}

TextPainter text_painter_from_adapter(const HostTextAdapter& adapter) {
    return TextPainter{adapter.paint != nullptr ? paint_bridge : nullptr,
                       adapter.paint != nullptr ? const_cast<HostTextAdapter*>(&adapter) : nullptr};
}

bool host_text_adapter_ready(const HostTextAdapter& adapter) {
    return adapter.measure != nullptr && adapter.paint != nullptr;
}

} // namespace jellyframe
