#include <stdio.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BLINK_GPIO GPIO_NUM_5

void app_main(void) {
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

    bool led_on = false;
    while (1) {
        led_on = !led_on;
        printf("LED is %s\n", led_on ? "ON" : "OFF");
        gpio_set_level(BLINK_GPIO, led_on);
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}
