/* Flash-Partition "kiwidb" als kiwi_dir_store_t (zwei Slots A/B). Auf dem Host im Test durch einen RAM-Store ersetzt. */
#pragma once
#include "kiwi_dir_store.h"
const kiwi_dir_store_t *irm_dir_store_get(void);   /* NULL, wenn die Partition fehlt */
