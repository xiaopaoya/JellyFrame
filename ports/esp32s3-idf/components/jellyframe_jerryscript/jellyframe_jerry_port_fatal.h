#ifndef JELLYFRAME_JERRY_PORT_FATAL_H
#define JELLYFRAME_JERRY_PORT_FATAL_H

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The first fatal receipt remains immutable until reboot. Returns false until
 * publication completes. Either output may be NULL. This function never waits. */
bool jellyframe_jerry_port_try_get_fatal(TaskHandle_t *owner, int *code);

#ifdef __cplusplus
}
#endif

#endif
