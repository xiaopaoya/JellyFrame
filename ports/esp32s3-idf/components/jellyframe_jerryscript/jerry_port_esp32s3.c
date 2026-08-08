#include "jerryscript-port.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>

static const char *const jellyframe_jerry_tag = "JellyFrameJerry";
static jerry_context_t *jellyframe_jerry_context = NULL;

void jerry_port_init(void) {}

void jerry_port_fatal(jerry_fatal_code_t code) {
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
    const size_t total = context_size + (size_t) JERRY_GLOBAL_HEAP_SIZE * 1024U;
    jellyframe_jerry_context = (jerry_context_t *) heap_caps_malloc(
        total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (jellyframe_jerry_context == NULL) {
        jellyframe_jerry_context = (jerry_context_t *) heap_caps_malloc(total, MALLOC_CAP_8BIT);
    }
    return jellyframe_jerry_context == NULL ? 0 : total;
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
