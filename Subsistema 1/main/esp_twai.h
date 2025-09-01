#include "driver/twai.h"
#include "at_mqtt.h"

struct pidListStruct {
    int length;
    uint8_t data[128];
};

void create_twai(void);
void request_pids();
void request_vin();
void request_dtc();
