#include <iostream>
#include <stdexcept>

int script_runtime_tests_main();

int main() {
    try {
        return script_runtime_tests_main();
    } catch (const std::exception& error) {
        std::cerr << "script runtime tests failed: " << error.what() << '\n';
        return 1;
    }
}
