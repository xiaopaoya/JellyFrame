#include "jerryscript-port.h"
#include "esp_heap_caps.h"
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static jmp_buf fatal_boundary;
static int fatal_code;
void test_record_fatal(int code) { fatal_code = code; }
static unsigned allocation_calls;
static unsigned fail_attempts;
static unsigned release_calls;
static unsigned requested_caps[2];
static size_t requested_size;
static _Alignas(8) unsigned char allocation[270336];

void *heap_caps_aligned_alloc(size_t alignment, size_t size, unsigned caps) {
    assert(alignment == 8 && size % alignment == 0);
    assert(allocation_calls < 2);
    requested_caps[allocation_calls++] = caps;
    requested_size = size;
    return allocation_calls <= fail_attempts ? NULL : allocation;
}
void heap_caps_free(void *pointer) {
    assert(pointer == allocation);
    ++release_calls;
}
void vTaskSuspend(void *task) { (void)task; longjmp(fatal_boundary, 1); }
void vTaskDelay(unsigned delay) { (void)delay; abort(); }
int64_t esp_timer_get_time(void) { return 0; }

static void reset(unsigned failures) {
    fatal_code = -1;
    allocation_calls = release_calls = 0;
    fail_attempts = failures;
}
static void expect_fatal(size_t context_size, unsigned failures, unsigned calls) {
    reset(failures);
    if (setjmp(fatal_boundary) == 0) {
        (void)jerry_port_context_alloc(context_size);
        abort();
    }
    assert(fatal_code == JERRY_FATAL_OUT_OF_MEMORY);
    assert(allocation_calls == calls);
    assert(jerry_port_context_get() == NULL);
}
int main(void) {
    for (unsigned fallback = 0; fallback != 2; ++fallback) {
        reset(fallback);
        size_t total = jerry_port_context_alloc(2269);
        assert(total == 2272 + 256 * 1024 && total == requested_size);
        assert(allocation_calls == fallback + 1);
        assert(requested_caps[0] == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (fallback) assert(requested_caps[1] == MALLOC_CAP_8BIT);
        assert((uintptr_t)jerry_port_context_get() % 8 == 0);
        assert((uintptr_t)((unsigned char *)jerry_port_context_get() + 2272) % 8 == 0);
        jerry_port_context_free();
        assert(release_calls == 1 && jerry_port_context_get() == NULL);
    }
    expect_fatal(2269, 2, 2);
    expect_fatal(SIZE_MAX, 0, 0);
    expect_fatal(SIZE_MAX - 7, 0, 0);
    puts("context allocation tests passed");
    return 0;
}
