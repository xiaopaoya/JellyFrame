#include <iostream>

int script_task_contract_tests_main();
int script_task_frame_codec_tests_main();
int script_task_input_codec_tests_main();
int script_task_service_bridge_tests_main();

int main() {
    int failed = 0;
    failed += script_task_contract_tests_main();
    failed += script_task_frame_codec_tests_main();
    failed += script_task_input_codec_tests_main();
    failed += script_task_service_bridge_tests_main();
    if (failed == 0) {
        std::cout << "script task runtime tests passed\n";
    }
    return failed == 0 ? 0 : 1;
}
