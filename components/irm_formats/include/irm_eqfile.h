#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/** Liest n ganze Zahlen (eine je Zeile, '#' = Kommentar), begrenzt auf [lo,hi]. true wenn genau n gelesen. */
bool irm_eqfile_parse(const char *text, size_t len, int8_t *out, size_t n, int lo, int hi);
/** Schreibt n Werte zeilenweise in buf. Rueckgabe: Laenge oder 0 bei zu kleinem Puffer. */
size_t irm_eqfile_format(const int8_t *vals, size_t n, char *buf, size_t buf_sz);
#ifdef __cplusplus
}
#endif
