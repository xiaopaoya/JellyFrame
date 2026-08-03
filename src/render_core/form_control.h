#pragma once

#include "render_core/dom.h"
#include "render_core/feature_config.h"
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
#include "render_core/geometry.h"
#endif

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
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
    bool select_popup_open = false;
#endif
    std::string custom_validation_message;
    bool dirty = false;
};

FormControlKind form_control_kind(const Node& node);
bool is_form_control(const Node& node);
bool is_disabled_form_control(const Node& node);
bool is_text_entry_control(const Node& node);
bool is_readonly_text_control(const Node& node);
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

#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
bool select_popup_is_open(const Node& node);
bool set_select_popup_open(Node& node, bool open);
int form_control_option_count(const Node& node);
const Node* form_control_option_at(const Node& node, int option_index);
std::string form_control_option_text(const Node& node, int option_index);
bool form_control_option_disabled(const Node& node, int option_index);

struct SelectPopupGeometry {
    Rect rect;
    int row_height = 0;
    int first_option_index = 0;
    int visible_option_count = 0;
};

SelectPopupGeometry select_popup_geometry(const Rect& select_rect,
                                         const Rect& viewport,
                                         int option_count,
                                         int row_height);
#endif

} // namespace jellyframe
