#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RED_LED_PIN GPIO_NUM_27
#define YELLOW_LED_PIN GPIO_NUM_26
#define GREEN_LED_PIN GPIO_NUM_25

const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t my_mac[6];
uint8_t other_mac[6];
bool is_master = false;
bool role_determined = false;
bool change_received = true;
bool change_ack = true;
bool yellow_on = false;

TimerHandle_t heartbeat_timer = NULL;
TimerHandle_t heartbeat_alive_timer = NULL;
TimerHandle_t yellow_timer = NULL;

typedef enum
{
    MSG_HELLO = 0x01,
    MSG_ACK = 0x02,
    MSG_CHANGE = 0x03,
    MSG_HEARTBEAT = 0x04
} msg_type_t;

typedef struct
{
    uint8_t type;      // message type
    uint8_t payload[]; // flexible array
} msg_header_t;

typedef struct
{
    msg_header_t hdr; // type = MSG_HELLO
    uint8_t mac[6];
} msg_hello_t;

typedef struct
{
    msg_header_t hdr; // type = MSG_ACK
    uint8_t mac[6];
} msg_ack_t;

typedef struct
{
    msg_header_t hdr; // type = MSG_SYNC
    uint8_t flag;
} msg_change_t;

typedef struct
{
    msg_header_t hdr; // type = MSG_HEARTBEAT
    uint8_t mac[6];
} msg_heartbeat_t;

void start_wifi(void)
{
    ESP_ERROR_CHECK(nvs_flash_init()); // Initialize NVS

    ESP_ERROR_CHECK(esp_netif_init()); // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); // Get default Wi-Fi configuration
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA)); // Set Wi-Fi mode to AP+STA
    ESP_ERROR_CHECK(esp_wifi_start());
}

void heartbeat_timer_cb(TimerHandle_t xTimer)
{
    if (role_determined)
    {
        msg_heartbeat_t msg = {
            .hdr.type = MSG_HEARTBEAT,
        };
        memcpy(msg.mac, my_mac, 6);
        esp_now_send(other_mac, (uint8_t *)&msg, sizeof(msg_heartbeat_t)); // Send HEARTBEAT message
    }
}

void heartbeat_alive_timer_cb(TimerHandle_t xTimer)
{
    is_master = false;
    role_determined = false; // Reset role determination on heartbeat timeout
    change_received = true;
    change_ack = true;
    yellow_on = false;
    if (heartbeat_alive_timer != NULL)
    {
        xTimerDelete(heartbeat_alive_timer, 0);
        heartbeat_alive_timer = NULL;
    }
    if (heartbeat_timer != NULL)
    {
        xTimerDelete(heartbeat_timer, 0);
        heartbeat_timer = NULL;
    }

    printf("HEARTBEAT TIMEOUT: Resetting role determination\n");
}
void yellow_timer_cb(TimerHandle_t xTimer)
{
    yellow_on = !yellow_on;
    gpio_set_level(YELLOW_LED_PIN, yellow_on);
}

void determine_role_and_configure_leds()
{
    if (memcmp(my_mac, other_mac, 6) < 0)
    {
        is_master = true;
        printf("I am MASTER\n");
    }
    else
    {
        is_master = false;
        printf("I am SLAVE\n");
    }
    gpio_set_level(RED_LED_PIN, 1);
    gpio_set_level(YELLOW_LED_PIN, 0);
    gpio_set_level(GREEN_LED_PIN, 0);
    role_determined = true;

    // Delete old timers if they exist
    if (heartbeat_timer != NULL)
    {
        xTimerDelete(heartbeat_timer, 0);
    }
    if (heartbeat_alive_timer != NULL)
    {
        xTimerDelete(heartbeat_alive_timer, 0);
    }

    // Create new timers and store handles
    heartbeat_timer = xTimerCreate("heartbeat_timer", pdMS_TO_TICKS(2000), pdTRUE, NULL, heartbeat_timer_cb);
    heartbeat_alive_timer =
        xTimerCreate("heartbeat_alive_timer", pdMS_TO_TICKS(3000), pdTRUE, NULL, heartbeat_alive_timer_cb);

    // Start timers
    xTimerStart(heartbeat_timer, 0);
    xTimerStart(heartbeat_alive_timer, 0);
    xTimerStop(yellow_timer, 0);
}

void send_hello_broadcast()
{
    printf("Sending HELLO with MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", my_mac[0], my_mac[1], my_mac[2], my_mac[3],
           my_mac[4], my_mac[5]);
    msg_hello_t msg = {
        .hdr.type = MSG_HELLO,
    };
    memcpy(msg.mac, my_mac, 6);
    esp_now_send(broadcast_mac, (uint8_t *)&msg, sizeof(msg_hello_t)); // Send HELLO message
}

void handle_hello(const uint8_t *mac_addr, const msg_hello_t *data)
{
    printf("Received HELLO from: %02x:%02x:%02x:%02x:%02x:%02x\n", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3],
           mac_addr[4], mac_addr[5]);

    // Register peer before sending
    if (!esp_now_is_peer_exist(mac_addr))
    {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, mac_addr, 6);
        peer.channel = 0;
        peer.ifidx = ESP_IF_WIFI_STA;
        esp_now_add_peer(&peer);
        memcpy(other_mac, mac_addr, 6);
    }

    msg_ack_t msg = {
        .hdr.type = MSG_ACK,
    };
    memcpy(msg.mac, my_mac, 6);
    determine_role_and_configure_leds();
    esp_now_send(mac_addr, (uint8_t *)&msg, sizeof(msg_ack_t)); // Send ACK message
}

