idf_component_register(
    INCLUDE_DIRS .
    SRCS flexible_pipeline.c
    REQUIRES nvs_flash audio_pipeline audio_stream audio_hal esp_peripherals
)
