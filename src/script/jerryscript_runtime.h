#pragma once

#include <string>
#include <string_view>

namespace wearweb {

struct ScriptEvaluationResult {
    bool ok = false;
    std::string value;
    std::string error;
};

class JerryScriptRuntime {
public:
    JerryScriptRuntime();
    ~JerryScriptRuntime();

    JerryScriptRuntime(const JerryScriptRuntime&) = delete;
    JerryScriptRuntime& operator=(const JerryScriptRuntime&) = delete;

    ScriptEvaluationResult eval(std::string_view source, std::string_view source_name = {});

private:
    bool initialized_ = false;
};

} // namespace wearweb
