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
void nvsMan_set_u32(nvsMan_t* self, const char* key, uint32_t val);

 // caller must free return value
char* nvsMan_get_str(nvsMan_t* self, const char* key);
void nvsMan_set_str(nvsMan_t* self, const char* key, const char* str);
