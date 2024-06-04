#include "UREPL_PWM.h"
#include "feature_errMan.h"
#include "esp_err.h" // ESP_ERROR_CHECK
#include "esp_log.h"
#include "driver/ledc.h"
#include "dispatcher.h"
#include "string.h" // memset

static ledc_timer_config_t cTimer = {
    .speed_mode       = LEDC_HIGH_SPEED_MODE,
    .timer_num        = LEDC_TIMER_0,
    .duty_resolution  = LEDC_TIMER_16_BIT,
    .freq_hz          = 50,
    .clk_cfg          = LEDC_AUTO_CLK
};
  
  ledc_channel_config_t cChan = {
    .speed_mode     = LEDC_HIGH_SPEED_MODE,
    .channel        = LEDC_CHANNEL_0,
    .timer_sel      = LEDC_TIMER_0,
    .intr_type      = LEDC_INTR_DISABLE,
    .gpio_num       = 5,
    .duty           = 0x1000, // duty cycle
    .hpoint         = 0 // high level at start of cycle
  };

extern errMan_t errMan; // required feature (for dealing with incorrect input)
static const char *TAG = "PWM";

void PWM_init(){
	ESP_ERROR_CHECK(ledc_timer_config(&cTimer));
	ESP_ERROR_CHECK(ledc_channel_config(&cChan));
	ESP_LOGI(TAG, "PWM_init() done");
}

void GPIO_handlerGet(dispatcher_t* disp, char* inp, void* payload){
  if (!dispatcher_getArgsNull(disp, inp)) return;
  char buf[16];	
  sprintf(buf, "%lu", (uint32_t)cChan.gpio_num);
  dispatcher_connWriteCString(disp, buf);
}

void GPIO_handlerDoSet(dispatcher_t* disp, char* inp, void* payload){
  char* args[1];
  if (!dispatcher_getArgs(disp, inp, /*n*/1, args)) return;
  uint32_t gpio;
  if (!dispatcher_parseArg_UINT32(disp, args[0], &gpio))
    return;
  ledc_channel_config_t prev = cChan;
  cChan.gpio_num = gpio;
  if (ledc_channel_config(&cChan) == ESP_OK) 
	  return;

  // === fallback to known-good value ===
  cChan = prev;
  ESP_ERROR_CHECK(ledc_channel_config(&cChan));
  errMan_throwARG_INVALID(&disp->errMan);  
}

void FREQ_handlerGet(dispatcher_t* disp, char* inp, void* payload){
  if (!dispatcher_getArgsNull(disp, inp)) return;
  char buf[16];	
  sprintf(buf, "%lu", cTimer.freq_hz);
  dispatcher_connWriteCString(disp, buf);
}

void FREQ_handlerDoSet(dispatcher_t* disp, char* inp, void* payload){
  char* args[1];
  if (!dispatcher_getArgs(disp, inp, /*n*/1, args)) return;
  uint32_t freq;
  if (!dispatcher_parseArg_UINT32(disp, args[0], &freq))
    return;
  ledc_timer_config_t prev = cTimer;
  cTimer.freq_hz = freq;
  if (ledc_timer_config(&cTimer) == ESP_OK)
	  return;
  cTimer = prev;
  ESP_ERROR_CHECK(ledc_timer_config(&cTimer));
  errMan_throwARG_INVALID(&disp->errMan);  
}


//  ESP_ERROR_CHECK(ledc_set_duty(LEDC_HIGH_SPEED_MODE, ixServoOutChan, val));
//  ESP_ERROR_CHECK(ledc_update_duty(LEDC_HIGH_SPEED_MODE, ixServoOutChan));



static dispatcherEntry_t PWM_dispEntries[] = {
  {.key="GPIO", .handlerPrefix=NULL, .handlerDoSet=GPIO_handlerDoSet, .handlerGet=GPIO_handlerGet, .payload=NULL},
  {.key="FREQ", .handlerPrefix=NULL, .handlerDoSet=FREQ_handlerDoSet, .handlerGet=FREQ_handlerGet, .payload=NULL},
  {.key=NULL, .handlerPrefix=NULL, .handlerDoSet=NULL, .handlerGet=NULL, .payload=NULL} // end marker
};

void PWM_handlerGet(dispatcher_t* disp, char* inp, void* payload){
  errMan_throwSYNTAX(&disp->errMan);
}

void PWM_handlerDoSet(dispatcher_t* disp, char* inp, void* payload){
  errMan_throwSYNTAX(&disp->errMan);
}

void PWM_handlerPrefix(dispatcher_t* disp, char* inp, void* payload){
  if (!dispatcher_exec(disp, inp, PWM_dispEntries))
    errMan_throwSYNTAX(&disp->errMan);        
}

