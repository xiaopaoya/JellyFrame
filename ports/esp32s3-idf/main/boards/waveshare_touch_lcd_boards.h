#pragma once

#include "jellyframe_esp32s3_hal.h"

#include <cstdint>

namespace jellyframe_esp32s3 {

class BoardInputQueue;

namespace boards {

enum class BoardId : std::uint8_t {
    GenericQemu = 0,
    WaveshareEsp32s3TouchLcd147,
};

struct DisplayProfile {
    int width = 0;
    int height = 0;
    const char* controller = "";
    const char* bus = "";
    bool has_touch = false;
    const char* touch_controller = "";
};

struct BoardProfile {
    BoardId id = BoardId::GenericQemu;
    const char* name = "";
    DisplayProfile display;
    const char* notes = "";
};

struct BoardRuntime {
    BoardProfile profile;
    bool hardware_display_ready = false;
    const char* hardware_status = "";
    Rgb565PackedRectFlushCallback packed_flush = nullptr;
    void* flush_context = nullptr;
};

const BoardProfile& selected_board_profile();
BoardRuntime initialize_selected_board();
void release_board_runtime(BoardRuntime& runtime);
void attach_input_queue(BoardRuntime& runtime, BoardInputQueue* queue);
bool selected_board_probe_only_enabled();
void run_selected_board_probe_only();

} // namespace boards
} // namespace jellyframe_esp32s3
