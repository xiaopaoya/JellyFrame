"""Compile the real ESP port with host stubs and exercise its fatal boundary.

Run with a GCC-compatible host compiler (for example clang) on PATH, or pass
--cc. On Windows run from a Visual Studio developer shell when using clang.
Temporary compiler outputs stay in the system temporary directory.
"""

import argparse
from pathlib import Path
import subprocess
import tempfile


STUBS = {
    "jerryscript-port.h": r"""
#include <stddef.h>
#include <stdint.h>
typedef struct { int unused; } jerry_context_t;
typedef unsigned char jerry_char_t;
typedef size_t jerry_size_t;
typedef enum { JERRY_FATAL_OUT_OF_MEMORY = 10 } jerry_fatal_code_t;
#define JERRY_GLOBAL_HEAP_SIZE 256
""",
    "freertos/FreeRTOS.h": r"""
#pragma once
#include <stdint.h>
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(value) (value)
""",
    "freertos/task.h": r"""
#pragma once
typedef void *TaskHandle_t;
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void vTaskSuspend(TaskHandle_t task);
void vTaskDelay(unsigned int ticks);
""",
    "esp_attr.h": "#define DRAM_ATTR\n",
    "esp_cpu.h": r"""
#include <stdbool.h>
#include <stdint.h>
bool esp_cpu_compare_and_set(volatile uint32_t *, uint32_t, uint32_t);
""",
    "esp_heap_caps.h": r"""
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_aligned_alloc(size_t, size_t, unsigned int);
void heap_caps_free(void *);
""",
    "esp_log.h": r"""
void test_fatal_log(void);
#define ESP_LOGE(...) test_fatal_log()
#define ESP_LOGI(...) ((void) 0)
""",
    "esp_timer.h": r"""
#include <stdint.h>
int64_t esp_timer_get_time(void);
""",
}

HARNESS = r"""
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include "jellyframe_jerry_port_fatal.h"
#include "jerryscript-port.h"

void jerry_port_fatal(jerry_fatal_code_t);
size_t jerry_port_context_alloc(size_t);
void jerry_port_context_free(void);
void jerry_port_init(void);

static jmp_buf suspended;
static TaskHandle_t current_task;
static TaskHandle_t first_owner;
static int first_code;
static int allocations;
static int logs;
static int captures;

static void check_receipt(void) {
    TaskHandle_t owner = NULL;
    int code = -1;
    assert(jellyframe_jerry_port_try_get_fatal(&owner, &code));
    assert(owner == first_owner);
    assert(code == first_code);
    assert(jellyframe_jerry_port_try_get_fatal(NULL, NULL));
}

bool esp_cpu_compare_and_set(volatile uint32_t *address, uint32_t expected,
                             uint32_t desired) {
    return __atomic_compare_exchange_n(address, &expected, desired, false,
                                       __ATOMIC_RELAXED, __ATOMIC_RELAXED);
}

TaskHandle_t xTaskGetCurrentTaskHandle(void) {
    // The winner has claimed WRITING but has yet to publish its payload.
    TaskHandle_t owner = (TaskHandle_t) (uintptr_t) 99;
    int code = -99;
    assert(!jellyframe_jerry_port_try_get_fatal(&owner, &code));
    assert(owner == (TaskHandle_t) (uintptr_t) 99 && code == -99);
    ++captures;
    return current_task;
}

void test_fatal_log(void) { check_receipt(); ++logs; }
void vTaskSuspend(TaskHandle_t task) {
    assert(task == NULL);
    check_receipt();
    longjmp(suspended, 1);
}
void vTaskDelay(unsigned int ticks) { (void) ticks; abort(); }
void *heap_caps_aligned_alloc(size_t alignment, size_t size, unsigned int caps) {
    (void) alignment; (void) size; (void) caps;
    ++allocations;
    return NULL;
}
void heap_caps_free(void *ptr) { assert(ptr == NULL); }
int64_t esp_timer_get_time(void) { return 0; }

int main(int argc, char **argv) {
    assert(argc == 2);
    first_owner = current_task = (TaskHandle_t) (uintptr_t) 1;
    first_code = argv[1][0] == 'd' ? 73 : JERRY_FATAL_OUT_OF_MEMORY;
    assert(!jellyframe_jerry_port_try_get_fatal(NULL, NULL));
    if (setjmp(suspended) == 0) {
        if (argv[1][0] == 'd') jerry_port_fatal((jerry_fatal_code_t) first_code);
        else jerry_port_context_alloc(argv[1][0] == 'o' ? SIZE_MAX : 64);
        abort();
    }
    assert(allocations == (argv[1][0] == 'a' ? 2 : 0));
    assert(logs == 1 && captures == 1);
    jerry_port_context_free();
    jerry_port_init();
    check_receipt();
    current_task = (TaskHandle_t) (uintptr_t) 2;
    if (setjmp(suspended) == 0) {
        jerry_port_fatal((jerry_fatal_code_t) 91);
        abort();
    }
    assert(logs == 2 && captures == 1);
    check_receipt();
    return 0;
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang")
    args = parser.parse_args()
    component = Path(__file__).resolve().parents[1] / "components/jellyframe_jerryscript"
    with tempfile.TemporaryDirectory(prefix="jerry-port-fatal-") as temporary:
        root = Path(temporary)
        for name, content in STUBS.items():
            destination = root / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(content, encoding="utf-8")
        harness = root / "harness.c"
        harness.write_text(HARNESS, encoding="utf-8")
        executable = root / "fatal-test.exe"
        subprocess.run([
            args.cc, "-std=c17", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-parameter", "-Wno-unused-variable",
            "-Wno-c2x-extensions", "-I", str(root), "-I", str(component),
            str(component / "jerry_port_esp32s3.c"), str(harness),
            "-o", str(executable),
        ], check=True)
        for scenario in ("direct", "allocation-failure", "overflow"):
            subprocess.run([str(executable), scenario], check=True)
            print(f"PASS {scenario}")


if __name__ == "__main__":
    main()
