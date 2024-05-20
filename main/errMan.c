#include <string.h>

#include "errMan.h"
static const char *TAG = "errMan";
void errMan_init(errMan_t* self){
  self->errCount = 0;
  // self->mutex = xSemaphoreCreateMutex();
}

void errMan_reportError(errMan_t* self, const char* msg){
  // xSemaphoreTake(self->mutex, /*wait indefinitely*/portMAX_DELAY);

  if (!self->errCount) // only store first error
    strcpy(self->msg, msg);  
  ++self->errCount;

  // xSemaphoreGive(self->mutex);
}

int errMan_getError(errMan_t* self, const char** dest){
  int retVal = 0; // retval: no error
  // xSemaphoreTake(self->mutex, /*wait indefinitely*/portMAX_DELAY); // --------
  if (self->errCount){
    *dest = self->msg;
    retVal = self->errCount; // retval: number of logged errors since last readout
    self->errCount = 0;
  }
  
  // xSemaphoreGive(self->mutex); // ---------------------------------------------
  return retVal;
}

