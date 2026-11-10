#pragma once

#include "app_runtime/script_task_contract.h"
#include "render_core/geometry.h"
#include "render_core/layer_tree.h"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <array>

namespace jellyframe {

constexpr std::uint16_t kScriptTaskNoClip = 0xffffU;
constexpr std::uint32_t kScriptTaskNoParentClip = 0xffffffffU;

struct ScriptTaskInputTarget {
    std::uint32_t target_key = 0;
    Rect rect;
    bool enabled = true;
    std::uint16_t clip_index = kScriptTaskNoClip;
};

struct ScriptTaskFrameClip {
    Rect rect;
    int border_radius = 0;
    std::uint32_t parent_clip = kScriptTaskNoParentClip;
};

// Preorder, value-owned paint groups. Commands remain in source coordinates;
// the compositor applies group clips, opacity and transforms in tree order.
// Float fields use finite IEEE-754 binary32 values on the wire, without the
// repeated 1/1024 quantization of flattened command transforms.
struct ScriptTaskFrameGroup {
    std::uint32_t parent = kScriptTaskNoParentClip;
    std::uint32_t command_begin = 0;
    std::uint32_t command_count = 0;
    Rect bounds;
    Rect clip_rect;
    int clip_border_radius = 0;
    LayerType type = LayerType::Paint;
    std::uint8_t clip_axes = 3;
    bool has_clip = false;
    float opacity = 1.0F;
    Transform2D transform;
    int origin_x_percent = 50;
    int origin_y_percent = 50;
    bool has_affine = false;
    std::array<float, 6> affine{{1, 0, 0, 1, 0, 0}};
    bool has_clip_ellipse = false;
    std::array<int, 8> clip_ellipse{};
    // Canonical ABI4 metadata, transported only by explicitly selected v9.
    bool has_transform = false;
    std::uint32_t reasons = 0;
    int scroll_y = 0;
    int max_scroll_y = 0;
    int z_index = 0;
    std::uint64_t source_order = 0;
};

struct ScriptTaskFrameEllipse {
    std::uint32_t command_index = 0;
    std::array<int, 8> radii{};
};

struct ScriptTaskAppFrame {
    // v9 image handle namespace. Zero for image-free or legacy frames.
    std::uint64_t image_provider_id = 0;
    Rect viewport;
    std::vector<ScriptTaskFrameClip> clips;
    DisplayList display_list;
    // Empty means that every display command has no frame clip. Otherwise this
    // vector is parallel to display_list and contains bounded clip indices.
    std::vector<std::uint16_t> display_clip_indices;
    std::vector<ScriptTaskInputTarget> input_targets;
    // Set by flattening when a source clip reference cannot be represented by
    // the value-frame wire format. It prevents the reference from silently
    // becoming kScriptTaskNoClip.
    bool clip_metadata_overflow = false;
    // Nonempty only for explicitly selected v6 group frames. There is one
    // source command list, not a second flattened copy.
    std::vector<ScriptTaskFrameGroup> groups;
    std::vector<ScriptTaskFrameEllipse> command_ellipses;
};

struct ScriptTaskAppFrameCodecOptions {
    std::size_t max_commands = 0;
    std::size_t max_text_bytes = 0;
    std::size_t max_input_targets = 0;
    std::size_t max_payload_bytes = 0;
    std::uint8_t version = 1;
    // Producer-owned namespace used during v9 group capture only.
    std::uint64_t image_provider_id = 0;
    std::size_t max_clips = 0;
    std::size_t max_clip_depth = 8;
    std::size_t max_groups = 0;
    std::size_t max_group_depth = 32;
};

enum class ScriptTaskAppFrameCodecStatus {
    Accepted,
    TooManyCommands,
    TooManyTextBytes,
    TooManyInputTargets,
    TooManyClips,
    PayloadTooLarge,
    InvalidValue,
    UnsupportedVersion,
    UnsupportedClipFeature,
    UnsupportedTransformFeature,
    TooDeepClipChain,
    InvalidClip,
    Malformed,
    UnsupportedSamplingFeature,
    UnsupportedGroupFeature,
    TooManyGroups,
    InvalidGroup,
    UnsupportedTextFeature,
};

// Versioned, value-only wire format. v1 carries only the legacy display list;
// v2 additionally carries bounded clip records and references; v3 carries
// fixed-point command transforms; v4 also identifies a transformed layer's
// source-space clip; v5 carries independent per-command sampling geometry;
// v6 additionally carries preorder paint groups and sparse ellipse sidecars.
// Image handles and font-family hashes are
// opaque integers; no DisplayCommand storage or text pointer crosses tasks.
// v7 uses the v6 group records with canonical JFTPv3 text only, and requires
// group frames. It rejects legacy text/JFGR and does not downgrade to v6.
// v9 is an independent canonical ABI4 typed-command family: JFTP1 and explicit
// text origin/baseline, image/gradient relative clips, canonical layer metadata.
// Its header/group/command records are44/112/128 bytes. It does not enable or
// redefine the ABI9-only v5-v8 prerequisites. Both peers must explicitly opt in.
// Configure the same explicitly selected version on both peers. v5 is transport
// support only; a painter/renderer still has to support its sampling semantics.
bool script_task_app_frame_codec_supports_version(std::uint8_t version);

// Bounded capture publishes output only after all values and sidecars have
// been copied. No LayoutBox, DOM, provider or source-tree pointer is retained.
ScriptTaskAppFrameCodecStatus make_script_task_group_frame(
    const LayerNode& root, Rect viewport,
    const ScriptTaskAppFrameCodecOptions& limits, ScriptTaskAppFrame& output);

ScriptTaskAppFrameCodecStatus encode_script_task_app_frame(
    const ScriptTaskAppFrame& frame,
    const ScriptTaskAppFrameCodecOptions& options,
    std::vector<std::uint8_t>& output);
ScriptTaskAppFrameCodecStatus decode_script_task_app_frame(
    const std::vector<std::uint8_t>& input,
    const ScriptTaskAppFrameCodecOptions& options,
    ScriptTaskAppFrame& output);

// The frame carries hit regions in paint order. Reverse lookup selects the
// visually topmost enabled target and returns only its opaque worker key.
std::uint32_t resolve_script_task_input_target(const ScriptTaskAppFrame& frame, int x, int y);

// Runs only in the script worker while its LayerNode and DOM remain private.
// Flattening applies layer clips, opacity and final device-space transforms
// before the copied DisplayList enters the value frame. Input targets are
// optional hints; raw input remains authoritative for the worker's private
// InputController.
ScriptTaskAppFrame make_script_task_app_frame(const LayerNode& layer_tree,
                                              Rect viewport,
                                              std::vector<ScriptTaskInputTarget> input_targets = {},
                                              bool include_clip_metadata = false);

struct ScriptTaskAppFramePublishResult {
    ScriptTaskAppFrameCodecStatus codec_status = ScriptTaskAppFrameCodecStatus::InvalidValue;
    ScriptTaskFramePublishResult lease;

