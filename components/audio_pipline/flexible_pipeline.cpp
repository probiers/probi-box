/*  Flexible pipeline playback with different music

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "flexible_pipeline.hpp"
extern "C" {
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_wifi.h"
#include "audio_element.h"
#include "audio_event_iface.h"
#include "audio_mem.h"
#include "audio_common.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"
#include "http_stream.h"
#include "mp3_decoder.h"
#include "wav_decoder.h"
#include "raw_stream.h"

#include "board.h"
#include "filter_resample.h"
#include "esp_peripherals.h"
#include "periph_wifi.h"
#include "periph_sdcard.h"
#include "periph_button.h"
#include "periph_adc_button.h"
#include "periph_touch.h"

#include "audio_idf_version.h"

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
#include "esp_netif.h"
#else
#include "tcpip_adapter.h"
#endif
}

static const char *TAG = "FLEXIBLE_PIPELINE";

#define SAVE_FILE_RATE      44100
#define SAVE_FILE_CHANNEL   2
#define SAVE_FILE_BITS      16

#define PLAYBACK_RATE       48000
#define PLAYBACK_CHANNEL    2
#define PLAYBACK_BITS       16

// Define your own event ID for starting and stopping the pipeline
#define MY_APP_START_EVENT_ID 1
#define MY_APP_STOP_EVENT_ID 2

#define RESAMPLE_FILTER_CONFIG() {          \
        .src_rate = 44100,                          \
        .src_ch = 2,                                \
        .dest_rate = 48000,                         \
        .dest_bits = 16,                            \
        .dest_ch = 2,                               \
        .src_bits = 16,                             \
        .mode = RESAMPLE_DECODE_MODE,               \
        .max_indata_bytes = RSP_FILTER_BUFFER_BYTE, \
        .out_len_bytes = RSP_FILTER_BUFFER_BYTE,    \
        .type = ESP_RESAMPLE_TYPE_AUTO,             \
        .complexity = 2,                            \
        .down_ch_idx = 0,                           \
        .prefer_flag = ESP_RSP_PREFER_TYPE_SPEED,   \
        .out_rb_size = RSP_FILTER_RINGBUFFER_SIZE,  \
        .task_stack = RSP_FILTER_TASK_STACK,        \
        .task_core = RSP_FILTER_TASK_CORE,          \
        .task_prio = RSP_FILTER_TASK_PRIO,          \
        .stack_in_ext = true,                       \
    }


audio_element_handle_t FlexiblePipeline::create_filter_upsample(int source_rate, int source_channel, int dest_rate, int dest_channel)
{
    rsp_filter_cfg_t rsp_cfg = RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = source_rate;
    rsp_cfg.src_ch = source_channel;
    rsp_cfg.dest_rate = dest_rate;
    rsp_cfg.dest_ch = dest_channel;
    return rsp_filter_init(&rsp_cfg);
}

audio_element_handle_t FlexiblePipeline::create_fatfs_stream(int sample_rates, int bits, int channels, audio_stream_type_t type)
{
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = type;
    audio_element_handle_t fatfs_stream = fatfs_stream_init(&fatfs_cfg);
    mem_assert(fatfs_stream);
    audio_element_info_t writer_info = {0};
    audio_element_getinfo(fatfs_stream, &writer_info);
    writer_info.bits = bits;
    writer_info.channels = channels;
    writer_info.sample_rates = sample_rates;
    audio_element_setinfo(fatfs_stream, &writer_info);
    return fatfs_stream;
}

audio_element_handle_t FlexiblePipeline::create_i2s_stream_writer(int sample_rates, int bits, int channels, audio_stream_type_t type)
{
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = type;
    audio_element_handle_t i2s_stream = i2s_stream_init(&i2s_cfg);
    mem_assert(i2s_stream);
    audio_element_info_t i2s_info = {0};
    audio_element_getinfo(i2s_stream, &i2s_info);
    i2s_info.bits = bits;
    i2s_info.channels = channels;
    i2s_info.sample_rates = sample_rates;
    audio_element_set_music_info(i2s_stream, i2s_info.sample_rates, i2s_info.channels, i2s_info.bits);
    return i2s_stream;
}

audio_element_handle_t FlexiblePipeline::create_mp3_decoder()
{
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    return mp3_decoder_init(&mp3_cfg);
}

audio_element_handle_t FlexiblePipeline::create_wav_decoder()
{
    wav_decoder_cfg_t wav_cfg = DEFAULT_WAV_DECODER_CONFIG();
    return wav_decoder_init(&wav_cfg);
}

static audio_element_handle_t create_http_stream(const char *url)
{
    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_cfg.multi_out_num = 1;
    audio_element_handle_t http_stream = http_stream_init(&http_cfg);
    mem_assert(http_stream);
    audio_element_set_uri(http_stream, url);
    return http_stream;
}

static audio_element_handle_t create_raw_stream()
{
    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_WRITER;
    audio_element_handle_t raw_stream = raw_stream_init(&raw_cfg);
    return raw_stream;
}

FlexiblePipeline::FlexiblePipeline(){
    pipeline_play = audio_pipeline_init(&pipeline_cfg);
    fatfs_reader_el = create_fatfs_stream(SAVE_FILE_RATE, SAVE_FILE_BITS, SAVE_FILE_CHANNEL, AUDIO_STREAM_READER);
    decoder_el = create_wav_decoder();
    filter_upsample_el = create_filter_upsample(SAVE_FILE_RATE, SAVE_FILE_CHANNEL, PLAYBACK_RATE, PLAYBACK_CHANNEL);
    i2s_stream_writer_el = create_i2s_stream_writer(PLAYBACK_RATE, PLAYBACK_BITS, PLAYBACK_CHANNEL, AUDIO_STREAM_WRITER);

    audio_pipeline_register(pipeline_play, fatfs_reader_el,  "file_reader");
    audio_pipeline_register(pipeline_play, decoder_el,       "decoder");
    audio_pipeline_register(pipeline_play, filter_upsample_el,   "filter_upsample");
    audio_pipeline_register(pipeline_play, i2s_stream_writer_el,        "i2s_writer");
    //audio_element_set_uri(fatfs_reader_el, "/sdcard/muecke_mit_beute.wav");
    audio_element_set_uri(fatfs_reader_el, "/sdcard/twinkle_twinkle.wav");

    ESP_LOGI(TAG, "Set up  i2s clock");
    i2s_stream_set_clk(i2s_stream_writer_el, PLAYBACK_RATE, PLAYBACK_BITS, PLAYBACK_CHANNEL);
    
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(pipeline_play, evt);

    evt_cmd = audio_event_iface_init(&evt_cfg);
    audio_event_iface_set_listener(evt_cmd, evt);

    ESP_LOGI(TAG, "Start playback pipeline");
    const char *link_tag[4] = {"file_reader", "decoder", "filter_upsample", "i2s_writer"};
    audio_pipeline_link(pipeline_play, &link_tag[0], 4);

}
FlexiblePipeline::~FlexiblePipeline(){
    audio_pipeline_stop(pipeline_play);
    audio_pipeline_wait_for_stop(pipeline_play);
    audio_pipeline_terminate(pipeline_play);
    audio_pipeline_unregister_more(pipeline_play, fatfs_reader_el,
                                   decoder_el,
                                   filter_upsample_el, i2s_stream_writer_el, NULL);
    audio_pipeline_remove_listener(pipeline_play);
    audio_event_iface_destroy(evt);

    audio_element_deinit(fatfs_reader_el);
    audio_element_deinit(decoder_el);
    audio_element_deinit(filter_upsample_el);
    audio_element_deinit(i2s_stream_writer_el);
    audio_pipeline_deinit(pipeline_play);
    

}
void FlexiblePipeline::loop(){

    ESP_LOGI(TAG, "Plan music!");
    audio_pipeline_run(pipeline_play);
    audio_event_iface_msg_t msg;
    while(1){
        ESP_LOGI(TAG, "LOOP");
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
            continue;
        }
        ESP_LOGI(TAG, "Receive event : %d", msg.cmd);

        if (msg.cmd == MY_APP_START_EVENT_ID) {
            audio_pipeline_pause(pipeline_play);
            audio_element_set_uri(fatfs_reader_el, "/sdcard/muecke_mit_beute.mp3");
            ESP_LOGI(TAG, "Changing music to %s", (char *)msg.data);
            audio_pipeline_run(pipeline_play);
            audio_pipeline_resume(pipeline_play);
        } else if(msg.cmd == MY_APP_STOP_EVENT_ID){
            audio_pipeline_pause(pipeline_play);
        }
        if (msg.need_free_data) {
            free(msg.data);
        }
    }
}

void FlexiblePipeline::start(std::string filename){
    ESP_LOGI(TAG, "Start");
    int data_size = filename.length()+1;
    void * data = malloc(data_size);
    strcpy((char *)data, filename.c_str());
    audio_event_iface_msg_t msg = {
        .cmd = MY_APP_START_EVENT_ID,
        .data = data,
        .data_len = data_size,
        .source = (void *)this,
        .source_type = 0,
        .need_free_data = true,
    };
    audio_event_iface_sendout(evt_cmd, &msg);
}

void FlexiblePipeline::stop(){

    audio_event_iface_msg_t msg = {
        .cmd = MY_APP_STOP_EVENT_ID,
        .data = NULL,
        .data_len = 0,
        .source = (void *)this,
        .source_type = 0,
        .need_free_data = false,
    };
    audio_event_iface_sendout(evt_cmd, &msg);
}

void FlexiblePipeline::pause(){
    audio_pipeline_pause(pipeline_play);

}

void FlexiblePipeline::resume(){
    audio_pipeline_resume(pipeline_play);

}