#pragma once

#include "core/dom.h"

#include <string>
#include <string_view>

namespace jellyframe {

enum class FormControlKind {
    None,
    Button,
    Text,
    TextArea,
    Checkbox,
    Radio,
    Range,
    Select,
    Date,
    Time,
    Color,
    File,
};

struct FormControlState {
    FormControlKind kind = FormControlKind::None;
    std::string value;
    bool checked = false;
    int selected_index = -1;
    int min = 0;
    int max = 100;
    int step = 1;
    bool dirty = false;
};

FormControlKind form_control_kind(const Node& node);
bool is_form_control(const Node& node);
bool is_disabled_form_control(const Node& node);
bool is_text_entry_control(const Node& node);
FormControlState& ensure_form_control_state(const Node& node);
const FormControlState* form_control_state_if_created(const Node& node);
std::string form_control_display_text(const Node& node);
bool append_text_to_control(Node& node, std::string_view text);
bool backspace_control(Node& node);
bool complete_text_control_from_datalist(Node& node);
bool activate_form_control(Node& node);
bool set_range_value_from_local_x(Node& node, int local_x, int width);
std::string form_control_value(const Node& node);
bool set_form_control_value(Node& node, std::string value);
bool form_control_checked(const Node& node);
bool set_form_control_checked(Node& node, bool checked);
int form_control_selected_index(const Node& node);
bool set_form_control_selected_index(Node& node, int selected_index);
bool step_select_control(Node& node, int delta);

} // namespace jellyframe
