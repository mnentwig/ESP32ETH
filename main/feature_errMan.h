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
// unexpected number of arguments
void errMan_throwARG_COUNT(errMan_t* self);

// command not recognized
void errMan_throwSYNTAX(errMan_t* self);

// command too long (needs to fit into processing buffer)
void errMan_throwOVERFLOW(errMan_t* self);
