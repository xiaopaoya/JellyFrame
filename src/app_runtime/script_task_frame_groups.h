#pragma once

#include "app_runtime/script_task_command_access.h"

#include "app_runtime/script_task_frame_codec.h"
#include "app_runtime/script_task_sampling.h"

#include <algorithm>
#include <cmath>
#include <limits>

// This structural API capability is independent of optional feature cropping.
// The locked Core predates axis clips, ellipse sidecars and checked paint.
#if defined(JELLYFRAME_RENDER_CORE_LAYER_VALUE_API_VERSION) && JELLYFRAME_RENDER_CORE_LAYER_VALUE_API_VERSION >= 1
#define JELLYFRAME_SCRIPT_TASK_GROUP_API 1
#else
#define JELLYFRAME_SCRIPT_TASK_GROUP_API 0
#endif

namespace jellyframe::detail {

constexpr bool kScriptTaskGroupTransportAvailable = JELLYFRAME_SCRIPT_TASK_GROUP_API != 0;

inline bool valid_group_float(float value) {
    return std::isfinite(value);
}

inline bool valid_group_rect(Rect value) {
    return value.width >= 0 && value.height >= 0;
}

inline bool valid_group_radii(const std::array<int, 8>& radii) {
    return std::all_of(radii.begin(), radii.end(), [](int radius) { return radius >= 0; });
}

inline bool valid_group_command(const DisplayCommand& command) {
    const auto type = command_type(command);
    bool supported = legacy_command_type(type);
#if JELLYFRAME_SCRIPT_TASK_GROUP_API
    supported = supported || type == DisplayCommandType::GlyphRun ||
        type == DisplayCommandType::BaselineText || type == DisplayCommandType::OriginText ||
        type == DisplayCommandType::BorderRing;
#endif
    if (!supported || command_has_untransported_payload(command)) return false;
    const auto p = legacy_command_fields(command);
    return static_cast<unsigned>(p.text_align) <= static_cast<unsigned>(TextCommandAlign::End) &&
        static_cast<unsigned>(p.gradient_axis) <= static_cast<unsigned>(GradientAxis::RadialPosition) &&
        static_cast<unsigned>(p.object_fit) <= static_cast<unsigned>(ObjectFit::ScaleDown) &&
        static_cast<unsigned>(p.image_rendering) <= static_cast<unsigned>(ImageRendering::CrispEdges) &&
        valid_group_rect(command.rect) && valid_group_rect(command_sampling_rect(command)) &&
        !command.transform.enabled && command.transform.source_clip_index == kScriptTaskNoClip;
}

// Besides acyclicity, require preorder: children cannot return to a subtree
// already closed. This keeps paint order unambiguous and validation linear.
inline ScriptTaskAppFrameCodecStatus validate_frame_groups(
    const ScriptTaskAppFrame& frame, std::size_t max_groups, std::size_t max_depth) {
    using Status = ScriptTaskAppFrameCodecStatus;
    if (frame.groups.empty())
        return frame.command_ellipses.empty() ? Status::Accepted : Status::InvalidGroup;
    if (!kScriptTaskGroupTransportAvailable) return Status::UnsupportedGroupFeature;
    if (frame.groups.size() > max_groups || frame.groups.size() > UINT32_MAX)
        return Status::TooManyGroups;
    if (frame.command_ellipses.size() > frame.display_list.size()) return Status::InvalidGroup;
    if (!frame.display_clip_indices.empty()) return Status::InvalidGroup;
    std::vector<std::uint32_t> ancestors;
    ancestors.reserve(std::min(max_depth, frame.groups.size()));
    std::size_t next_command = 0;
    for (std::size_t index = 0; index < frame.groups.size(); ++index) {
        const auto& group = frame.groups[index];
        if (index == 0) {
            if (group.parent != kScriptTaskNoParentClip) return Status::InvalidGroup;
        } else {
            while (!ancestors.empty() && ancestors.back() != group.parent) ancestors.pop_back();
            if (ancestors.empty()) return Status::InvalidGroup;
        }
        if (ancestors.size() >= max_depth) return Status::InvalidGroup;
        ancestors.push_back(static_cast<std::uint32_t>(index));
        if (group.command_begin != next_command || next_command > frame.display_list.size() ||
            group.command_count > frame.display_list.size() - next_command ||
            !valid_group_rect(group.bounds) || !valid_group_rect(group.clip_rect) ||
            group.clip_axes > 3 ||
            static_cast<unsigned>(group.type) > static_cast<unsigned>(LayerType::Composited) ||
            !valid_group_float(group.opacity) || group.opacity < 0 || group.opacity > 1 ||
            !valid_group_float(group.transform.translate_x) || !valid_group_float(group.transform.translate_y) ||
            !valid_group_float(group.transform.scale_x) || !valid_group_float(group.transform.scale_y) ||
            !valid_group_float(group.transform.rotate_degrees) ||
            !std::all_of(group.affine.begin(), group.affine.end(), valid_group_float) ||
            !valid_group_radii(group.clip_ellipse)) return Status::InvalidGroup;
#if !JELLYFRAME_RENDER_CORE_AFFINE2D_ENABLED
        if (group.has_affine) return Status::UnsupportedGroupFeature;
#endif
        next_command += group.command_count;
    }
    if (next_command != frame.display_list.size()) return Status::InvalidGroup;
    for (const auto& command : frame.display_list) {
        if (!valid_group_command(command))
            return Status::InvalidGroup;
    }
    std::uint32_t previous = 0;
    bool first = true;
    for (const auto& ellipse : frame.command_ellipses) {
        if (ellipse.command_index >= next_command || (!first && ellipse.command_index <= previous) ||
            !valid_group_radii(ellipse.radii)) return Status::InvalidGroup;
        first = false;
        previous = ellipse.command_index;
    }
    return Status::Accepted;
}

#if JELLYFRAME_SCRIPT_TASK_GROUP_API
inline std::array<int, 8> group_radii(const EllipticalCornerRadii& radii) {
    return {{radii.horizontal.top_left, radii.horizontal.top_right,
             radii.horizontal.bottom_right, radii.horizontal.bottom_left,
             radii.vertical.top_left, radii.vertical.top_right,
             radii.vertical.bottom_right, radii.vertical.bottom_left}};
}

inline EllipticalCornerRadii group_radii(const std::array<int, 8>& radii) {
    return {{radii[0], radii[1], radii[2], radii[3]},
            {radii[4], radii[5], radii[6], radii[7]}};
}

// A single contiguous node array owns the reconstructed paint graph. Child
// pointers borrow this array (arena_owned deleters); destruction is iterative,
// even if a malformed external frame attempted a deep hierarchy.
class FrameGroupTree {
public:
    explicit FrameGroupTree(const ScriptTaskAppFrame& frame) : nodes_(frame.groups.size()) {
        for (std::size_t index = 0; index < nodes_.size(); ++index) {
            const auto& value = frame.groups[index];
            auto& node = nodes_[index];
            node.type = value.type;
            node.bounds = value.bounds;
            node.clip_rect = value.clip_rect;
            node.has_clip = value.has_clip;
            node.clip_axes = value.clip_axes;
            node.clip_border_radius = value.clip_border_radius;
            node.opacity = value.opacity;
            node.transform = value.transform;
            node.transform_origin_x_percent = value.origin_x_percent;
            node.transform_origin_y_percent = value.origin_y_percent;
#if JELLYFRAME_RENDER_CORE_AFFINE2D_ENABLED
            if (value.has_affine) node.affine_transform = std::make_unique<AffineTransform2D>(
                AffineTransform2D{value.affine[0], value.affine[1], value.affine[2],
                                  value.affine[3], value.affine[4], value.affine[5]});
#endif
            if (value.has_clip_ellipse)
                node.clip_ellipse_geometry = std::make_unique<EllipticalCornerRadii>(group_radii(value.clip_ellipse));
            node.display_list.assign(frame.display_list.begin() + value.command_begin,
                frame.display_list.begin() + value.command_begin + value.command_count);
            const auto begin = std::lower_bound(frame.command_ellipses.begin(), frame.command_ellipses.end(),
                value.command_begin, [](const auto& ellipse, std::uint32_t command) { return ellipse.command_index < command; });
            for (auto it = begin; it != frame.command_ellipses.end() &&
                 it->command_index < value.command_begin + value.command_count; ++it) {
                if (!node.ellipse_geometry) node.ellipse_geometry = std::make_unique<CommandEllipseGeometryList>();
                node.ellipse_geometry->entries.push_back({it->command_index - value.command_begin, group_radii(it->radii)});
            }
            if (index != 0)
                nodes_[value.parent].children.emplace_back(&node, LayerNodeDeleter{true});
        }
    }
    const LayerNode& root() const { return nodes_.front(); }
private:
    std::vector<LayerNode> nodes_;
};
#endif

} // namespace jellyframe::detail
