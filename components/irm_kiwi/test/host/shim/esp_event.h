#pragma once
#include "esp_err.h"
typedef const char *esp_event_base_t;
typedef void (*esp_event_handler_t)(void *arg, esp_event_base_t base, int id, void *data);
#define ESP_EVENT_DECLARE_BASE(x) extern esp_event_base_t x
