#include "script/jerryscript_runtime.h"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace wearweb;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expression_returns_value() {
    JerryScriptRuntime runtime;
    const ScriptEvaluationResult result = runtime.eval("1 + 2", "expression.js");

    check(result.ok, "expression evaluates successfully");
    check(result.value == "3", "expression result is stringified");
}

void exception_returns_error_text() {
    JerryScriptRuntime runtime;
    const ScriptEvaluationResult result = runtime.eval("throw new Error('boom')", "exception.js");

    check(!result.ok, "exception is reported as failure");
    check(!result.error.empty(), "exception has error text");
}

void runtime_can_restart() {
    for (int i = 0; i < 3; ++i) {
        JerryScriptRuntime runtime;
        const ScriptEvaluationResult result = runtime.eval("'run-' + " + std::to_string(i));
        check(result.ok, "runtime restart eval succeeds");
    }
}

} // namespace

int main() {
    try {
        expression_returns_value();
        exception_returns_error_text();
        runtime_can_restart();
    } catch (const std::exception& error) {
        std::cerr << "script runtime test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "script runtime tests passed\n";
    return 0;
}
