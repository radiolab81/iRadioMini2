#include "irm_out.h"
#include "irm_pcm.h"

void irm_out_set_volume(irm_out_sink_t *sink, uint8_t percent)
{
    if (sink) sink->gain_q15 = irm_pcm_gain_from_percent(percent);
}

uint32_t irm_out_dma_desc_num(uint32_t sample_rate, uint32_t buffer_ms, uint32_t dma_frame_num)
{
    uint32_t frames = (uint32_t)(((uint64_t)sample_rate * buffer_ms) / 1000u);
    uint32_t n = (frames + dma_frame_num - 1) / dma_frame_num;
    return n < 3 ? 3 : (n > 64 ? 64 : n);
}
