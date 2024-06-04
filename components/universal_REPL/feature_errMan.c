#include <string.h> // strcpy
#include "esp_log.h" // ESP_LOGI

#include "feature_errMan.h"
#include "dispatcher.h"
static const char *TAG = "errMan";
void errMan_init(errMan_t* self){
  self->errCount = 0;
}

void errMan_reportError(errMan_t* self, const char* msg){
  if (!self->errCount){ // only store first error
    assert(strlen(msg) <= sizeof(self->msg)-1);
    strcpy(self->msg, msg);
    ESP_LOGI(TAG, "setting first error (%s)", msg);
  } else {
    ESP_LOGI(TAG, "setting additional error (%s)", msg);
  }
  ++self->errCount;
}

int errMan_getError(errMan_t* self, const char** dest){
  int retVal = 0; // retval: no error
  if (self->errCount){
    *dest = self->msg;
    retVal = self->errCount; // retval: number of logged errors since last readout
  }
  errMan_clear(self);
  return retVal;
}

void errMan_clear(errMan_t* self){
  self->errCount = 0;    
}

void errMan_throwARG_COUNT(errMan_t* self){
  errMan_reportError(self, "ARG_COUNT");
}

void errMan_throwARG_NOT_IP(errMan_t* self){
  errMan_reportError(self, "ARG_NOT_IP");
}

void errMan_throwARG_NOT_UINT32(errMan_t* self){
  errMan_reportError(self, "ARG_NOT_UINT32");
}

void errMan_throwARG_INVALID(errMan_t* self){
  errMan_reportError(self, "ARG_INVALID");
}

void errMan_throwSYNTAX(errMan_t* self){
  errMan_reportError(self, "SYNTAX");
}

void errMan_throwOVERFLOW(errMan_t* self){
  errMan_reportError(self, "OVERFLOW");
}

void errMan_throwRESOURCE(errMan_t* self){
  errMan_reportError(self, "RESOURCE");
}

void ERR_handlerPrefix(dispatcher_t* disp, char* inp, void* payload){
  errMan_t* self = &disp->errMan;
  errMan_throwSYNTAX(self); // not yet implemented
}

void ERR_handlerDoSet(dispatcher_t* disp, char* inp, void* payload){
  errMan_t* self = &disp->errMan;
  char* args[1];
  if (!dispatcher_getArgs(disp, inp, /*n*/1, args))
    return;
  errMan_reportError(self, args[0]);
}

void ERR_handlerGet(dispatcher_t* disp, char* inp, void* payload){
  errMan_t* self = &disp->errMan;
  if (!dispatcher_getArgsNull(disp, inp)) return;
  if (self->errCount){
    dispatcher_connWriteCString(disp, self->msg);
    self->errCount = 0;    
  } else {
    dispatcher_connWriteCString(disp, "NO_ERROR");    
  }
}
