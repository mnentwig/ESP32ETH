#include "UREPL_PWMDET.h"
#include "feature_nvsMan.h"
#include "feature_errMan.h"
#include "esp_log.h"
#include "dispatcher.h"
extern errMan_t errMan; // required feature (for dealing with incorrect input)
static const char *TAG = "PWMDET";

#define NCHANMAX 8
typedef struct {
  // number of active channels
  uint8_t nChan; 
  uint8_t GPIO[NCHANMAX];
} PWMDET_t;

PWMDET_t PWMDET; // singleton instance
void* PWMDET_getPayload(){ // retrieve singleton as opaque pointer (void* for use as payload arg)
  return (void*)&PWMDET;
}

void PWMDET_init(PWMDET_t* self){
  self->nChan = 0;
}
void PWMDET_handlerPrefix(dispatcher_t* disp, char* inp, void* payload){
  errMan_throwSYNTAX(&disp->errMan);
}
void PWMDET_handlerDoSet(dispatcher_t* disp, char* inp, void* payload){
  char* args[2];
  if (!dispatcher_getArgs(disp, inp, /*n*/2, args)) return;
  uint32_t gpio;
  if (!dispatcher_parseArg_UINT32(disp, args[0], &gpio))
    return;
  uint32_t rate_Hz;
  if (!dispatcher_parseArg_UINT32(disp, args[1], &rate_Hz))
    return;

  ESP_LOGI(TAG, "set %lu %lu", gpio, rate_Hz);
}
void PWMDET_handlerGet(dispatcher_t* disp, char* inp, void* payload){
  errMan_throwSYNTAX(&disp->errMan);
}
