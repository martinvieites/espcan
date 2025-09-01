#include "mqtt_parser.h"

// Parser de los mensajes MQTT
struct uart_mqtt_message_t parse_mqtt_message(char *inputString){
    static const char *PARSER_TAG = "PARSER";

    struct uart_mqtt_message_t fullMessage;

    char clearMessage[256*3];
    char data[3][254];
    char topicLen[40];

    strncpy(clearMessage, 9 + inputString, strlen(inputString) - 9);

    char *token = strtok(clearMessage, ",");
    for (int i = 0; i < 3; i++) {
        strcpy(data[i], token);
        token = strtok(NULL, ",");
    }

    strcpy(topicLen, strtok(data[1], " "));

    // Se colocan los datos en una estructura
    strncpy(fullMessage.topic, 1+data[0], strlen(data[0])-2);
    fullMessage.topic[strlen(data[0])-2] = 0;
    sscanf(topicLen, "%d", &fullMessage.length);
    strcpy(fullMessage.message, data[2]);
    fullMessage.message[2*fullMessage.length] = 0;

    return fullMessage;
}
