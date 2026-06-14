#include "script/jerryscript_runtime.h"

#include <jerryscript.h>

#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace wearweb {
namespace {

bool g_runtime_active = false;

class JerryValue {
public:
    explicit JerryValue(jerry_value_t value)
        : value_(value) {}

    ~JerryValue() {
        if (owns_) {
            jerry_value_free(value_);
        }
    }

    JerryValue(const JerryValue&) = delete;
    JerryValue& operator=(const JerryValue&) = delete;

    JerryValue(JerryValue&& other) noexcept
        : value_(other.value_),
          owns_(other.owns_) {
        other.owns_ = false;
    }

    JerryValue& operator=(JerryValue&& other) noexcept {
        if (this != &other) {
            if (owns_) {
                jerry_value_free(value_);
            }
            value_ = other.value_;
            owns_ = other.owns_;
            other.owns_ = false;
        }
        return *this;
    }

    jerry_value_t get() const {
        return value_;
    }

    jerry_value_t release() {
        owns_ = false;
        return value_;
    }

private:
    jerry_value_t value_;
    bool owns_ = true;
};

std::string jerry_string_to_std_string(jerry_value_t value) {
    const jerry_size_t size = jerry_string_size(value, JERRY_ENCODING_UTF8);
    if (size == 0) {
        return {};
    }

    std::vector<jerry_char_t> buffer(size);
    const jerry_size_t written = jerry_string_to_buffer(value, JERRY_ENCODING_UTF8, buffer.data(), size);
    return std::string(reinterpret_cast<const char*>(buffer.data()), written);
}

std::string value_to_string(jerry_value_t value) {
    JerryValue string_value(jerry_value_to_string(value));
    if (jerry_value_is_exception(string_value.get())) {
        return "<unprintable JavaScript value>";
    }
    return jerry_string_to_std_string(string_value.get());
}

JerryValue evaluate_script(std::string_view source, std::string_view source_name) {
    const auto* bytes = reinterpret_cast<const jerry_char_t*>(source.data());
    if (source_name.empty()) {
        return JerryValue(jerry_eval(bytes, source.size(), JERRY_PARSE_NO_OPTS));
    }

    const std::string name(source_name);
    JerryValue name_value(jerry_string_sz(name.c_str()));

    jerry_parse_options_t options;
    std::memset(&options, 0, sizeof(options));
    options.options = JERRY_PARSE_HAS_SOURCE_NAME;
    options.source_name = name_value.get();

    JerryValue parsed(jerry_parse(bytes, source.size(), &options));
    if (jerry_value_is_exception(parsed.get())) {
        return parsed;
    }

    return JerryValue(jerry_run(parsed.get()));
}

} // namespace

JerryScriptRuntime::JerryScriptRuntime() {
    if (g_runtime_active) {
        throw std::runtime_error("only one JerryScriptRuntime can be active in this build");
    }

    jerry_init(JERRY_INIT_EMPTY);
    initialized_ = true;
    g_runtime_active = true;
}

JerryScriptRuntime::~JerryScriptRuntime() {
    if (initialized_) {
        jerry_cleanup();
        initialized_ = false;
        g_runtime_active = false;
    }
}

ScriptEvaluationResult JerryScriptRuntime::eval(std::string_view source, std::string_view source_name) {
    ScriptEvaluationResult output;

    JerryValue result = evaluate_script(source, source_name);
    if (jerry_value_is_exception(result.get())) {
        JerryValue exception_value(jerry_exception_value(result.release(), true));
        output.ok = false;
        output.error = value_to_string(exception_value.get());
        if (output.error.empty()) {
            output.error = "JavaScript exception";
        }
        return output;
    }

    output.ok = true;
    output.value = value_to_string(result.get());
    return output;
}

} // namespace wearweb
