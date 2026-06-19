#pragma once

#include "render_core/software_renderer.h"
#include "render_core/text_backend.h"

namespace jellyframe {

using HostTextMeasureCallback = bool (*)(const std::string& text,
                                         int font_size,
                                         int font_weight,
                                         TextMetrics* metrics,
                                         void* context);

using HostTextPaintCallback = bool (*)(FrameBuffer& target,
                                       Rect rect,
                                       Color color,
                                       const std::string& text,
                                       int font_size,
                                       int font_weight,
                                       TextCommandAlign align,
                                       bool single_line,
                                       void* context);

struct HostTextAdapter {
    HostTextMeasureCallback measure = nullptr;
    HostTextPaintCallback paint = nullptr;
    // The host owns this context and must keep it alive while layout/rendering uses the adapter.
    void* context = nullptr;
};

TextMeasureProvider text_measure_provider_from_adapter(const HostTextAdapter& adapter);
TextPainter text_painter_from_adapter(const HostTextAdapter& adapter);
bool host_text_adapter_ready(const HostTextAdapter& adapter);

} // namespace jellyframe
