#include "esp_twai.h"
#include "hal/twai_types.h"

static const char *TAG = "TWAI";

// Inicialización del controlador
void create_twai(void) {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_5, GPIO_NUM_4, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    // Instalación del driver
    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        printf("Driver installed\n");
    } else {
        printf("Failed to install driver\n");
        return;
    }

    // Arranque del driver
    if (twai_start() == ESP_OK) {
        printf("Driver started\n");
    } else {
        printf("Failed to start driver\n");
        return;
    }
}
// Solicitar información dos PIDS dispoñibles
void request_pids() {

    const uint8_t pids_supported[2] = {0x00, 0x20};
    for (int i = 0; i < sizeof(pids_supported); i++){

        twai_message_t data_request = {
            .extd = 0,              // Standard vs extended format
            .rtr = 0,               // Data vs RTR frame
            .ss = 0,                // Whether the message is single shot (i.e., does not repeat on error)
            .self = 0,              // Whether the message is a self reception request (loopback)
            .dlc_non_comp = 0,      // DLC is less than 8
            // Message ID and payload
            .identifier = 0x7DF,
            .data_length_code = 8,
            .data = {0x02, 0x01, pids_supported[i], 0xAA, 0xAA, 0xAA, 0xAA, 0xAA},
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
            ESP_LOGI(TAG, "received any message");
            // Se o identificador se corresponde co de respuesta de obd2 envíar mensaje
            if (message.identifier == 2024) {
                ESP_LOGI(TAG, "sending message");
                sendBytes(message.data, "dongle/esp32can/pids", message.data_length_code);
                ESP_LOGI(TAG, "sent data successful, msg=%s", (char *)message.data);
            }

        } else {
            ESP_LOGI(TAG, "Failed to receive message");
        }
    }
}

// Obtener el identificador VIN del vehículo
void request_vin()
{

    char vin[35];
    vin[0] = '\0';

    // Solicitar VIN
    twai_message_t data_request = {
        .extd = 0,              // Standard vs extended format
        .rtr = 0,               // Data vs RTR frame
        .ss = 0,                // Whether the message is single shot (i.e., does not repeat on error)
        .self = 0,              // Whether the message is a self reception request (loopback)
        .dlc_non_comp = 0,      // DLC is less than 8
        // Message ID and payload
        .identifier = 0x7E0,
        .data_length_code = 8,
        .data = {0x02, 0x09, 0x02, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA},
    };

    // Enviar mensaje
    if (twai_transmit(&data_request, pdMS_TO_TICKS(1000)) == ESP_OK) {
        ESP_LOGI(TAG, "Message queued for transmission");
    } else {
        ESP_LOGI(TAG, "Failed to queue message for transmission");
    }

    // Recepción primera trama
    twai_message_t message;
    if (twai_receive(&message, pdMS_TO_TICKS(10000)) == ESP_OK) {

        for (int i = 5; i < message.data_length_code; i++) {
            sprintf(vin + strlen(vin), "%02x", message.data[i]);
        }

    } else {
        ESP_LOGI(TAG, "Failed to receive message\n");
    }

    // Mensaje de continuación
    twai_message_t continue_request = {
        .extd = 0,              // Standard vs extended format
        .rtr = 0,               // Data vs RTR frame
        .ss = 0,                // Whether the message is single shot (i.e., does not repeat on error)
        .self = 0,              // Whether the message is a self reception request (loopback)
        .dlc_non_comp = 0,      // DLC is less than 8
        // Message ID and payload
        .identifier = 0x7E0,
        .data_length_code = 8,
        .data = {0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    };

    // Enviar mensaje
    if (twai_transmit(&continue_request, pdMS_TO_TICKS(1000)) == ESP_OK) {
        ESP_LOGI(TAG, "Message queued for transmission");
    } else {
        ESP_LOGI(TAG, "Failed to queue message for transmission");
    }

    // Recepción segunda y tercera trama
    for (int j = 0; j < 2; j++)
    {
        // Esperar a recibir el mensaje
        twai_message_t message;
        if (twai_receive(&message, pdMS_TO_TICKS(10000)) == ESP_OK) {
            for (int i = 1; i < message.data_length_code; i++) {
                sprintf(vin + strlen(vin), "%02x", message.data[i]);
            }
        } else {
            ESP_LOGI(TAG, "Failed to receive message\n");
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }

    vin[strlen(vin)] = 0;
    sendMessage(vin, "dongle/esp32can/vin");
}

// Obtención de los códigos de error del vehículo
void request_dtc()
{
    int dtcs_count = 0;
    char dtcs[35];
    dtcs[0] = '\0';

    // Solicitar DTC
    twai_message_t data_request = {
        .extd = 0,              // Standard vs extended format
        .rtr = 0,               // Data vs RTR frame
        .ss = 0,                // Whether the message is single shot (i.e., does not repeat on error)
        .self = 0,              // Whether the message is a self reception request (loopback)
        .dlc_non_comp = 0,      // DLC is less than 8
        // Message ID and payload
        .identifier = 0x7DF,
        .data_length_code = 8,
        .data = {0x01, 0x03, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA},
    };

    // Enviar mensaje
    if (twai_transmit(&data_request, pdMS_TO_TICKS(1000)) == ESP_OK) {
        printf("Message queued for transmission\n");
    } else {
        printf("Failed to queue message for transmission\n");
    }

    // Recibir primera trama
    twai_message_t message;
    if (twai_receive(&message, pdMS_TO_TICKS(10000)) == ESP_OK) {

        // Obtener el número de códigos de error
        dtcs_count = message.data[2];
        if (dtcs_count == 0) {
            // Sin DTCs
            sendMessage("30", "dongle/esp32can/dtc/0");
            return;
        } else {
            for (int i = 4; i < message.data_length_code; i++) {
                if (message.data[i] != 0xAA) {
                    sprintf(dtcs + strlen(dtcs), "%02x", message.data[i]);
                }
            }
        }

    } else {
        printf("Failed to receive message\n");
    }

    // Mientras sigan existiendo códigos de error
    while (message.data[message.data_length_code-1] != 0xAA) {
        // Mensaje de continuación
        twai_message_t continue_request = {
            .extd = 0,              // Standard vs extended format
            .rtr = 0,               // Data vs RTR frame
            .ss = 0,                // Whether the message is single shot (i.e., does not repeat on error)
            .self = 0,              // Whether the message is a self reception request (loopback)
            .dlc_non_comp = 0,      // DLC is less than 8
            // Message ID and payload
            .identifier = 0x7E0,
            .data_length_code = 8,
            .data = {0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        };

        // Enviar mensaje
        if (twai_transmit(&continue_request, pdMS_TO_TICKS(1000)) == ESP_OK) {
            ESP_LOGI(TAG, "Message queued for transmission");
        } else {
            ESP_LOGI(TAG, "Failed to queue message for transmission");
        }

        // Recibir tramas
        if (twai_receive(&message, pdMS_TO_TICKS(10000)) == ESP_OK) {
            for (int i = 2; i < message.data_length_code; i++) {
                if (message.data[i] != 0xAA) {
                    sprintf(dtcs + strlen(dtcs), "%02x", message.data[i]);
                }
            }
        } else {
            printf("Failed to receive message\n");
        }
    }

    // Enviar datos
    char buff[32];
    snprintf(buff, 30+dtcs_count,"dongle/esp32can/dtc/");
    sendMessage(dtcs, buff);
}
