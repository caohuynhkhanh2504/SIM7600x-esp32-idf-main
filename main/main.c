#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "mqtt_client.h"
#include "string.h"
#include <inttypes.h>
#include "esp_netif.h"

#define MODEM_TX 17          // Chân TX
#define MODEM_RX 16          // Chân RX
#define MODEM_EN 15          // Chân EN (không dùng)
#define UART_NUM UART_NUM_2  // UART cho SIM7600
#define MODEM_BAUD 115200    // Baud rate


static const char *TAG = "SIM7600C_MQTT";

// Thông tin mạng và MQTT
const char *apn = "v-internet";
const char *mqtt_server = "mqtt://test.mosquitto.org:1883";
const char *mqtt_topic = "topic/huka";
const char *phone_number = "+84398481814";

char mqtt_data[100];
uint8_t rx_buffer[1024];

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

// Gửi lệnh AT qua UART và kiểm tra phản hồi
bool send_at_command(const char *cmd, const char *expected_response, int timeout_ms) {
    uart_write_bytes(UART_NUM, cmd, strlen(cmd));
    uart_write_bytes(UART_NUM, "\r\n", 2);
    //ESP_LOGI(TAG, "Command: %s", cmd);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    int len = uart_read_bytes(UART_NUM, rx_buffer, sizeof(rx_buffer) - 1, pdMS_TO_TICKS(timeout_ms));
    if (len > 0) {
        rx_buffer[len] = 0;
        ESP_LOGI(TAG, "Response: %s", rx_buffer);
        if (strstr((char *)rx_buffer, expected_response) != NULL) {
            return true; // Phản hồi khớp
        }else{
            ESP_LOGE(TAG, "Unexpected response: %s", rx_buffer); // Log nếu phản hồi không khớp
        }
    }else{
         ESP_LOGE(TAG, "No response received");
    }
    return false;
}

// Khởi tạo và kết nối GPRS
bool setup_gprs() {

    if (send_at_command("AT+NETCLOSE", "OK", 2000)) { // Đóng kết nối nếu cần
        ESP_LOGI(TAG, "Network closed successfully");
    }

    send_at_command("AT+CPIN?", "OK", 2000);

    if (!send_at_command("AT+CREG?", "OK", 2000)) {
        ESP_LOGE(TAG, "Failed to check network registration status");
        return false;
    }

    if (send_at_command("AT+CGATT=1", "OK", 2000)) {
        ESP_LOGI(TAG, "Attached to GPRS");
        vTaskDelay(pdMS_TO_TICKS(500));
    } else {
        ESP_LOGE(TAG, "Failed to attach GPRS");
        return false;
    }
    send_at_command("AT+CSQ", "OK", 2000); // Kiểm tra tín hiệu

    if (send_at_command("AT+CGDCONT=1,\"IP\",\"v-internet\"", "OK", 2000)) {
        ESP_LOGI(TAG, "APN set successfully");
    } else {
        ESP_LOGE(TAG, "Failed to set APN");
        return false;
    }

    send_at_command("AT+COPS?", "OK", 2000); // Kiểm tra mạng

    if (send_at_command("AT+NETOPEN", "OK", 5000)) {
        ESP_LOGI(TAG, "GPRS Connected Successfully.");
    }else{
        ESP_LOGE(TAG, "GPRS Connected Failed.");
        return false;
    }
    
    send_at_command("AT+CGACT?", "OK", 2000);
    send_at_command("AT+CGPADDR", "OK", 2000);


/*
    // Kiểm tra kết nối PDP
    if (!send_at_command("AT+CGACT=1", "OK", 2000)) {
        ESP_LOGE(TAG, "Failed to check PDP status");
        return false;
    }
*/
    // Kiểm tra chất lượng tín hiệu
    // Kiểm tra tình trạng kết nối PDP
/*
    if (send_at_command("AT+NETCLOSE", "OK", 2000)) { // Đóng kết nối nếu cần
        ESP_LOGI(TAG, "Network closed successfully");
    }

     if (send_at_command("AT+NETOPEN", "OK", 5000)) {
        ESP_LOGI(TAG, "GPRS Connected Successfully");
        return true;
    }
*/
    ESP_LOGI(TAG, "GPRS Connection Successfully");
    return true;
}

bool setup_dns() {
    // Đảm bảo rằng GPRS đã được kết nối
    if (!send_at_command("AT+CGATT?", "1", 2000)) {
        ESP_LOGE(TAG, "GPRS not attached");
        return false;
    }
/*
    // Cài đặt DNS
    if (send_at_command("AT+CDNSCFG=\"8.8.8.8\",\"8.8.4.4\"", "OK", 2000)) {
        ESP_LOGI(TAG, "DNS Server set to 8.8.8.8");
    } else {
        ESP_LOGE(TAG, "Failed to set DNS");
        return false;
    }

    // Thực hiện kiểm tra DNS
    if (send_at_command("AT+SISHR=1", "OK", 2000)) {
        ESP_LOGI(TAG, "DNS setup successfully");
    } else {
        ESP_LOGE(TAG, "Failed to retrieve DNS");
        return false;
    }
*/
    return true;
}


