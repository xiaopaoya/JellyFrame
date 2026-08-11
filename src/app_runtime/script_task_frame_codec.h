#pragma once

#include "app_runtime/script_task_contract.h"
#include "render_core/geometry.h"
#include "render_core/layer_tree.h"

#include <cstddef>
#include <cstdint>
#include <vector>

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

struct ScriptTaskAppFrame {
    Rect viewport;
    std::vector<ScriptTaskFrameClip> clips;
    DisplayList display_list;
    // Empty means that every display command has no frame clip. Otherwise this
    // vector is parallel to display_list and contains bounded clip indices.
    std::vector<std::uint16_t> display_clip_indices;
    std::vector<ScriptTaskInputTarget> input_targets;
};

struct ScriptTaskAppFrameCodecOptions {
    std::size_t max_commands = 0;
    std::size_t max_text_bytes = 0;
    std::size_t max_input_targets = 0;
    std::size_t max_payload_bytes = 0;
    std::uint8_t version = 1;
    std::size_t max_clips = 0;
    std::size_t max_clip_depth = 8;
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
    TooDeepClipChain,
    InvalidClip,
    Malformed,
};

// Versioned, value-only wire format. v1 carries only the legacy display list;
// v2 additionally carries bounded clip records and references. Image handles
// and font-family hashes are opaque integers; no DisplayCommand storage or text
// pointer crosses tasks.
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
// Flattening applies layer clips, transforms and opacity before the copied
// DisplayList enters the value frame. Input targets are optional hints; raw
// input remains authoritative for the worker's private InputController.
ScriptTaskAppFrame make_script_task_app_frame(const LayerNode& layer_tree,
                                              Rect viewport,
                                              std::vector<ScriptTaskInputTarget> input_targets = {});

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
};

// UI-task helper. It copies a sealed lease before decoding and always releases
// the lease after a successful copy, even when the value frame is malformed.
ScriptTaskAppFrameTakeStatus take_script_task_app_frame(ScriptTaskSupervisor& supervisor,
                                                        const ScriptAppSession& session,
                                                        const ScriptTaskAppFrameCodecOptions& options,
                                                        ScriptTaskAppFrame& output,
                                                        std::uint32_t* accepted_packet_sequence = nullptr);

} // namespace jellyframe
