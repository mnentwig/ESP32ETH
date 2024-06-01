#include "UREPL_PWM.h"
#include "feature_nvsMan.h"
#include "feature_errMan.h"
#include "esp_err.h" // ESP_ERROR_CHECK
#include "esp_log.h"
#include "dispatcher.h"
#include "string.h" // memset

#include "driver/ledc.h"

extern errMan_t errMan; // required feature (for dealing with incorrect input)
static const char *TAG = "PWM";

#define NGPIO 32
#define NTIMERS 4 // LEDC high speed timers only
#define PINOUT 0x80
#define PININ 0x40
#define INV 0xFF
typedef struct {
  uint8_t alloc[NGPIO];
} PWM_t;

PWM_t PWM; // singleton instance

void PWM_init(){
  memset((void*)&PWM.alloc, 0, sizeof(PWM.alloc));

  // start timer 0 for all input pins
  ledc_timer_config_t ledc_timer = {
    .speed_mode       = LEDC_HIGH_SPEED_MODE,
    .timer_num        = LEDC_TIMER_0,
    .duty_resolution  = LEDC_TIMER_16_BIT,
    .freq_hz          = 50,
    .clk_cfg          = LEDC_AUTO_CLK
  };
  ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
}

// gets lowest timer-/channel combination that is not used by any output
static uint8_t nextFreeTimerChan(){
  for (size_t ixTimer = 0; ixTimer < NTIMERS; ++ixTimer){
    for (size_t ixChan = 0; ixChan < 2; ++ixChan){
      int busy = 0;
      uint8_t v = (ixTimer << 1) | ixChan;
      for (size_t ixGPIO = 0; ixGPIO < NGPIO; ++ixGPIO){
	if ((PWM.alloc[ixGPIO] & 0x0F) == v){
	  busy = 1;
	  break;
	}
      } // loop over GPIOs
      if (!busy)
	return v;
    } // loop over chan
  } // loop over timer
  return INV;
}

static void stop(size_t gpio){
  uint8_t c = PWM.alloc[gpio];
  if ((c & (PININ | PINOUT)) == 0)
    return;
  PWM.alloc[gpio] = 0;
  size_t chan = (c & 0x0F) >> 1;
  ESP_ERROR_CHECK(ledc_stop(LEDC_HIGH_SPEED_MODE, chan, /*idle_level*/0));
}

void PININ_handlerDoSet(dispatcher_t* disp, char* inp, void* payload){
  char* args[1];
  if (!dispatcher_getArgs(disp, inp, /*n*/1, args)) return;
  uint32_t gpio;
  if (!dispatcher_parseArg_UINT32(disp, args[0], &gpio))
    return;

  ESP_LOGI(TAG, "PININ %lu", gpio);
  stop(gpio);
}

void PINOUT_handlerDoSet(dispatcher_t* disp, char* inp, void* payload){
  char* args[1];
  if (!dispatcher_getArgs(disp, inp, /*n*/1, args)) return;
  uint32_t gpio;
  if (!dispatcher_parseArg_UINT32(disp, args[0], &gpio))
    return;

  ESP_LOGI(TAG, "PINOUT %lu", gpio);
  stop(gpio);

  // === locate free timer ===
  uint8_t c = nextFreeTimerChan();
  if (c == INV){
    errMan_throwRESOURCE(&disp->errMan);
    return;
  }

  size_t ixChan = c & 0x01;
  size_t ixTimer = (c & 0x0F) >> 1;

  // note: timer gets reconfigured, if already used by some channel.
  ledc_timer_config_t ledc_timer = {
    .speed_mode       = LEDC_HIGH_SPEED_MODE,
    .timer_num        = LEDC_TIMER_0 + ixTimer,
    .duty_resolution  = LEDC_TIMER_16_BIT,
    .freq_hz          = 50,
    .clk_cfg          = LEDC_AUTO_CLK
  };
  ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
  
  ledc_channel_config_t ledc_channel = {
    .speed_mode     = LEDC_HIGH_SPEED_MODE,
    .channel        = LEDC_CHANNEL_0 + ixChan,
    .timer_sel      = LEDC_TIMER_0 + ixTimer,
    .intr_type      = LEDC_INTR_DISABLE,
    .gpio_num       = gpio,
    .duty           = 0, // 0 duty cycle, still off
    .hpoint         = 0 // high level at start of cycle
  };
  ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));  
}

void PINCLR_handlerDoSet(dispatcher_t* disp, char* inp, void* payload){
  char* args[1];
  if (!dispatcher_getArgs(disp, inp, /*n*/1, args)) return;
  uint32_t gpio;
  if (!dispatcher_parseArg_UINT32(disp, args[0], &gpio))
    return;

  ESP_LOGI(TAG, "PININ %lu", gpio);
  stop(gpio);
}

void PWM_handlerGet(dispatcher_t* disp, char* inp, void* payload){
  errMan_throwSYNTAX(&disp->errMan);
}

void PWM_handlerDoSet(dispatcher_t* disp, char* inp, void* payload){
  errMan_throwSYNTAX(&disp->errMan);
}

 static dispatcherEntry_t PWM_dispEntries[] = {
  {.key="PININ", .handlerPrefix=NULL, .handlerDoSet=PININ_handlerDoSet, .handlerGet=NULL, .payload=NULL},
  {.key="PINOUT", .handlerPrefix=NULL, .handlerDoSet=PINOUT_handlerDoSet, .handlerGet=NULL, .payload=NULL},
  {.key="PINCLR", .handlerPrefix=NULL, .handlerDoSet=PINCLR_handlerDoSet, .handlerGet=NULL, .payload=NULL},
  {.key=NULL, .handlerPrefix=NULL, .handlerDoSet=NULL, .handlerGet=NULL, .payload=NULL} // end marker
};

void PWM_handlerPrefix(dispatcher_t* disp, char* inp, void* payload){
  if (!dispatcher_exec(disp, inp, PWM_dispEntries))
    errMan_throwSYNTAX(&disp->errMan);        
}
