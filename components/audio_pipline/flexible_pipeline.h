#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_peripherals.h"

#ifdef __cplusplus
extern "C" {
#endif

void flexible_pipeline_playback(esp_periph_set_handle_t set);

#ifdef __cplusplus
}
#endif