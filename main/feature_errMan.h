#pragma once
typedef struct {
  char msg[256];
  size_t errCount;  
} errMan_t;

void errMan_init(errMan_t* self);
void errMan_reportError(errMan_t* self, const char* msg);
int errMan_getError(errMan_t* self, const char** dest);
void errMan_clear(errMan_t* self);

// === standard errors ===
void errMan_throwARG_COUNT(errMan_t* self);
