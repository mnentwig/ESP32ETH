#pragma once
#include <stddef.h> // size
#include <stdint.h> // size
#include "feature_errMan.h"

typedef struct dispatcher_s dispatcher_t;

// handler function called on identified command. Note, input buffer is modified ('\0' token separation)
// payload arg can be optionally used to identify the subsystem (typical use case: If subsystem is replicated, e.g. appears in multiple "channels" or the like)
typedef void (*dispatcherFun_t)(dispatcher_t* disp, char* input, void* payload);

// - handlerPrefix is called when input starts with key plus ":" e.g. "ETH:" 
// - handlerDoSet is called when input equals key e.g. "IP"
// - handlerGet is called when input equals key with an additional question mark e.g. "IP?"
typedef struct {
  const char* key;
  dispatcherFun_t handlerPrefix;
  dispatcherFun_t handlerDoSet;
  dispatcherFun_t handlerGet;
  void* payload;
} dispatcherEntry_t;

// connection-specific function to read more data (supports binary)
typedef int(*dispatcher_readFun_t)(void* readFunArg, char* data, size_t nMax);
// connection-specific function to write reply data (supports binary)
typedef void(*dispatcher_writeFun_t)(void* writeFunArg, const char* data, size_t n);

typedef struct dispatcher_s{
  errMan_t errMan;
  void* appObj;
  void* connSpecArg;
  dispatcher_writeFun_t writeFn;
  dispatcher_readFun_t readFn;
  int connectState;
} dispatcher_t;

void dispatcher_init(dispatcher_t* self, void* appObj, dispatcher_writeFun_t writeFnc, dispatcher_readFun_t readFnc, void* connSpecArg);

int dispatcher_getConnectState(dispatcher_t* self);
void dispatcher_setConnectState(dispatcher_t* self, int connectState);

// called by command feed for parsing
// returns 1 if command was recognized, 0 otherwise
int dispatcher_exec
(dispatcher_t* self, // dispatcher state e.g. funcptr to write return data
 char* input, // input data start. Note, contents get modified ('\0' insertion)
 dispatcherEntry_t* dispEntries // NULL-terminated list of keywords to match (one level of parse tree)
 );

// === argument processing functions ===
// return 1 for success
// 0 otherwise, with appropriate error in errMan
// note: input is modified (string terminations)
int dispatcher_getArgsNull(dispatcher_t* self, char* inp);

// returns 1 for success
// 0 otherwise, with appropriate error in errMan
int dispatcher_getArgs(dispatcher_t* self, char* inp, size_t n, char** args);

int dispatcher_parseArg_IP(dispatcher_t* self, char* inp, uint32_t* outp);
int dispatcher_parseArg_UINT32(dispatcher_t* self, char* inp, uint32_t* outp);

// === connection access ===

// reads from connection, between 1 and nMax bytes (blocking)
size_t dispatcher_connRead(dispatcher_t* self, char* buf, size_t nMax);

// writes data to connection (blocking)
void dispatcher_connWrite(dispatcher_t* self, const char* buf, size_t nBytes);
// shorthand for above on C string, adds '\n' termination
void dispatcher_connWriteCString(dispatcher_t* self, const char* str);
// shorthand for above to send number
void dispatcher_connWriteUINT32(dispatcher_t* self, uint32_t val);

// sends binary header (SCPI style) for a follow-up reply containing nBytes of data
void dispatcher_connWriteBinaryHeader(dispatcher_t* self, uint32_t nBytes);

// === REPL loop ===
void dispatcher_REPL(dispatcher_t* self, dispatcherEntry_t* dispEntries);
