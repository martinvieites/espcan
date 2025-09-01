/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "hal/uart_types.h"
#include "string.h"
#include "driver/gpio.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include "mqtt_parser.h"
#include "esp_twai.h"
#include "at_mqtt.h"

#define DATA_TOPIC "dongle/esp32can/data"

static const int RX_BUF_SIZE = 1024;
static const char AT_MSUB[] = "+MSUB:";

static const char *TAG = "espcan";

int monitoring = 0;
struct pidListStruct pidStruct;

// Hilo encargado de la comunicación MQTT
static void vTaskMqtt (void *args)
{
    static const char *MQTT_TASK_TAG = "MQTT_TASK";
    char* data = (char*) malloc(RX_BUF_SIZE + 1);
    char subbuff[2];

    while (1) {
        struct uart_mqtt_message_t mqtt_message;
        const int rxBytes = uart_read_bytes(UART_NUM_2, data, RX_BUF_SIZE, 1000 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            int j = 0;
            ESP_LOG_BUFFER_HEXDUMP(TAG, data, rxBytes, ESP_LOG_INFO);
            data[rxBytes] = 0;
            if (strstr(data,AT_MSUB) != NULL){
                mqtt_message = parse_mqtt_message(data);

                // Se recibe una solicitud de identificadores
                if (strcmp(mqtt_message.topic, "dongle/esp32can/action/pids") == 0) {
                    if (strcmp(mqtt_message.message, "31") == 0) {
                        request_pids();
                    }

                // Se recibe un comando sobre la monitorización
                } else if (strcmp(mqtt_message.topic, "dongle/esp32can/action/monitor") == 0) {
                    // Se comprueba si se debe parar o iniciar la monitorización
                    if (strcmp(mqtt_message.message, "30") == 0) {
                        ESP_LOGI(TAG, "stop monitoring");
                        monitoring = 0;
                    } else {
                        // Parsear la lista de identificadores y comenzar la monitorización
                        // Copia de los datos
                        for (int i = 0; i < 2*mqtt_message.length; i = i+2) {
                            memcpy(subbuff, &mqtt_message.message[i], 2);
                            uint8_t num = (uint8_t)strtol(subbuff,NULL,16);
                            pidStruct.data[j] = num;
                            ESP_LOGI(TAG, "Parsed pid: %d", num);
                            j++;
                        }

                        // Copia de la longitud
                        pidStruct.length = mqtt_message.length;

                        // Se actualiza el estado de la monitorización
                        monitoring = 1;
                    }

                // Se recibe una solicitud del identificador VIN
                } else if (strcmp(mqtt_message.topic, "dongle/esp32can/action/vin") == 0) {
                    if (strcmp(mqtt_message.message, "31") == 0) {
                        ESP_LOGI(TAG, "received VIN request");
                        request_vin();
                    }

                // Se recibe una solicitud de los códigos de error
                } else if (strcmp(mqtt_message.topic, "dongle/esp32can/action/dtc") == 0) {
                    if (strcmp(mqtt_message.message, "31") == 0) {
                        ESP_LOGI(TAG, "received DTCs request");
                        request_dtc();
                    }
                }
            }
        }
    }
    free(data);
}

// Hilo encargado de la monitorización
void vTaskMonitor(void * pvParameters) {
    while (1) {
        if (monitoring == 1){
            for (int i = 0; i < pidStruct.length; i++) {

                // Solicitud del valor a monitorizar
                twai_message_t data_request = {
                    .extd = 0,              // Standard vs extended format
                    .rtr = 0,               // Data vs RTR frame
                    .ss = 0,                // Whether the message is single shot (i.e., does not repeat on error)
                    .self = 0,              // Whether the message is a self reception request (loopback)
                    .dlc_non_comp = 0,      // DLC is less than 8
                    // Message ID and payload
                    .identifier = 0x7DF,
                    .data_length_code = 8,
                    .data = {0x02, 0x01, pidStruct.data[i], 0xAA, 0xAA, 0xAA, 0xAA, 0xAA},
                };

                // Enviar mensaje
                if (twai_transmit(&data_request, pdMS_TO_TICKS(1000)) == ESP_OK) {
                    ESP_LOGI(TAG, "Message queued for transmission");
                } else {
                    ESP_LOGI(TAG, "Failed to queue message for transmission");
                }

                // Recibir información
                twai_message_t message;
                if (twai_receive(&message, pdMS_TO_TICKS(10000)) == ESP_OK) {
                    // Si el identificador se corresponde con el de una respuesta OBD-2 se envía el mensaje
                    if (message.identifier == 2024) {
                        sendBytes(message.data, DATA_TOPIC, message.data_length_code);
                    }
                } else {
                    ESP_LOGI(TAG, "Failed to receive message");
                }
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// Hilo principal del programa
void app_main(void)
{
    static const char *TAG = "MAIN";

    // Configuración de la comunicación MQTT
    init();
    if (!is_mqtt_connected()){
        ate_0();
        check_net();
        config_mqtt();
    }

    // Configuración del controlador CAN
    create_twai();

    // Creación de los hilos de las tareas
    xTaskCreate(vTaskMqtt, "vTaskMqtt", 4096, NULL, configMAX_PRIORITIES - 1, NULL);
    xTaskCreate(vTaskMonitor, "vTaskMonitor", 4096, NULL, configMAX_PRIORITIES - 1, NULL);
}
