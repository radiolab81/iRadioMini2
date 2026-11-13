#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg, int prio, TaskHandle_t *h, int core);
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *arg, int prio, TaskHandle_t *h);
void vTaskDelete(TaskHandle_t t);
void vTaskDelay(TickType_t ticks);
