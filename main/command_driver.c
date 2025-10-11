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

    gpio_set_level(CMD_CALDAIA_GPIO, 0); // always off at startup
}

