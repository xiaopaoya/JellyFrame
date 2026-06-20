#pragma once

#include "render_core/diagnostics.h"
#include "render_core/dom.h"
#include "render_core/style.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jellyframe {

struct StyleOverride {
    const Node* node = nullptr;
    bool has_opacity = false;
    float opacity = 1.0F;
    bool has_color = false;
    Color color;
    bool has_background_color = false;
    Color background_color;
    bool has_transform = false;
    std::string transform;
};

struct AnimationTimelineOptions {
    std::size_t max_active_animations = 16;
    DiagnosticSink* diagnostics = nullptr;
};

struct AnimationTimelineStatistics {
    std::size_t active_animations = 0;
    std::size_t sampled_overrides = 0;
    std::size_t rejected_animations = 0;
};

enum class AnimatedTransitionProperty {
    Opacity,
    Transform,
    BackgroundColor,
    Color,
};

class AnimationTimeline {
public:
    explicit AnimationTimeline(AnimationTimelineOptions options = {});

    void clear();
    bool empty() const;
    std::size_t active_count() const;
    AnimationTimelineStatistics statistics() const;

    bool start_transitions(const Node& node,
                           const Style& from_style,
                           const Style& to_style,
                           std::uint64_t now_ms);
    bool sample(std::uint64_t now_ms, std::vector<StyleOverride>& overrides);

private:
    struct ActiveTransition {
        const Node* node = nullptr;
        AnimatedTransitionProperty property = AnimatedTransitionProperty::Opacity;
        std::uint64_t start_ms = 0;
        std::uint32_t duration_ms = 0;
        std::uint32_t delay_ms = 0;
        AnimationTimingFunction timing = AnimationTimingFunction::Ease;
        float from_opacity = 1.0F;
        float to_opacity = 1.0F;
        Color from_color;
        Color to_color;
        Transform2D from_transform;
        Transform2D to_transform;
    };

    AnimationTimelineOptions options_;
    std::vector<ActiveTransition> active_;
    std::size_t rejected_animations_ = 0;
};

} // namespace jellyframe
