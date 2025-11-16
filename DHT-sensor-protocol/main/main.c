#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"
#include "stdio.h"

#define SENSOR_PIN GPIO_NUM_14

static portMUX_TYPE gpio_mux = portMUX_INITIALIZER_UNLOCKED;

int wait_for_level(gpio_num_t pin, int level, int timeout_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(pin) != level)
    {
        if ((int)(esp_timer_get_time() - start) > timeout_us)
            return -1;
    }
    return 0;
}

int measure_high_pulse(gpio_num_t pin, int timeout_us)
{
    if (wait_for_level(pin, 1, timeout_us) < 0)
        return -1;
    int64_t t0 = esp_timer_get_time();
    if (wait_for_level(pin, 0, timeout_us) < 0)
        return -1;
    return (int)(esp_timer_get_time() - t0);
}

void measure_temp_and_humidity()
{
    gpio_reset_pin(SENSOR_PIN);
    gpio_set_direction(SENSOR_PIN, GPIO_MODE_OUTPUT);

    // pocni komunikaciju
    gpio_set_level(SENSOR_PIN, 0);
    ets_delay_us(20000);

    // cekaj na odgovor senzora
    gpio_set_level(SENSOR_PIN, 1);
    ets_delay_us(30);
    gpio_set_direction(SENSOR_PIN, GPIO_MODE_INPUT);

    if (wait_for_level(SENSOR_PIN, 0, 1000) < 0)
    {
        printf("No response (wait LOW)\n");
        return;
    }
    if (wait_for_level(SENSOR_PIN, 1, 1000) < 0)
    {
        printf("No response (wait HIGH)\n");
        return;
    }
    if (wait_for_level(SENSOR_PIN, 0, 1000) < 0)
    {
        printf("No response (start data)\n");
        return;
    }

    taskENTER_CRITICAL(&gpio_mux);
    // citanje podataka
    int data[5] = {0};
    for (size_t i = 0; i < 40; i++)
    {
        int us = measure_high_pulse(SENSOR_PIN, 200);
        if (us < 0)
        {
            printf("Bit timeout\n");
            taskEXIT_CRITICAL(&gpio_mux);
            return;
        }
        int bit = (us > 50) ? 1 : 0;
        data[i / 8] = (data[i / 8] << 1) | bit;
    }
    taskEXIT_CRITICAL(&gpio_mux);

    // provjera checksum
    if (((data[0] + data[1] + data[2] + data[3]) & 0xFF) != data[4])
    {
        printf("Checksum error\n");
        return;
    }

    float humidity = data[0] + data[1] * 0.1f;
    float temperature = data[2] + data[3] * 0.1f;
    printf("Humidity: %.1f %% Temperature: %.1f C\n", humidity, temperature);
}

void app_main()
{
    while (1)
    {
        measure_temp_and_humidity();
        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }
}