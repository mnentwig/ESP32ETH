#pragma once
typedef struct {
  char msg[256];
  size_t errCount;  
  // SemaphoreHandle_t mutex; // errMan is thread-specific, no need for mutex
} errMan_t;

void errMan_init(errMan_t* self);
void errMan_reportError(errMan_t* self, const char* msg);
int errMan_getError(errMan_t* self, const char** dest);

