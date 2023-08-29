/*  Flexible pipeline playback with different music

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

extern "C" {
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "esp_log.h"
#include "esp_event.h"
#include "sdkconfig.h"
#include "nvs_flash.h"

#include "board.h"
#include "board_def.h"
#include "filter_resample.h"
#include "esp_peripherals.h"
#include "periph_sdcard.h"
#include "periph_button.h"
#include "periph_adc_button.h"
#include "periph_touch.h"

#include "audio_idf_version.h"
#include "rfid_reader.h"
#include "file_server.h"
#include "protocol_common.h"

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
#include "esp_netif.h"
#else
#include "tcpip_adapter.h"
#endif
#include <esp_pthread.h>

}
#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <string>
#include <sstream>

#include "flexible_pipeline.hpp"

static const char *TAG = "main";
static esp_periph_set_handle_t set;

esp_pthread_cfg_t create_config(const char *name, int core_id, int stack, int prio)
{
    auto cfg = esp_pthread_get_default_config();
    cfg.thread_name = name;
    cfg.pin_to_core = core_id;
    cfg.stack_size = stack;
    cfg.prio = prio;
    return cfg;
}

esp_err_t sdcard_init(esp_periph_set_handle_t set, periph_sdcard_mode_t mode)
{

    periph_sdcard_cfg_t sdcard_cfg = {
        .card_detect_pin = get_sdcard_intr_gpio(), // GPIO_NUM_34
        .root = "/sdcard",
        .mode = mode
    };
    esp_periph_handle_t sdcard_handle = periph_sdcard_init(&sdcard_cfg);
    esp_err_t ret = esp_periph_start(set, sdcard_handle);
    int retry_time = 5;
    bool mount_flag = false;
    while (retry_time --) {
        if (periph_sdcard_is_mounted(sdcard_handle)) {
            mount_flag = true;
            break;
        } else {
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }
    }
    if (mount_flag == false) {
        ESP_LOGE(TAG, "Sdcard mount failed");
        return ESP_FAIL;
    }
    return ret;
}

extern "C" void app_main(void)
{
     // Create a thread using deafult values that can run on any core
    auto cfg = esp_pthread_get_default_config();
    esp_pthread_set_cfg(&cfg);

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
    sdcard_init(set, SD_MODE_1_LINE);

    // Initialize Button peripheral
    audio_board_key_init(set);

    // Setup audio codec
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);

    int volume = 90;
    audio_hal_set_volume(board_handle->audio_hal, volume);
    ESP_LOGI(TAG, "[ * ] Receive music volume=%d",
                volume);


    rdm6300_handle_t rdm6300_handle = rdm6300_init(13);
    FlexiblePipeline flexible_pipeline{};
    std::thread any_core([&](){flexible_pipeline.loop();});
    std::thread wifi([&]{

        esp_event_loop_create_default();
        event_loop_run = true;
        ESP_ERROR_CHECK(example_connect());
        while(1)
        {   
            // TODO check if event_loop_run is atomic
            if(!event_loop_run)
            {
                ESP_LOGI(TAG, "LOOP BREAK");
                break;
            }
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    });

    std::thread file_server([&]{
        example_start_file_server("/sdcard");});
    ESP_LOGI(TAG, "LOOP");
    uint64_t old_serial = 0;
    while(1)
    {
        uint64_t serial = 0;
        enum rdm6300_sense_result sense_result = rdm630_sense(&rdm6300_handle, &serial);
        if(sense_result == RDM6300_SENSE_NEW_TAG)
        {
            ESP_LOGI(TAG, "NEW TAG: %" PRIu64, serial);
            if (old_serial != serial) {
                flexible_pipeline.stop();
                flexible_pipeline.start(std::to_string(serial));
                old_serial = serial;
            } else{
                flexible_pipeline.resume();
            }
        }
        else if(sense_result == RDM6300_SENSE_TAG_LOST)
        {
            ESP_LOGI(TAG, "TAG LOST: %" PRIu64, serial);
            flexible_pipeline.pause();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    esp_periph_set_stop_all(set);
    esp_periph_set_destroy(set);
}
