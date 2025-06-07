#include "esp_log.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "command_driver.h"
#include "thermostat.h"

static led_strip_handle_t s_led_strip;

void light_driver_init(bool power) {
    // Configura il LED RGB
    gpio_set_level(RGB_LED_EN_GPIO, 1); // Abilita il LED RGB
    led_strip_config_t led_strip_conf = {
        .max_leds = 1,
        .strip_gpio_num = RGB_LED_DATA_GPIO,
    };
    led_strip_rmt_config_t rmt_conf = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };
    led_strip_new_rmt_device(&led_strip_conf, &rmt_conf, &s_led_strip);

    // Configura il LED blu
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BLUE_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);

    light_driver_set_power(power);
}

void light_driver_set_power(bool power) {
    gpio_set_level(BLUE_LED_GPIO, power);
}

void light_driver_set_rgb(uint8_t r, uint8_t g, uint8_t b) {
    gpio_set_level(RGB_LED_EN_GPIO, 1); // Abilita il LED RGB
    led_strip_set_pixel(s_led_strip, 0, r, g, b);
    led_strip_refresh(s_led_strip);
}