    bool accepted() const {
        return codec_status == ScriptTaskAppFrameCodecStatus::Accepted && lease.accepted();
    }
};

// Worker-owned encoder scratch. Its buffer is pre-reserved from the declared
// frame budget, so ordinary frame publication does not grow it.
class ScriptTaskAppFramePublisher {
public:
    explicit ScriptTaskAppFramePublisher(ScriptTaskAppFrameCodecOptions options);

    ScriptTaskAppFramePublishResult publish(ScriptTaskSupervisor& supervisor,
                                            const ScriptAppSession& session,
                                            const ScriptTaskAppFrame& frame);

private:
    ScriptTaskAppFrameCodecOptions options_;
    std::vector<std::uint8_t> encoded_;
};

enum class ScriptTaskAppFrameTakeStatus {
    NoFrame,
    UnexpectedPacket,
    LeaseRejected,
    DecodeRejected,
    Accepted,
    TextCatalogRejected,
    ImageProviderRejected,
    ResourceRejected,
};

// UI-task helper. It copies a sealed lease before decoding and always releases
// the lease after a successful copy, even when the value frame is malformed.
ScriptTaskAppFrameTakeStatus take_script_task_app_frame(ScriptTaskSupervisor& supervisor,
                                                        const ScriptAppSession& session,
                                                        const ScriptTaskAppFrameCodecOptions& options,
                                                        ScriptTaskAppFrame& output,
                                                        std::uint32_t* accepted_packet_sequence = nullptr);

#if JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
#if JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API
class AppRuntimeHost;
// Host-task-only start, capturing the current app's effective immutable catalog.
ScriptAppSession begin_script_task_app_session(const AppRuntimeHost&, ScriptTaskSupervisor&);
#endif

// Ownership admission only; glyph semantics/geometry remain checked by Core.
bool script_task_frame_uses_image_provider(const ScriptTaskAppFrame&, const RuntimeImageProvider::Handle&);
bool script_task_frame_uses_catalog(const ScriptTaskAppFrame&, const RuntimeFontCatalog&);

// UI-side accepted value frame plus its exact session resource owner. This
// envelope is never serialized. Copies intentionally retain immutable fonts.
class ScriptTaskAcceptedAppFrame {
public:
    const ScriptTaskAppFrame& frame() const noexcept { return frame_; }
    ScriptAppSession session() const noexcept { return session_; }
    std::uint32_t sequence() const noexcept { return sequence_; }
    const RuntimeFontCatalog::Handle& text_catalog() const noexcept { return catalog_; }
    const RuntimeImageProvider::Handle& image_provider() const noexcept { return images_; }
    bool valid() const noexcept { return session_.valid() && catalog_ && !frame_.groups.empty(); }
private:
    friend ScriptTaskAppFrameTakeStatus take_script_task_app_frame(
        ScriptTaskSupervisor&, const ScriptAppSession&, const ScriptTaskAppFrameCodecOptions&,
        ScriptTaskAcceptedAppFrame&);
    ScriptTaskAppFrame frame_;
    ScriptAppSession session_;
    std::uint32_t sequence_ = 0;
    RuntimeFontCatalog::Handle catalog_;
    RuntimeImageProvider::Handle images_;
};

// v7/JFTP3 or independently negotiated v9/JFTP1. Captures the supervisor's
// bound catalog before taking a packet.
// Refusal preserves the prior accepted envelope and its resource ownership.
ScriptTaskAppFrameTakeStatus take_script_task_app_frame(
    ScriptTaskSupervisor&, const ScriptAppSession&, const ScriptTaskAppFrameCodecOptions&,
    ScriptTaskAcceptedAppFrame&);
#endif

} // namespace jellyframe
