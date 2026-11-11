#pragma once

#include "script/script_runtime.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace jellyframe {

struct PendingScriptAudioEvent {
    std::uint32_t audio_id = 0;
    std::uint64_t due_ms = 0;
    ScriptAudioEventKind kind = ScriptAudioEventKind::Ended;
};

// Dispatch can append playback completions. Only the initial batch is eligible
// in this pump, and each event is consumed before running author JavaScript.
// The caller keeps the queue alive and processes session clear/rebuild afterward.
template<class Dispatch>
bool pump_pending_script_audio_events(std::vector<PendingScriptAudioEvent>& pending,
                                      std::uint64_t now_ms, Dispatch&& dispatch) {
    bool handled = false;
    const std::size_t initial_count = pending.size();
    for (std::size_t index = 0; index < initial_count; ++index) {
        const PendingScriptAudioEvent event = pending[index];
        if (event.audio_id == 0 || event.due_ms > now_ms) {
            continue;
        }
        pending[index].audio_id = 0;
        handled = dispatch(event.audio_id, event.kind) || handled;
    }
    pending.erase(std::remove_if(pending.begin(), pending.end(),
        [](const PendingScriptAudioEvent& event) { return event.audio_id == 0; }), pending.end());
    return handled;
}

} // namespace jellyframe
