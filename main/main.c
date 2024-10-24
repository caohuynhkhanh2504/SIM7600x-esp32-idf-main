#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "mqtt_client.h"
#include "string.h"
#include <inttypes.h>
#include "esp_netif.h"

#define MODEM_TX 17   // Chân TX
#define MODEM_RX 16   // Chân RX
#define MODEM_EN 15   // Chân EN
#define UART_NUM UART_NUM_2
#define MODEM_BAUD 115200

static const char *TAG = "SIM7600x_MQTT";

// Thông tin mạng và MQTT
const char *apn = "v-internet";
const char *phone_number = "+84398481814";
const char *mqtt_server = "mqtt://test.mosquitto.org:1883";
const char *mqtt_topic = "topic/huka";
char mqtt_data[100];
    
// UART cấu hình
void uart_init() {
    const uart_config_t uart_config = {
        .baud_rate = MODEM_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, MODEM_TX, MODEM_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM, 4096, 0, 0, NULL, 0);
}

// Gửi lệnh AT qua UART
bool send_at_command(const char *cmd) {
    uart_write_bytes(UART_NUM, cmd, strlen(cmd));
    uart_write_bytes(UART_NUM, "\r\n", 2);
    vTaskDelay(pdMS_TO_TICKS(500));  // Đợi phản hồi từ modem
    
    uint8_t data[128];
    memset(data, 0, sizeof(data));
    int len = uart_read_bytes(UART_NUM, data, sizeof(data), pdMS_TO_TICKS(2000));
    if (len > 0) {
        data[len] = 0;
        ESP_LOGI(TAG, "Response: %s", data);
        return strstr((char *)data, "OK") != NULL || strstr((char *)data, "ERROR") != NULL;
    }
    return false;
}

// Hàm thực hiện cuộc gọi
void make_call(const char *number) {
    char command[32];
    sprintf(command, "ATD%s;", number);
    if (send_at_command(command)) {
        ESP_LOGI(TAG, "Calling %s", number);
    } else {
        ESP_LOGE(TAG, "Failed to call %s", number);
    }
}


// MQTT sự kiện handler
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            msg_id = esp_mqtt_client_subscribe(client, mqtt_topic, 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_publish(client, mqtt_topic, "Done Subscribe", 0, 1, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
    //        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
    //       printf("DATA=%.*s\r\n", event->data_len, event->data);
            break;
        default:
            ESP_LOGI(TAG, "Error for MQTT");
            break;
    }
}

// Cấu hình MQTT
esp_mqtt_client_handle_t mqtt_client;
void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = mqtt_server,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

bool check_network_connectivity() {
    // Kiểm tra kết nối mạng
    if (!send_at_command("AT+CGATT?")) {
        ESP_LOGE(TAG, "Module is not attached to the network.");
        return false;
    }
    
    // Kiểm tra IP
    if (!send_at_command("AT+CIFSR")) {
        ESP_LOGE(TAG, "Failed to acquire IP address.");
        return false;
    }
    
    // Ping thử tới Google DNS
    if (!send_at_command("AT+CDNSCFG=\"8.8.8.8\",\"8.8.4.4\"")) {
        ESP_LOGE(TAG, "Failed to reach external DNS server.");
        return false;
    }
    
    return true;
}

void app_main(void) {
    // Khởi tạo NVS cho ESP-IDF
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    // Cấu hình UART
    uart_init();

    // Kết nối mạng và kiểm tra Internet qua SIM7600
   if (send_at_command("AT+CGATT=1")) {  // Kết nối GPRS
        vTaskDelay(pdMS_TO_TICKS(500));
        send_at_command("AT+CGDCONT=1,\"IP\",\"v-internet\"");
        vTaskDelay(pdMS_TO_TICKS(500));
        if (send_at_command("AT+NETOPEN")) {
            vTaskDelay(pdMS_TO_TICKS(500));
            send_at_command("AT+CSQ");
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    } else {
        ESP_LOGE(TAG, "Failed to attach to GPRS");
    }

//    make_call(phone_number);
    vTaskDelay(pdMS_TO_TICKS(10000));

    send_at_command("AT+CREG?");
    if (send_at_command("AT+CGATT?")) {
        ESP_LOGI(TAG, "Module is attached to the GPRS network.");
    } else {
        ESP_LOGE(TAG, "Module is not attached to the GPRS network.");
    }
    send_at_command("AT+CIICR");
    vTaskDelay(pdMS_TO_TICKS(1000));
    send_at_command("AT+CGPADDR");
    // Khởi tạo MQTT
    if (check_network_connectivity()) {
        ESP_LOGI(TAG, "Network is connected, proceed with MQTT.");
    } else {
        ESP_LOGE(TAG, "Network check failed.");
        return;
    }
    send_at_command("AT+CIICR");
    vTaskDelay(pdMS_TO_TICKS(100));
    mqtt_app_start();
     // In kích thước bộ nhớ heap còn lại
    ESP_LOGI(TAG, "Free heap size: %" PRIu32, esp_get_free_heap_size());
    while (1) {
        ESP_LOGI(TAG, "Free heap size: %" PRIu32, esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
