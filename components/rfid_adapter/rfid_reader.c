/* Play MP3 file from SD Card

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "rc522.h" // FIXME: cannot remove this, otherwise I get LOGI errors

static const char *TAG = "RFID_READER";
#include "esp_system.h"
#include "rfid_reader.h"

/// initialize rdm6300 driver using uart2
/// Only a single pin is needed connected to the TX pin of the rdm6300
/// lots of things hard-coded and unnecessarily huge buffers
rdm6300_handle_t rdm6300_init(int pin)
{
    const uart_port_t uart_num = UART_NUM_2;
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };
    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, UART_PIN_NO_CHANGE, pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    // Setup UART buffered IO with event queue
    const int uart_buffer_size = (1024 * 2);
    //QueueHandle_t uart_queue;
    rdm6300_handle_t handle = {.state = 0, .pos = 0, .last_seen_serial = 0, .time_serial_last_seen = 0};
    // Install UART driver using an event queue here
    ESP_ERROR_CHECK(
        uart_driver_install(
        UART_NUM_2,
        uart_buffer_size,
        uart_buffer_size,
        10,
        &(handle.uart_queue),
        0
        )
        );
    return handle;
}

/// receives data from the rdm6300 and returns the serial number of the tag if present
/// returns RDM6300_SENSE_NEW_TAG if a new tag was detected
/// returns RDM6300_SENSE_TAG_LOST if a tag was lost
/// returns RDM6300_SENSE_NO_CHANGE if no change was sensed since last call. NOTE: this could either mean tag is still present or no tag is present, depending on last returned sense result.
enum rdm6300_sense_result rdm630_sense(rdm6300_handle_t * handle, uint64_t * serial)
{
    enum rdm6300_sense_result result = RDM6300_SENSE_NO_CHANGE;
    const uart_port_t uart_num = UART_NUM_2;
    uint8_t data[128];
    int length = 128;
    //ESP_LOGI(TAG, " start rx: %" PRIu64, esp_timer_get_time());
    //length = uart_read_bytes(uart_num, data, length, 100);
    length = uart_read_bytes(uart_num, data, length, 1);
    //ESP_LOGI(TAG, " end rx: %" PRIu64, esp_timer_get_time());
    for(int i = 0; i < length; i++)
    {
        uint8_t byte = data[i];
        switch(handle->state)
        {
            case 0: // wait for start
                if(byte == 0x02)
                {
                    handle->state = 1;
                }
                break;
            case 1: // reading data
                // FIXME: only works when we receive one message per call...
                if(byte == 0x03) // end received
                {
                    handle->serial[handle->pos] = '\0';
                    uint64_t intserial = strtoull(handle->serial, NULL, 16);
                    if(handle->last_seen_serial != intserial)
                    {
                        result = RDM6300_SENSE_NEW_TAG;
                        *serial = intserial;
                        handle->last_seen_serial = intserial;
                    }
                    handle->time_serial_last_seen = esp_timer_get_time();

                    //ESP_LOGI(TAG, "serial: %s", handle->serial);

                    handle->state = 0;
                    handle->pos = 0;
                    break;
                }
                if(handle->pos + 1 > 128)
                {
                    handle->state = 0;
                    handle->pos = 0;
                    break;
                }
                handle->serial[handle->pos++] = byte;
                break;
        }
    }
    if((handle->last_seen_serial != 0) && (esp_timer_get_time() - handle->time_serial_last_seen > 200000))
    {
        *serial = handle->last_seen_serial;
        result = RDM6300_SENSE_TAG_LOST;
        handle->last_seen_serial = 0;
    }
    return result;
}
