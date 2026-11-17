#pragma once
typedef struct { unsigned long long pin_bit_mask; int mode; } gpio_config_t;
#define GPIO_MODE_OUTPUT 1
static inline int gpio_config(const gpio_config_t *c) { (void)c; return 0; }
static inline int gpio_set_level(int p, int l) { (void)p; (void)l; return 0; }
