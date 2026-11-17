#pragma once
#include "../../../../../irm_kiwi/test/host/shim/freertos/stream_buffer.h"
typedef struct { char opaque[64]; } StaticStreamBuffer_t;
#define xStreamBufferCreateStatic(sz, trig, store, ctrl) ((void)(store), (void)(ctrl), xStreamBufferCreate((sz), (trig)))
static inline unsigned uxTaskGetStackHighWaterMark(void *h) { (void)h; return 0; }
