#include <string.h>

#include "errMan.h"
//static const char *TAG = "errMan";
void errMan_init(errMan_t* self){
  self->errCount = 0;
}

void errMan_reportError(errMan_t* self, const char* msg){
  if (!self->errCount) // only store first error
    strcpy(self->msg, msg);  
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
