#include "at_mqtt.h"
#include <stdint.h>

static const int RX_BUF_SIZE = 1024;

static const char* MCONFIG_COMMAND = "AT+MCONFIG=esp32can,\"mqttuser\",\"mqttpasswd\",0,1,\"dongle/esp32can/status\",\"30\"\r";
static const char* SUBSCRIBE_COMMAND = "AT+MSUB=\"dongle/esp32can/action/#\",0\r";
static const char* CONNECT_COMMAND = "AT+MIPSTART=\"coiro.duckdns.org\",\"1883\"\r";
static const char* WILL_COMMAND = "AT+MPUB=\"dongle/esp32can/status\",0,1,\"31\"\r";
static const char* MPUB_TEMPLATE = "AT+MPUB=\"%s\",0,0,\"%s\"\r";

#define TXD_PIN (GPIO_NUM_23)
#define RXD_PIN (GPIO_NUM_22)

// Inicialización del protocolo UART
void init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    // No se utiliza un buffer para el envío de datos
    uart_driver_install(UART_NUM_2, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_2, &uart_config);
    uart_set_pin(UART_NUM_2, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

// Enviar datos al módem
int sendData(const char* logName, const char* data)
{
    const int len = strlen(data);
    const int txBytes = uart_write_bytes(UART_NUM_2, data, len);
    ESP_LOGI(logName, "Wrote %d bytes", txBytes);
    return txBytes;
}

// Comprobar el retorno del comando ejecutado
int check_ok(int rxBytes, uint8_t * data)
{
    static const char *TX_TASK_TAG = "CHECK_OK";
    if (data[rxBytes-3] == 75) {
        ESP_LOGI(TX_TASK_TAG, "COMMAND OK");
        return 1;
    } else {
        ESP_LOGI(TX_TASK_TAG, "COMMAND ERROR");
        return 0;
    }
}

// Eliminar la repetición del comando enviado por parte del módem
void ate_0()
{
    static const char *TX_TASK_TAG = "ATE_0";
    uint8_t* data = (uint8_t*) malloc(RX_BUF_SIZE + 1);
    bool commandOk = false;
    bool hexOk = false;

    while (!commandOk){
        sendData(TX_TASK_TAG, "ATE0\r");
        int rxBytes = uart_read_bytes(UART_NUM_2, data, RX_BUF_SIZE, 5000 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            data[rxBytes] = 0;
            ESP_LOGI(TX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data);
            if (check_ok(rxBytes, data)) {
                commandOk = true;
            } else {
                vTaskDelay(10000 / portTICK_PERIOD_MS);
            }
        }
    }

    while (!hexOk){
        sendData(TX_TASK_TAG, "AT+MQTTMODE=1\r");
        int rxBytes = uart_read_bytes(UART_NUM_2, data, RX_BUF_SIZE, 5000 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            data[rxBytes] = 0;
            ESP_LOGI(TX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data);
            if (check_ok(rxBytes, data)) {
                hexOk = true;
            } else {
                vTaskDelay(10000 / portTICK_PERIOD_MS);
            }
        }
    }

    ESP_LOGI(TX_TASK_TAG, "DONE: ATE0");

}

// Comprobar el estado de la red antes de conectar con el servidor MQTT
void check_net()
{
    static const char *TX_TASK_TAG = "CHECK_NET";
    uint8_t* data = (uint8_t*) malloc(RX_BUF_SIZE + 1);
    bool commandReg = false;
    bool commandAtt = false;
    uint8_t stat = 0;
    uint8_t state = 0;

    // Comprobar conexión con la red
    while (!commandReg){
        sendData(TX_TASK_TAG, "AT+CGREG?\r");
        int rxBytes = uart_read_bytes(UART_NUM_2, data, RX_BUF_SIZE, 5000 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            data[rxBytes] = 0;
            ESP_LOGI(TX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data);
            if (check_ok(rxBytes, data)) {
                stat = data[rxBytes-9] - 48;
                if (stat == 1 || stat == 5){
                    ESP_LOGI(TX_TASK_TAG, "CREG CHECK OK");
                    commandReg = true;
                } else {
                    ESP_LOGI(TX_TASK_TAG, "FAILED CREG CHECK");
                    vTaskDelay(10000 / portTICK_PERIOD_MS);
                }
            } else {
                vTaskDelay(10000 / portTICK_PERIOD_MS);
            }
        }
    }

    // Comprobar GPRS
    while (!commandAtt){
        sendData(TX_TASK_TAG, "AT+CGATT?\r");
        int rxBytes = uart_read_bytes(UART_NUM_2, data, RX_BUF_SIZE, 5000 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            data[rxBytes] = 0;
            if (check_ok(rxBytes, data)) {
                state = data[rxBytes-9] - 48;
                if (state == 1){
                    ESP_LOGI(TX_TASK_TAG, "CGATT CHECK OK");
                    commandAtt = true;
                } else {
                    ESP_LOGI(TX_TASK_TAG, "FAILED CGATT CHECK");
                    vTaskDelay(10000 / portTICK_PERIOD_MS);
                }
            } else {
                vTaskDelay(10000 / portTICK_PERIOD_MS);
            }
        }
    }

    ESP_LOGI(TX_TASK_TAG, "DONE: NET CHECK");
}

// Configurar cliente MQTT
void config_mqtt()
{
    static const char *TX_TASK_TAG = "CONFIG_MQTT";
    uint8_t* data = (uint8_t*) malloc(RX_BUF_SIZE + 1);
    bool clientConfigOk = false;
    bool mipStart = false;
    bool mConnect = false;
    bool mSubscribe = false;

    while (!clientConfigOk) {
        sendData(TX_TASK_TAG, MCONFIG_COMMAND);
        int rxBytes = uart_read_bytes(UART_NUM_2, data, RX_BUF_SIZE, 5000 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            data[rxBytes] = 0;
            ESP_LOGI(TX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data);
            if (check_ok(rxBytes, data)) {
                clientConfigOk = true;
            } else {
                vTaskDelay(10000 / portTICK_PERIOD_MS);
            }
        }
    }
    ESP_LOGI(TX_TASK_TAG, "DONE: CLIENT CONFIG");

    while (!mipStart) {
        sendData(TX_TASK_TAG, CONNECT_COMMAND);
        int rxBytes = uart_read_bytes(UART_NUM_2, data, RX_BUF_SIZE, 5000 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            data[rxBytes] = 0;
            ESP_LOGI(TX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data);
            if (check_ok(rxBytes, data)) {
                mipStart = true;
            } else {
                vTaskDelay(10000 / portTICK_PERIOD_MS);
            }
        }
    }
    ESP_LOGI(TX_TASK_TAG, "DONE: MIP START");

    while (!mConnect) {
        sendData(TX_TASK_TAG, "AT+MCONNECT=1,60\r");
        int rxBytes = uart_read_bytes(UART_NUM_2, data, RX_BUF_SIZE, 5000 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            data[rxBytes] = 0;
            ESP_LOGI(TX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data);
            if (check_ok(rxBytes, data)) {
                mConnect = true;
            } else {
                vTaskDelay(10000 / portTICK_PERIOD_MS);
            }
        }
    }
    ESP_LOGI(TX_TASK_TAG, "DONE: MCONNECT");

    while (!mSubscribe) {
        sendData(TX_TASK_TAG, SUBSCRIBE_COMMAND);
        int rxBytes = uart_read_bytes(UART_NUM_2, data, RX_BUF_SIZE, 5000 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            data[rxBytes] = 0;
            ESP_LOGI(TX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data);
            if (check_ok(rxBytes, data) && strstr((char *)data, "SUBACK") != NULL) {
                mSubscribe = true;
            } else {
                vTaskDelay(10000 / portTICK_PERIOD_MS);
            }
        }
    }
    ESP_LOGI(TX_TASK_TAG, "DONE: SUBSCRIBE");

    // Establecer estado a conectado
    sendData(TX_TASK_TAG, WILL_COMMAND);
}

// Comprobar el estado de la conexión con el servidor MQTT
int is_mqtt_connected()
{
    static const char *TX_TASK_TAG = "MODEM_CHECK";
    uint8_t* data = (uint8_t*) malloc(RX_BUF_SIZE + 1);
    int state = 0;

    sendData(TX_TASK_TAG, "AT+MQTTSTATU\r");
    int rxBytes = uart_read_bytes(UART_NUM_2, data, RX_BUF_SIZE, 5000 / portTICK_PERIOD_MS);
    if (rxBytes > 0) {
        data[rxBytes] = 0;
        ESP_LOGI(TX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data);
        if (check_ok(rxBytes, data)) {
            state = data[rxBytes-9] - 48;
        }
    }

    ESP_LOGI(TX_TASK_TAG, "DONE: CHECK IF MODEM CONNECTED");
    return state;
}

// Enviar mensaje
void sendMessage(char* message, char* topic)
{
    static const char *TAG = "SEND_MESSAGE";
    char output_msg[128];

    sprintf(output_msg, MPUB_TEMPLATE, topic, message);
    ESP_LOGI(TAG, "Message: %s", message);
    ESP_LOGI(TAG, "Output message: %s", output_msg);
    sendData("SEND_MESSAGE", output_msg);
    ESP_LOG_BUFFER_HEXDUMP(TAG, output_msg, strlen(output_msg), ESP_LOG_INFO);
}

// Enviar bytes
void sendBytes(uint8_t* bytes, char* topic, int length)
{
    static const char *TAG = "SEND_BYTES";

    char buffer[3];
    char output[16];

    for (int i = 0; i < length; i++) {
        sprintf(buffer, "%02x", bytes[i]);
        strcat(output, buffer);
    }

    sendMessage(output, topic);
}
