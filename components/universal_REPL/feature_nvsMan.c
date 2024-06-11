#include "nvs_flash.h"
#include "esp_log.h" // ESP_LOGX
#include "esp_err.h" // ESP_ERROR_CHECK
#include "esp_netif.h" // esp_ip4addr_aton
#include "feature_nvsMan.h"
static const char *TAG = "nvsMan";
uint32_t nvsMan_get_u32(nvsMan_t* self, const char* key){
  uint32_t retVal;
  xSemaphoreTake(self->mutex, /*wait indefinitely*/portMAX_DELAY);
  esp_err_t err = nvs_get_u32(self->handle, key, &retVal);
  xSemaphoreGive(self->mutex);

  if (err != ESP_OK){
    ESP_LOGE(TAG, "nvs_get_u32");
    ESP_ERROR_CHECK(ESP_FAIL);
  }
  
  return retVal;
}

void nvsMan_set_u32(nvsMan_t* self, const char* key, uint32_t val){
  xSemaphoreTake(self->mutex, /*wait indefinitely*/portMAX_DELAY);
  esp_err_t err = nvs_set_u32(self->handle, key, val);
  if (err != ESP_OK){
    ESP_LOGE(TAG, "nvs_set_u32");
    ESP_ERROR_CHECK(ESP_FAIL);
  }
  err = nvs_commit(self->handle);
  if (err != ESP_OK){
    ESP_LOGE(TAG, "nvs_commit");
    ESP_ERROR_CHECK(ESP_FAIL);
  }      
  xSemaphoreGive(self->mutex);
}

// if not found, returns "".
// caller must free() returned value.
char* nvsMan_get_str(nvsMan_t* self, const char* key){
  char* retval;
  xSemaphoreTake(self->mutex, /*wait indefinitely*/portMAX_DELAY);
  size_t length;
  ESP_LOGI(TAG, "get_str '%s'", key);
  if (ESP_OK != nvs_get_str(self->handle, key, /*out_value*/NULL, &length)) goto fail_returnBlank;
  
  retval = (char*)calloc(length, sizeof(char));
  ESP_ERROR_CHECK(nvs_get_str(self->handle, key, /*out_value*/retval, &length));
  ESP_LOGI(TAG, "returns '%s'", retval);
  goto done;
  
 fail_returnBlank:
  retval = (char*)calloc(1, sizeof(char)); // single \0 char
 done:
  xSemaphoreGive(self->mutex);
  return retval; 
}

void nvsMan_set_str(nvsMan_t* self, const char* key, const char* str){
  xSemaphoreTake(self->mutex, /*wait indefinitely*/portMAX_DELAY);
  ESP_LOGI(TAG, "set_str '%s' => '%s'", key, str);
  ESP_ERROR_CHECK(nvs_set_str(self->handle, key, str));
  xSemaphoreGive(self->mutex);
}

static void nvsMan_initializeBlankField_u32(nvsMan_t* self, const char* key, uint32_t val){
  uint32_t dummyVal;
  xSemaphoreTake(self->mutex, /*wait indefinitely*/portMAX_DELAY);
  esp_err_t err = nvs_get_u32(self->handle, key, &dummyVal);
  xSemaphoreGive(self->mutex);

  switch (err) {
  case ESP_OK:
    ESP_LOGI(TAG, "'%s' (u32) found in nvs", key);
    return;
  case ESP_ERR_NVS_NOT_FOUND:
    ESP_LOGI(TAG, "'%s' (u32) not found in nvs, setting to default", key);
    break;
  default :
    ESP_LOGE(TAG, "nvs_get_u32 (%s)", esp_err_to_name(err));
    ESP_ERROR_CHECK(ESP_FAIL);    
  }

  nvsMan_set_u32(self, key, val);
}

static void util_printIp(char* buf, uint32_t ip){
  sprintf(buf, "%d.%d.%d.%d", (int)(ip >> 0) & 0xFF, (int)(ip >> 8) & 0xFF, (int)(ip >> 16) & 0xFF, (int)(ip >> 24) & 0xFF);
}

void nvsMan_init(nvsMan_t* self){
  self->mutex = xSemaphoreCreateMutex();

  // ==============================================================================
  ESP_LOGI(TAG, "initializing NVS");
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated and needs to be erased
    // Retry nvs_flash_init
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK( err );
  
  // ==============================================================================
  ESP_LOGI(TAG, "opening NVS");
  err = nvs_open("storage", NVS_READWRITE, &self->handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nvs_open (%s)", esp_err_to_name(err));
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  nvsMan_initializeBlankField_u32(self, "ip", esp_ip4addr_aton("192.168.178.123"));
  nvsMan_initializeBlankField_u32(self, "gw", esp_ip4addr_aton("192.168.178.1"));
  nvsMan_initializeBlankField_u32(self, "netmask", esp_ip4addr_aton("255.255.255.0"));
  
  char buf[20];
  util_printIp(buf, nvsMan_get_u32(self, "ip"));
  ESP_LOGI(TAG, "ip: %s", buf);
  util_printIp(buf, nvsMan_get_u32(self, "netmask"));
  ESP_LOGI(TAG, "netmask: %s", buf);
  util_printIp(buf, nvsMan_get_u32(self, "gw"));
  ESP_LOGI(TAG, "gw: %s", buf);
}

