#pragma once

extern "C" {
#include <stdint.h>
#include <stddef.h>
#include "esp_peripherals.h"
#include "audio_pipeline.h"
}

#include <string>
#include <vector>
#include <map>
#include <mutex>

class FlexiblePipeline
{
  public:
    FlexiblePipeline();
    ~FlexiblePipeline();

    void loop();
    void start(std::string&& filename);
    void stop();
    void pause();
    void resume();

    static audio_element_handle_t create_fatfs_stream(int sample_rates, int bits, int channels, audio_stream_type_t type);
    static audio_element_handle_t create_mp3_decoder();
    static audio_element_handle_t create_aac_decoder();
    static audio_element_handle_t create_wav_decoder();
    static audio_element_handle_t create_filter_upsample(int source_rate, int source_channel, int dest_rate, int dest_channel);
    static audio_element_handle_t create_i2s_stream_writer(int sample_rates, int bits, int channels, audio_stream_type_t type);

  private:
    enum class DecoderType{
        MP3,
        ACC,
        WAV
    };
    void link_pipeline(DecoderType type);
    void stop_pipeline();
    void play_file(const char* filename);
    DecoderType getFileType(const char* filename);
    void add_element(const char* name, audio_element_handle_t handle, bool link = true);

    void playlist_read(std::string& playlist_name);
    std::string playlist_next();
    /// Empty string if playlist is empty or ended
    std::string playlist_current_song();

    audio_pipeline_handle_t pipeline_play = NULL;
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    std::map<const std::string, audio_element_handle_t> handle_elements;
    std::vector<const char*> link_tags;
    audio_event_iface_handle_t evt = NULL;
    audio_event_iface_handle_t evt_cmd = NULL;

    std::vector <std::string> playlist;
    int playlist_index = 0;
    std::string curr_playlist_name = "";
    std::mutex playlist_mutex;

    
};
