#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

#define UART_NUM UART_NUM_2 //  UART2
#define TXD_PIN (GPIO_NUM_17) // Chân TX của UART1
#define RXD_PIN (GPIO_NUM_16) // Chân RX của UART1
#define BUF_SIZE (1024)

#define MQTT_BROKER_URI "mqtt://test.mosquitto.org:1883"
#define MQTT_TOPIC_SOIL_MOISTURE "sensor/Soil_Moisture"
#define MQTT_TOPIC_AIR_TEMPERATURE "sensor/Air_Temperature"
#define MQTT_TOPIC_AIR_HUMIDITY "sensor/Air_Humidity"
#define MQTT_TOPIC_WATER_PH "sensor/Water_pH"
#define MQTT_TOPIC_WATER_TEMPERATURE "sensor/Water_Temperature"


static const char *TAG = "MQTT_UART";
esp_mqtt_client_handle_t client;
uint8_t mqtt_data[BUF_SIZE];

int length;


static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        esp_mqtt_client_subscribe(client, MQTT_TOPIC_SOIL_MOISTURE, 0);
        esp_mqtt_client_subscribe(client, MQTT_TOPIC_AIR_TEMPERATURE, 0);
        esp_mqtt_client_subscribe(client, MQTT_TOPIC_AIR_HUMIDITY, 0);
        esp_mqtt_client_subscribe(client, MQTT_TOPIC_WATER_PH, 0);
        esp_mqtt_client_subscribe(client, MQTT_TOPIC_WATER_TEMPERATURE, 0);

        ESP_LOGI(TAG, "sent subscribe successful");
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    default:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    }
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://test.mosquitto.org:1883",
    };
#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.broker.address.uri, "FROM_STDIN") == 0) {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128) {
            int c = fgetc(stdin);
            if (c == '\n') {
                line[count] = '\0';
                break;
            } else if (c > 0 && c < 127) {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.broker.address.uri = line;
        printf("Broker url: %s\n", line);
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

void uart_init() {
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    // Cấu hình UART1
    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
}

void uart_read_task(void *arg) {
    uint8_t uart_data[BUF_SIZE];
    while (1) {
        // Đọc dữ liệu từ UART
        int length = uart_read_bytes(UART_NUM, uart_data, BUF_SIZE, 50 / portTICK_PERIOD_MS);
        if (length > 0) {
            uart_data[length] = '\0'; // end
            ESP_LOGI(TAG, "Received data: %s", uart_data);
            strcpy((char *)mqtt_data, (char *)uart_data);
            memset(uart_data, 0, sizeof(uart_data));
        }
        vTaskDelay(200/portTICK_PERIOD_MS); // delay
    }
}


void mqtt_publish_task(void *pvParameters) {
    while (1) {
        if (strstr((char *)mqtt_data, "\"sensor\":\"Soil Moisture Sensor\"") != NULL) {
            esp_mqtt_client_publish(client, MQTT_TOPIC_SOIL_MOISTURE, (char *)mqtt_data, strlen((char *)mqtt_data), 1, 0);
        }
        if (strstr((char *)mqtt_data, "\"sensor\":\"Air Temperature Sensor\"") != NULL) {
            esp_mqtt_client_publish(client, MQTT_TOPIC_AIR_TEMPERATURE, (char *)mqtt_data, strlen((char *)mqtt_data), 1, 0);
        }
        if (strstr((char *)mqtt_data, "\"sensor\":\"Air Humidity Sensor\"") != NULL) {
            esp_mqtt_client_publish(client, MQTT_TOPIC_AIR_HUMIDITY, (char *)mqtt_data, strlen((char *)mqtt_data), 1, 0);
        }
        if (strstr((char *)mqtt_data, "\"sensor\":\"Water pH Sensor\"") != NULL) {
            esp_mqtt_client_publish(client, MQTT_TOPIC_WATER_PH, (char *)mqtt_data, strlen((char *)mqtt_data), 1, 0);
        }
        if (strstr((char *)mqtt_data, "\"sensor\":\"Water Temperature Sensor\"") != NULL) {
            esp_mqtt_client_publish(client, MQTT_TOPIC_WATER_TEMPERATURE, (char *)mqtt_data, strlen((char *)mqtt_data), 1, 0);
        }

        vTaskDelay(100/portTICK_PERIOD_MS);
    }
}


void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    uart_init();

    mqtt_app_start();
    vTaskDelay(1000);
    xTaskCreate(uart_read_task, "uart_read_task", 4096, NULL, 10, NULL);
    xTaskCreate(mqtt_publish_task, "mqtt_publish_task", 4096, NULL, 5, NULL);
    
    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
