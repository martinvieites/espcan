#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <esp_log.h>

struct uart_mqtt_message_t {
    char topic[256];
    int length;
    char message[256];
};
struct uart_mqtt_message_t parse_mqtt_message(char *inputString);
