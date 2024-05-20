#pragma once
#include <stdint.h> // uint32_t
#include "freertos/FreeRTOS.h"
#include "nvs.h"
typedef struct nvsMan_s {
  nvs_handle_t handle;
  SemaphoreHandle_t mutex;
} nvsMan_t;

void nvsMan_init(nvsMan_t* self);
uint32_t nvsMan_get_u32(nvsMan_t* self, const char* key);
