#include <assert.h>
#include "feature_UART.h"
//static const char *TAG = "feature_UART";

static void UART_BAUDRATE_handlerDoSet(dispatcher_t* disp, char* inp, void* payload){assert(0);}
static void UART_BAUDRATE_handlerGet(dispatcher_t* disp, char* inp, void* payload){assert(0);}
static void UART_START_handlerDoSet(dispatcher_t* disp, char* inp, void* payload){assert(0);}

// === command parser: UART ===
static dispatcherEntry_t UART_dispEntries[] = {
  {.key="BAUDRATE", .handlerPrefix=NULL, .handlerDoSet=UART_BAUDRATE_handlerDoSet, .handlerGet=UART_BAUDRATE_handlerGet, .payload=NULL},
  {.key="START", .handlerPrefix=NULL, .handlerDoSet=UART_START_handlerDoSet, .handlerGet=NULL, .payload=NULL},
  {.key=NULL, .handlerPrefix=NULL, .handlerDoSet=NULL, .handlerGet=NULL, .payload=NULL} // end marker
};

void UART_handlerPrefix(dispatcher_t* disp, char* inp, void* payload){
  //  ESP_LOGI(TAG, "dummy log");
  
  dispatcher_exec(disp, inp, UART_dispEntries);
}
