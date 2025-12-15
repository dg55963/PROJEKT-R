#include "ble/ble_service.h"
#include <dht.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#define SENSOR_TYPE DHT_TYPE_AM2301
#define SENSOR_GPIO 4

void dht_test(void *pvParameters)
{
    float temperature, humidity;

    while (1)
    {
        if (dht_read_float_data(SENSOR_TYPE, SENSOR_GPIO, &humidity, &temperature) == ESP_OK)
        {
            printf("Humidity: %.1f%% Temp: %.1fC\n", humidity, temperature);
            ble_service_send_data(temperature, humidity);
        }
        else
        {
            printf("Could not read data from sensor\n");
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main()
{
    ble_service_init();
    xTaskCreate(dht_test, "dht_test", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL);
}
