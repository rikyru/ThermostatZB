#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void light_driver_init(bool power);
void light_driver_set_power(bool power);
void light_driver_set_rgb(uint8_t r, uint8_t g, uint8_t b);
#define LIGHT_DEFAULT_ON  1
#define LIGHT_DEFAULT_OFF 0

#ifdef __cplusplus
}
#endif
