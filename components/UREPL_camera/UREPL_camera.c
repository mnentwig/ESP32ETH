#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_camera.h"

#include "dispatcher.h"

#define BOARD_ESP32CAM_AITHINKER 1

// WROVER-KIT PIN Map
#ifdef BOARD_WROVER_KIT

#define CAM_PIN_PWDN -1  //power down is not used
#define CAM_PIN_RESET -1 //software reset will be performed
#define CAM_PIN_XCLK 21
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27

#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 19
#define CAM_PIN_D2 18
#define CAM_PIN_D1 5
#define CAM_PIN_D0 4
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

#endif

// ESP32Cam (AiThinker) PIN Map
#ifdef BOARD_ESP32CAM_AITHINKER

#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1 //software reset will be performed
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27

#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

#endif

static const char *TAG = "UREPL_camera";

#if ESP_CAMERA_SUPPORTED
#else
#error need ESP_CAMERA_SUPPORTED in config
#endif
static camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    //XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG, //YUV422,GRAYSCALE,RGB565,JPEG

    // FRAMESIZE_UXGA (1600 x 1200)
    // FRAMESIZE_QVGA (320 x 240)
    // FRAMESIZE_CIF (352 x 288)
    // FRAMESIZE_VGA (640 x 480)
    // FRAMESIZE_SVGA (800 x 600)
    // FRAMESIZE_XGA (1024 x 768)
    // FRAMESIZE_SXGA (1280 x 1024)
    // QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
    .frame_size = FRAMESIZE_XGA,    //QQVGA-UXGA, For ESP32, do not use sizes above QVGA when not JPEG. The performance of the ESP32-S series has improved a lot, but JPEG mode always gives better frame rates.

    .jpeg_quality = 10, //0-63, for OV series camera sensors, lower number means higher quality
    .fb_count = 1,       //When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};

void camera_init(void){
  if (esp_camera_init(&camera_config) != ESP_OK){
    ESP_LOGE(TAG, "esp_camera_init()");
    ESP_ERROR_CHECK(ESP_FAIL);
  }
}

static void CAPT_handlerGet(dispatcher_t* disp, char* inp, void* payload){
  if (!dispatcher_getArgsNull(disp, inp)) return;
  
  ESP_LOGI(TAG, "CAPT?");
  camera_fb_t *pic = esp_camera_fb_get();
  
  //ESP_LOGI(TAG, "pic size: %zu bytes", pic->len);
  dispatcher_connWriteBinaryHeader(disp, pic->len);
  dispatcher_connWrite(disp, (char*)pic->buf, pic->len);
  //ESP_LOGI(TAG, "written");
  
  esp_camera_fb_return(pic);
}

static dispatcherEntry_t camera_dispEntries[] = {
  {.key="CAPT", .handlerPrefix=NULL, .handlerDoSet=NULL, .handlerGet=CAPT_handlerGet, .payload=NULL},
  {.key=NULL, .handlerPrefix=NULL, .handlerDoSet=NULL, .handlerGet=NULL, .payload=NULL} // end marker
};

void CAM_handlerPrefix(dispatcher_t* disp, char* inp, void* payload){
  if (!dispatcher_exec(disp, inp, camera_dispEntries))
    errMan_throwSYNTAX(&disp->errMan);        
}

