#pragma once
#include <stddef.h> // size_t
typedef struct dispatcher_s dispatcher_t;
typedef struct {
  char msg[256];
  size_t errCount;  
} errMan_t;

void errMan_init(errMan_t* self);
void errMan_reportError(errMan_t* self, const char* msg);
int errMan_getError(errMan_t* self, const char** dest);
void errMan_clear(errMan_t* self);

// === standard errors ===
// unexpected number of arguments
void errMan_throwARG_COUNT(errMan_t* self);

// failure to parse argument as XYZ
void errMan_throwARG_NOT_IP(errMan_t* self);
void errMan_throwARG_NOT_UINT32(errMan_t* self);

// generic arg that didn't work e.g. invalid GPIO
void errMan_throwARG_INVALID(errMan_t* self);

// command not recognized
void errMan_throwSYNTAX(errMan_t* self);

// command too long (needs to fit into processing buffer)
void errMan_throwOVERFLOW(errMan_t* self);

// hardware resources not available
void errMan_throwRESOURCE(errMan_t* self);

// generic failure e.g. incorrectly configured
void errMan_throwFAIL(errMan_t* self);

// errMan response to ERR command at toplevel
void ERR_handlerPrefix(dispatcher_t* disp, char* inp, void* payload);
void ERR_handlerDoSet(dispatcher_t* disp, char* inp, void* payload);
void ERR_handlerGet(dispatcher_t* disp, char* inp, void* payload);