void close_gprs() {
    if (send_at_command("AT+NETCLOSE", "OK", 2000)) {
        ESP_LOGI(TAG, "Network closed successfully");
    } else {
        ESP_LOGE(TAG, "Failed to close network");
    }
}

// MQTT sự kiện handler
static esp_mqtt_client_handle_t mqtt_client;
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            msg_id = esp_mqtt_client_subscribe(client, mqtt_topic, 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            break;
        default:
            ESP_LOGI(TAG, "Error for MQTT");
            break;
    }
}

// Cấu hình MQTT
void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = mqtt_server,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}


void make_call(const char *number) {
    char command[32];
    sprintf(command, "ATD%s;", number);
    if (send_at_command(command, "OK", 5000)) {
        ESP_LOGI(TAG, "Calling %s", number);
    } else {
        ESP_LOGE(TAG, "Failed to call %s", number);
    }
}


void send_sms(const char *number, const char *message) {
    char command[256];
    
    // Đặt chế độ văn bản cho tin nhắn SMS
    if (!send_at_command("AT+CMGF=1", "OK", 2000)) {
        ESP_LOGE(TAG, "Failed to set SMS to text mode");
        return;
    }

    // Tạo lệnh gửi SMS
    sprintf(command, "AT+CMGS=\"%s\"", number);
    if (!send_at_command(command, ">", 2000)) {
        ESP_LOGE(TAG, "Failed to set SMS recipient");
        return;
    }

    // Gửi tin nhắn
    uart_write_bytes(UART_NUM, message, strlen(message));
    uart_write_bytes(UART_NUM, "\x1A", 1);  // Gửi ký tự Ctrl+Z để kết thúc tin nhắn
    vTaskDelay(pdMS_TO_TICKS(2000)); // Đợi một chút để nhận phản hồi

    // Kiểm tra phản hồi
    if (send_at_command("", "OK", 5000)) {
        ESP_LOGI(TAG, "SMS sent successfully");
    } else {
        ESP_LOGE(TAG, "Failed to send SMS");
    }
}



// Hàm chính
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

    // Khởi tạo GPRS
    if (setup_gprs()) {
        ESP_LOGI(TAG, "GPRS setup succesfully.");
    }else{
        ESP_LOGE(TAG, "GPRS setup failed.");
        return;
    }

    if (setup_dns()) {
        ESP_LOGI(TAG, "DNS setup successfully.");
    }else{
        ESP_LOGE(TAG, "DNS setup failed.");
        return;
    }

    if (send_at_command("AT+CREG?", "OK", 2000)) {
        ESP_LOGI(TAG, "Network registration status checked");
    } else {
        ESP_LOGE(TAG, "Failed to check network registration status");
    }

    send_at_command("AT+CGREG?", "OK", 2000);
    if (send_at_command("AT+NETOPEN?", "OK", 5000)) {
    ESP_LOGI(TAG, "Network opened successfully");
    } else {
        ESP_LOGE(TAG, "Failed to open network");
    }

    send_at_command("AT+CGDCONT?", "OK", 2000);
    send_at_command("AT+CPING=?", "OK", 5000);
    send_at_command("AT+IPADDR", "OK", 2000);
    send_at_command("AT+CIPRXGET=1", "OK", 2000);
    send_at_command("AT+CIPOPEN", "OK", 2000);
    send_at_command("AT+CIPOPEN=1,\"TCP\",\"test.mosquitto.org\",1883\"", "OK", 5000);
    send_at_command("AT+CIPSEND=1,27", "OK", 2000);

    if (send_at_command("AT+CPING=\"8.8.8.8\",5000", "OK", 5000)) {
        ESP_LOGI(TAG, "internet connected.");
    }else{
        ESP_LOGE(TAG, "No internet connection.");
    //    return;
    }
    // Khởi tạo MQTT
    //send_sms(phone_number, "CAO HUYNH KHANH");
    //make_call(phone_number);
    mqtt_app_start();

    // Chu kỳ gửi dữ liệu MQTT
    while (1) {
        strcpy(mqtt_data, "Cao Huynh Khanh");
        esp_mqtt_client_publish(mqtt_client, mqtt_topic, mqtt_data, 0, 1, 0);
        ESP_LOGI(TAG, "Published data: %s", mqtt_data);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
