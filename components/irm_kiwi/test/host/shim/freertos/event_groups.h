#pragma once
#include "FreeRTOS.h"
typedef void *EventGroupHandle_t;
EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupSetBits(EventGroupHandle_t e, EventBits_t b);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t e, EventBits_t b, BaseType_t clear, BaseType_t all, TickType_t t);
