#pragma once
#include <stddef.h>
#include <stdint.h>
#include "sdkconfig.h"   /* wie im echten IDF: FreeRTOS.h zieht die Konfiguration mit */
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef uint32_t EventBits_t;
#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))   /* 1 Tick = 1 ms */
#define tskNO_AFFINITY 0x7FFFFFFF
#define BIT0 (1u << 0)
