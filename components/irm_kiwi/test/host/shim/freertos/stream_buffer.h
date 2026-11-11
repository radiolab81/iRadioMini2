#pragma once
#include "FreeRTOS.h"
typedef void *StreamBufferHandle_t;
StreamBufferHandle_t xStreamBufferCreate(size_t size, size_t trigger);
void vStreamBufferDelete(StreamBufferHandle_t s);
size_t xStreamBufferSend(StreamBufferHandle_t s, const void *d, size_t n, TickType_t t);
size_t xStreamBufferReceive(StreamBufferHandle_t s, void *d, size_t n, TickType_t t);
size_t xStreamBufferBytesAvailable(StreamBufferHandle_t s);
