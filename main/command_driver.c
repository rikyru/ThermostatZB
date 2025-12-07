#include "esp_log.h"
#include "driver/gpio.h"
#include "command_driver.h"
#include "main.h"


void command_driver_init() {

    // Configura il PIN CALDAIA
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CMD_CALDAIA_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);
    gpio_config_t io_conf2 = {
        .pin_bit_mask = (1ULL << CMD_TABLET_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf2);

    gpio_set_level(CMD_CALDAIA_GPIO, 0); // always off at startup
    gpio_set_level(CMD_TABLET_GPIO, 1); // always on at startup
}

