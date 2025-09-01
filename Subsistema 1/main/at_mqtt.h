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

void init(void);
int sendData(const char* logName, const char* data);
int check_ok(int rxBytes, uint8_t * data);
void ate_0();
void check_net();
void config_mqtt();
int is_mqtt_connected();
void sendMessage(char* message, char* topic);
void sendBytes(uint8_t* bytes, char* topic, int length);
