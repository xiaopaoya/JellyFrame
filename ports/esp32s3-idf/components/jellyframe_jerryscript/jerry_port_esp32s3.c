#include "jerryscript-port.h"
#include "jellyframe_jerry_port_fatal.h"

#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <stdint.h>

// The pinned JerryScript engine uses JMEM_ALIGNMENT_LOG=3. Its external
// context starts the heap at the next 8-byte boundary.
#define JELLYFRAME_JERRY_ALIGNMENT 8U

static const char *const jellyframe_jerry_tag = "JellyFrameJerry";
static jerry_context_t *jellyframe_jerry_context = NULL;

enum { JELLYFRAME_FATAL_EMPTY, JELLYFRAME_FATAL_WRITING, JELLYFRAME_FATAL_READY };
// Internal RAM lets ESP-IDF's CAS use its bounded CPU instruction path.
static DRAM_ATTR uint32_t jellyframe_fatal_state;
static DRAM_ATTR TaskHandle_t jellyframe_fatal_owner;
static DRAM_ATTR int jellyframe_fatal_code;

bool jellyframe_jerry_port_try_get_fatal(TaskHandle_t *owner, int *code) {
    if (__atomic_load_n(&jellyframe_fatal_state, __ATOMIC_ACQUIRE) !=
        JELLYFRAME_FATAL_READY) {
        return false;
    }
    if (owner != NULL) *owner = jellyframe_fatal_owner;
    if (code != NULL) *code = jellyframe_fatal_code;
    return true;
}

void jerry_port_init(void) {}

void jerry_port_fatal(jerry_fatal_code_t code) {
    if (esp_cpu_compare_and_set(&jellyframe_fatal_state, JELLYFRAME_FATAL_EMPTY,
                                JELLYFRAME_FATAL_WRITING)) {
        jellyframe_fatal_owner = xTaskGetCurrentTaskHandle();
        jellyframe_fatal_code = (int) code;
        __atomic_store_n(&jellyframe_fatal_state, JELLYFRAME_FATAL_READY,
                         __ATOMIC_RELEASE);
    }
    ESP_LOGE(jellyframe_jerry_tag, "fatal code=%d; suspending script task", (int) code);
    vTaskSuspend(NULL);
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}

void jerry_port_sleep(uint32_t sleep_time) {
    vTaskDelay(pdMS_TO_TICKS(sleep_time));
}

size_t jerry_port_context_alloc(size_t context_size) {
    const size_t alignment_padding = JELLYFRAME_JERRY_ALIGNMENT - 1U;
    const size_t heap_size = (size_t) JERRY_GLOBAL_HEAP_SIZE * 1024U;
    if (context_size > SIZE_MAX - alignment_padding) {
        jerry_port_fatal(JERRY_FATAL_OUT_OF_MEMORY);
    }
    const size_t aligned_context_size =
        (context_size + alignment_padding) & ~alignment_padding;
    if (aligned_context_size > SIZE_MAX - heap_size) {
        jerry_port_fatal(JERRY_FATAL_OUT_OF_MEMORY);
    }
    const size_t total = aligned_context_size + heap_size;
    jellyframe_jerry_context = (jerry_context_t *) heap_caps_aligned_alloc(
        JELLYFRAME_JERRY_ALIGNMENT, total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (jellyframe_jerry_context == NULL) {
        jellyframe_jerry_context = (jerry_context_t *) heap_caps_aligned_alloc(
            JELLYFRAME_JERRY_ALIGNMENT, total, MALLOC_CAP_8BIT);
    }
    // jerry_init immediately clears the returned context and computes the heap
    // size. Allocation failure must enter the existing fatal boundary first.
    if (jellyframe_jerry_context == NULL) {
        jerry_port_fatal(JERRY_FATAL_OUT_OF_MEMORY);
    }
    return total;
}

jerry_context_t *jerry_port_context_get(void) { return jellyframe_jerry_context; }

void jerry_port_context_free(void) {
    heap_caps_free(jellyframe_jerry_context);
    jellyframe_jerry_context = NULL;
}

void jerry_port_log(const char *message_p) {
    ESP_LOGI(jellyframe_jerry_tag, "%s", message_p != NULL ? message_p : "");
}

void jerry_port_print_buffer(const jerry_char_t *buffer_p, jerry_size_t buffer_size) {
    if (buffer_p != NULL && buffer_size != 0) {
        ESP_LOGI(jellyframe_jerry_tag, "%.*s", (int) buffer_size, (const char *) buffer_p);
    }
}

jerry_char_t *jerry_port_line_read(jerry_size_t *out_size_p) {
    if (out_size_p != NULL) *out_size_p = 0;
    return NULL;
}

void jerry_port_line_free(jerry_char_t *buffer_p) { free(buffer_p); }
jerry_char_t *jerry_port_path_normalize(const jerry_char_t *, jerry_size_t) { return NULL; }
void jerry_port_path_free(jerry_char_t *path_p) { free(path_p); }
jerry_size_t jerry_port_path_base(const jerry_char_t *) { return 0; }

jerry_char_t *jerry_port_source_read(const char *, jerry_size_t *out_size_p) {
    if (out_size_p != NULL) *out_size_p = 0;
    return NULL;
}

void jerry_port_source_free(jerry_char_t *buffer_p) { free(buffer_p); }
int32_t jerry_port_local_tza(double) { return 0; }
double jerry_port_current_time(void) { return (double) esp_timer_get_time() / 1000.0; }
