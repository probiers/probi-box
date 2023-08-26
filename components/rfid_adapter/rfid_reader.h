#pragma once

#include <stdint.h>
#include <stddef.h>
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct {
    char serial[129];
    size_t pos;
    int state;
    uint64_t last_seen_serial;
    uint64_t time_serial_last_seen;
    QueueHandle_t uart_queue;
} rdm6300_handle_t;

enum rdm6300_sense_result
{
    RDM6300_SENSE_NEW_TAG,
    RDM6300_SENSE_TAG_LOST,
    RDM6300_SENSE_NO_CHANGE,
};

rdm6300_handle_t rdm6300_init(int pin);

enum rdm6300_sense_result rdm630_sense(rdm6300_handle_t * handle, uint64_t * serial);

#ifdef __cplusplus
}
#endif