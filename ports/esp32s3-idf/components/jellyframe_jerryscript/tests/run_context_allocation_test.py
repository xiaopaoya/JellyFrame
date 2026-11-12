"""Compile the real ESP port against allocation/task stubs with Clang ASan."""
import argparse
import os
import pathlib
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--build-dir', required=True)
parser.add_argument('--jerry-root', required=True)
parser.add_argument('--compiler', default='clang')
parser.add_argument('--no-asan', action='store_true')
args = parser.parse_args()
build = pathlib.Path(args.build_dir)
build.mkdir(parents=True, exist_ok=True)
(build / 'freertos').mkdir(exist_ok=True)
headers = {
    'esp_heap_caps.h': '''#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_aligned_alloc(size_t, size_t, unsigned);
void heap_caps_free(void *);
''',
    'esp_log.h': 'void test_record_fatal(int);\n#define ESP_LOGE(tag, format, code) test_record_fatal(code)\n#define ESP_LOGI(...) ((void)0)\n',
    'esp_timer.h': '#include <stdint.h>\nint64_t esp_timer_get_time(void);\n',
    'freertos/FreeRTOS.h': '#define pdMS_TO_TICKS(x) (x)\n#define portMAX_DELAY 0xffffffff\n',
    'freertos/task.h': '#include <stddef.h>\nvoid vTaskSuspend(void *);\nvoid vTaskDelay(unsigned);\n',
}
for name, text in headers.items():
    (build / name).write_text(text)
source = pathlib.Path(__file__).parent
command = [args.compiler, '-g', '-O0', '-std=c2x',
           '-DJERRY_GLOBAL_HEAP_SIZE=256', '-I' + str(build),
           '-I' + str(pathlib.Path(args.jerry_root) / 'jerry-core/include'),
           str(source.parent / 'jerry_port_esp32s3.c'),
           str(source / 'context_allocation_test.c'), '-o', str(build / 'context_test.exe')]
if not args.no_asan:
    command.insert(1, '-fsanitize=address')
subprocess.run(command, check=True)
environment = os.environ.copy()
if os.name == 'nt' and not args.no_asan:
    # Windows loads Clang's shared ASan runtime from PATH.
    runtime_directory = subprocess.check_output(
        [args.compiler, '--print-runtime-dir'], text=True).strip()
    resource_directory = subprocess.check_output(
        [args.compiler, '--print-resource-dir'], text=True).strip()
    runtime_paths = [pathlib.Path(runtime_directory),
                     pathlib.Path(resource_directory) / 'lib' / 'windows']
    environment['PATH'] = os.pathsep.join(str(path) for path in runtime_paths) + os.pathsep + environment.get('PATH', '')
subprocess.run([str(build / 'context_test.exe')], check=True, env=environment)
