/*  Flexible pipeline playback with different music

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "esp_log.h"
#include "sdkconfig.h"
#include "nvs_flash.h"

#include "board.h"
#include "filter_resample.h"
#include "esp_peripherals.h"
#include "periph_wifi.h"
#include "periph_sdcard.h"
#include "periph_button.h"
#include "periph_adc_button.h"
#include "periph_touch.h"

#include "audio_idf_version.h"
#include "rfid_reader.h"
#include "flexible_pipeline.h"

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
#include "esp_netif.h"
#else
#include "tcpip_adapter.h"
#endif

static const char *TAG = "main";
static esp_periph_set_handle_t set;

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("AUDIO_ELEMENT", ESP_LOG_DEBUG);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
    ESP_ERROR_CHECK(esp_netif_init());
#else
    tcpip_adapter_init();
#endif

    // Initialize peripherals management
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    set = esp_periph_set_init(&periph_cfg);

    // Initialize SD Card peripheral
    audio_board_sdcard_init(set, SD_MODE_1_LINE);

    // Initialize Button peripheral
    audio_board_key_init(set);

    // Setup audio codec
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);

    rdm6300_handle_t rdm6300_handle = rdm6300_init(13);
    ESP_LOGI(TAG, "LOOP");
    while(1)
    {
        uint64_t serial;
        enum rdm6300_sense_result sense_result = rdm630_sense(&rdm6300_handle, &serial);
        if(sense_result == RDM6300_SENSE_NEW_TAG)
        {
            ESP_LOGI(TAG, "NEW TAG: %" PRIu64, serial);
        }
        else if(sense_result == RDM6300_SENSE_TAG_LOST)
        {
            ESP_LOGI(TAG, "TAG LOST: %" PRIu64, serial);
        }
    }

    flexible_pipeline_playback(set);
    esp_periph_set_destroy(set);
}