void handle_ack(const uint8_t *mac_addr, const msg_ack_t *data)
{
    printf("Received ACK from: %02x:%02x:%02x:%02x:%02x:%02x\n", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3],
           mac_addr[4], mac_addr[5]);

    if (!role_determined)
    {
        if (!esp_now_is_peer_exist(mac_addr))
        {
            esp_now_peer_info_t peer = {};
            memcpy(peer.peer_addr, mac_addr, 6);
            peer.channel = 0;
            peer.ifidx = ESP_IF_WIFI_STA;
            esp_now_add_peer(&peer);
            memcpy(other_mac, mac_addr, 6);
        }
        determine_role_and_configure_leds();
    }
    else
    {
        change_ack = false;
    }
}

void handle_change(const uint8_t *mac_addr, const msg_change_t *data)
{
    printf("Received CHANGE from: %02x:%02x:%02x:%02x:%02x:%02x\n", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3],
           mac_addr[4], mac_addr[5]);
    change_received = false; // Release the slave from waiting
    msg_ack_t msg = {.hdr.type = MSG_ACK};
    esp_now_send(mac_addr, (uint8_t *)&msg, sizeof(msg_ack_t)); // Send ACK message
}

void handle_heartbeat(const uint8_t *mac_addr, const msg_heartbeat_t *data)
{
    // Reset heartbeat alive timer
    if (heartbeat_alive_timer != NULL)
    {
        xTimerReset(heartbeat_alive_timer, 0);
    }
}

void recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    const uint8_t *mac_addr = recv_info->src_addr;
    if (role_determined)
    {
        msg_header_t *hdr = (msg_header_t *)data;
        switch (hdr->type)
        {
        case MSG_CHANGE:
            handle_change(mac_addr, (msg_change_t *)data);
            break;

        case MSG_ACK:
            handle_ack(mac_addr, (msg_ack_t *)data);
            break;

        case MSG_HEARTBEAT:
            handle_heartbeat(mac_addr, (msg_heartbeat_t *)data);
            break;

        default:
            break;
        }
    }
    else
    {
        msg_header_t *hdr = (msg_header_t *)data;

        switch (hdr->type)
        {
        case MSG_HELLO:
            handle_hello(mac_addr, (msg_hello_t *)data);
            break;

        case MSG_ACK:
            handle_ack(mac_addr, (msg_ack_t *)data);
            break;
        default:
            break;
        }
    }
}

void app_main(void)
{
    gpio_set_direction(RED_LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(YELLOW_LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(GREEN_LED_PIN, GPIO_MODE_OUTPUT);

    srand(time(NULL));
    start_wifi();
    esp_now_init();                    // Initialize ESP-NOW
    esp_now_register_recv_cb(recv_cb); // Register receive callback for ESP-NOW

    esp_wifi_get_mac(ESP_IF_WIFI_STA, my_mac); // Get device MAC address

    // Register broadcast peer for HELLO messages
    esp_now_peer_info_t broadcast_peer = {};
    memset(broadcast_peer.peer_addr, 0xFF, 6);
    broadcast_peer.channel = 0;
    broadcast_peer.ifidx = ESP_IF_WIFI_STA;
    esp_now_add_peer(&broadcast_peer);

    yellow_timer = xTimerCreate("yellow_timer", pdMS_TO_TICKS(500), pdTRUE, NULL, yellow_timer_cb);
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait a moment before starting

    while (1)
    {
        if (!role_determined)
        {
            gpio_set_level(RED_LED_PIN, 0);
            gpio_set_level(GREEN_LED_PIN, 0);
            gpio_set_level(YELLOW_LED_PIN, 0);
            xTimerStart(yellow_timer, 0);
        }

        while (!role_determined)
        {
            send_hello_broadcast(); // Broadcast HELLO messages until role is determined
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        int green_duration = rand() % 5000 + 5000; // Random green duration between 5-10 seconds
        if (is_master)
        {
            printf("MASTER: Starting green light cycle\n");
            vTaskDelay(pdMS_TO_TICKS(1000));
            gpio_set_level(RED_LED_PIN, 1);
            gpio_set_level(YELLOW_LED_PIN, 1);
            gpio_set_level(GREEN_LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));

            gpio_set_level(RED_LED_PIN, 0);
            gpio_set_level(YELLOW_LED_PIN, 0);
            gpio_set_level(GREEN_LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(green_duration));

            printf("MASTER: Sending CHANGE to slave\n");
            msg_change_t msg = {.hdr.type = MSG_CHANGE, .flag = 1};
            esp_now_send(other_mac, (uint8_t *)&msg, sizeof(msg_change_t)); // Send CHANGE message
            while (change_ack && role_determined)
            {
                vTaskDelay(pdMS_TO_TICKS(100));
            }

            if (role_determined)
            {
                change_ack = true;
                is_master = false; // Swap role: master becomes slave
                printf("MASTER: Became SLAVE\n");
            }
        }
        else
        {
            printf("SLAVE: Waiting for CHANGE signal\n");
            gpio_set_level(RED_LED_PIN, 1);
            gpio_set_level(YELLOW_LED_PIN, 0);
            gpio_set_level(GREEN_LED_PIN, 0);
            while (change_received && role_determined)
            {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            if (role_determined)
            {
                change_received = true;
                is_master = true; // Swap role: slave becomes master
                printf("SLAVE: Became MASTER\n");
            }
        }
    }
}