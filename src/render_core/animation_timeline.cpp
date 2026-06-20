#include "render_core/animation_timeline.h"

#include <algorithm>
#include <cmath>

namespace jellyframe {
namespace {

bool property_matches(AnimatableProperty transition_property, AnimatedTransitionProperty property) {
    if (transition_property == AnimatableProperty::All) {
        return true;
    }
    switch (property) {
    case AnimatedTransitionProperty::Opacity:
        return transition_property == AnimatableProperty::Opacity;
    case AnimatedTransitionProperty::Transform:
        return transition_property == AnimatableProperty::Transform;
    case AnimatedTransitionProperty::BackgroundColor:
        return transition_property == AnimatableProperty::BackgroundColor;
    case AnimatedTransitionProperty::Color:
        return transition_property == AnimatableProperty::Color;
    }
    return false;
}

const StyleTransition* transition_for_property(const Style& style, AnimatedTransitionProperty property) {
    for (std::size_t index = 0; index < style.transition_count; ++index) {
        const StyleTransition& transition = style.transitions[index];
        if (transition.duration_ms > 0 && property_matches(transition.property, property)) {
            return &transition;
        }
    }
    return nullptr;
}

bool color_equal(Color left, Color right) {
    return left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;
}

Color mix_color(Color from, Color to, float t) {
    const auto mix_channel = [&](std::uint8_t a, std::uint8_t b) {
        return static_cast<std::uint8_t>(
            std::max(0.0F, std::min(255.0F, static_cast<float>(a) +
                (static_cast<float>(b) - static_cast<float>(a)) * t)) + 0.5F);
    };
    return Color{
        mix_channel(from.r, to.r),
        mix_channel(from.g, to.g),
        mix_channel(from.b, to.b),
        mix_channel(from.a, to.a),
    };
}

float mix_float(float from, float to, float t) {
    return from + (to - from) * t;
}

float apply_timing(AnimationTimingFunction timing, float progress) {
    progress = std::max(0.0F, std::min(1.0F, progress));
    switch (timing) {
    case AnimationTimingFunction::Linear:
        return progress;
    case AnimationTimingFunction::EaseIn:
        return progress * progress;
    case AnimationTimingFunction::EaseOut:
        return 1.0F - (1.0F - progress) * (1.0F - progress);
    case AnimationTimingFunction::EaseInOut:
        return progress < 0.5F
            ? 2.0F * progress * progress
            : 1.0F - std::pow(-2.0F * progress + 2.0F, 2.0F) * 0.5F;
    case AnimationTimingFunction::Ease:
        return progress < 0.5F
            ? 2.0F * progress * progress
            : 1.0F - std::pow(-2.0F * progress + 2.0F, 2.0F) * 0.5F;
    }
    return progress;
}

bool transform_equal(const Transform2D& left, const Transform2D& right) {
    return std::abs(left.translate_x - right.translate_x) < 0.01F &&
        std::abs(left.translate_y - right.translate_y) < 0.01F &&
        std::abs(left.scale_x - right.scale_x) < 0.001F &&
        std::abs(left.scale_y - right.scale_y) < 0.001F;
}

Transform2D mix_transform(const Transform2D& from, const Transform2D& to, float t) {
    Transform2D output;
    output.translate_x = mix_float(from.translate_x, to.translate_x, t);
    output.translate_y = mix_float(from.translate_y, to.translate_y, t);
    output.scale_x = mix_float(from.scale_x, to.scale_x, t);
    output.scale_y = mix_float(from.scale_y, to.scale_y, t);
    return output;
}

StyleOverride& override_for(std::vector<StyleOverride>& overrides, const Node* node) {
    for (StyleOverride& existing : overrides) {
        if (existing.node == node) {
            return existing;
        }
    }
    StyleOverride added;
    added.node = node;
    overrides.push_back(std::move(added));
    return overrides.back();
}

} // namespace

AnimationTimeline::AnimationTimeline(AnimationTimelineOptions options)
    : options_(options) {}

void AnimationTimeline::clear() {
    active_.clear();
}

bool AnimationTimeline::empty() const {
    return active_.empty();
}

std::size_t AnimationTimeline::active_count() const {
    return active_.size();
}

AnimationTimelineStatistics AnimationTimeline::statistics() const {
    AnimationTimelineStatistics statistics;
    statistics.active_animations = active_.size();
    statistics.rejected_animations = rejected_animations_;
    return statistics;
}

bool AnimationTimeline::start_transitions(const Node& node,
                                          const Style& from_style,
                                          const Style& to_style,
                                          std::uint64_t now_ms) {
    bool started = false;
    const std::size_t max_active = std::max<std::size_t>(1, options_.max_active_animations);
    const auto append = [&](ActiveTransition transition) {
        active_.erase(std::remove_if(active_.begin(), active_.end(), [&](const ActiveTransition& existing) {
            return existing.node == transition.node && existing.property == transition.property;
        }), active_.end());
        if (active_.size() >= max_active) {
            ++rejected_animations_;
            report_diagnostic(options_.diagnostics,
                              DiagnosticStage::Style,
                              DiagnosticSeverity::Warning,
                              "animation-active-limit",
                              "Active animation budget was reached; transition snapped to final style",
                              node.tag_name.empty() ? "node" : node.tag_name);
            return;
        }
        active_.push_back(transition);
        started = true;
    };

    if (const StyleTransition* transition = transition_for_property(to_style, AnimatedTransitionProperty::Opacity);
        transition != nullptr && std::abs(from_style.opacity - to_style.opacity) >= 0.001F) {
        ActiveTransition active;
        active.node = &node;
        active.property = AnimatedTransitionProperty::Opacity;
        active.start_ms = now_ms;
        active.duration_ms = transition->duration_ms;
        active.delay_ms = transition->delay_ms;
        active.timing = transition->timing;
        active.from_opacity = from_style.opacity;
        active.to_opacity = to_style.opacity;
        append(active);
    }
    if (const StyleTransition* transition = transition_for_property(to_style, AnimatedTransitionProperty::BackgroundColor);
        transition != nullptr && !color_equal(from_style.background_color, to_style.background_color)) {
        ActiveTransition active;
        active.node = &node;
        active.property = AnimatedTransitionProperty::BackgroundColor;
        active.start_ms = now_ms;
        active.duration_ms = transition->duration_ms;
        active.delay_ms = transition->delay_ms;
        active.timing = transition->timing;
        active.from_color = from_style.background_color;
        active.to_color = to_style.background_color;
        append(active);
    }
    if (const StyleTransition* transition = transition_for_property(to_style, AnimatedTransitionProperty::Color);
        transition != nullptr && !color_equal(from_style.color, to_style.color)) {
        ActiveTransition active;
        active.node = &node;
        active.property = AnimatedTransitionProperty::Color;
        active.start_ms = now_ms;
        active.duration_ms = transition->duration_ms;
        active.delay_ms = transition->delay_ms;
        active.timing = transition->timing;
        active.from_color = from_style.color;
        active.to_color = to_style.color;
        append(active);
    }
    if (const StyleTransition* transition = transition_for_property(to_style, AnimatedTransitionProperty::Transform);
        transition != nullptr) {
        Transform2D from_transform;
        Transform2D to_transform;
        if (parse_css_transform_2d(from_style.transform, from_transform) &&
            parse_css_transform_2d(to_style.transform, to_transform) &&
            !transform_equal(from_transform, to_transform)) {
            ActiveTransition active;
            active.node = &node;
            active.property = AnimatedTransitionProperty::Transform;
            active.start_ms = now_ms;
            active.duration_ms = transition->duration_ms;
            active.delay_ms = transition->delay_ms;
            active.timing = transition->timing;
            active.from_transform = from_transform;
            active.to_transform = to_transform;
            append(active);
        }
    }
    return started;
}

bool AnimationTimeline::sample(std::uint64_t now_ms, std::vector<StyleOverride>& overrides) {
    overrides.clear();
    if (active_.empty()) {
        return false;
    }
    std::vector<ActiveTransition> remaining;
    remaining.reserve(active_.size());
    for (const ActiveTransition& active : active_) {
        const std::uint64_t begin = active.start_ms + active.delay_ms;
        const float raw_progress = now_ms <= begin
            ? 0.0F
            : static_cast<float>(now_ms - begin) / static_cast<float>(std::max<std::uint32_t>(1, active.duration_ms));
        const float progress = std::min(1.0F, raw_progress);
        const float eased = apply_timing(active.timing, progress);
        StyleOverride& override = override_for(overrides, active.node);
        switch (active.property) {
        case AnimatedTransitionProperty::Opacity:
            override.has_opacity = true;
            override.opacity = mix_float(active.from_opacity, active.to_opacity, eased);
            break;
        case AnimatedTransitionProperty::BackgroundColor:
            override.has_background_color = true;
            override.background_color = mix_color(active.from_color, active.to_color, eased);
            break;
        case AnimatedTransitionProperty::Color:
            override.has_color = true;
            override.color = mix_color(active.from_color, active.to_color, eased);
            break;
        case AnimatedTransitionProperty::Transform:
            override.has_transform = true;
            override.transform = serialize_css_transform_2d(mix_transform(active.from_transform, active.to_transform, eased));
            break;
        }
        if (progress < 1.0F) {
            remaining.push_back(active);
        }
    }
    active_.swap(remaining);
    return !overrides.empty();
}

} // namespace jellyframe
