/* Host-Test: gcc -Wall -Wextra -fsanitize=address,undefined -Iinclude src/irm_pcm.c test/test_pcm.c -o /tmp/tp && /tmp/tp */
#include <assert.h>
#include <stdio.h>
#include "irm_pcm.h"

int main(void)
{
    assert(irm_pcm_gain_from_percent(100) == IRM_PCM_UNITY);
    assert(irm_pcm_gain_from_percent(0) == 0);
    assert(irm_pcm_gain_from_percent(200) == IRM_PCM_UNITY);          /* geklemmt */
    assert(irm_pcm_gain_from_percent(50) == IRM_PCM_UNITY / 4);       /* quadratisch: -12 dB */
    for (int p = 1; p <= 100; p++) assert(irm_pcm_gain_from_percent((uint8_t)p) >= irm_pcm_gain_from_percent((uint8_t)(p - 1)));

    int16_t in[4] = { 32767, -32768, 1000, -1000 }, out[4];
    irm_pcm_scale(out, in, 4, IRM_PCM_UNITY);
    for (int i = 0; i < 4; i++) assert(out[i] == in[i]);               /* Einheitsverstaerkung verlustfrei */
    irm_pcm_scale(out, in, 4, IRM_PCM_UNITY / 2);
    assert(out[0] == 16383 && out[1] == -16384);
    irm_pcm_scale(out, in, 4, 0);
    for (int i = 0; i < 4; i++) assert(out[i] == 0 || out[i] == -1);

    int16_t st[4] = { 1000, 3000, -32768, -32768 }, mono[2];
    irm_pcm_downmix_mono(mono, st, 2, IRM_PCM_UNITY);
    assert(mono[0] == 2000 && mono[1] == -32768);

    /* u8: Stille -> Mitte 128, Vollaussteuerung -> 0/255, Mittelwert bei leisem Signal bleibt ~128 (Dither) */
    int16_t z[1000] = { 0 }; uint8_t u[1000]; uint32_t st8 = 1;
    irm_pcm_to_u8(u, z, 1000, &st8);
    long sum = 0; for (int i = 0; i < 1000; i++) { assert(u[i] >= 127 && u[i] <= 129); sum += u[i]; }
    assert(sum / 1000 >= 127 && sum / 1000 <= 128);
    int16_t ext[2] = { 32767, -32768 }; uint8_t ue[2];
    irm_pcm_to_u8(ue, ext, 2, &st8);
    assert(ue[0] >= 254 && ue[1] <= 1);
    puts("irm_pcm: alle Tests ok");
    return 0;
}
