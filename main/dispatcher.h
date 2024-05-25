#pragma once
#include <stdint.h> // uint32_t
#include "errMan.h"

typedef struct dispatcher_s dispatcher_t;

typedef enum {EXEC_OK, EXEC_NOMATCH, EXEC_DISCONNECT} dispatcher_exec_e;

// handler function for command. Returns 1 when input was fully handled, 0 otherwise
// note: -DoSet and -Get would typically never return 0, unless to pretend the identified command is not implemented.
typedef dispatcher_exec_e (*dispatcherFun_t)(dispatcher_t* disp, void* payload, const char* itBegin, const char* itEnd);

// - handlerPrefix is called when input starts with key plus ":" e.g. "ETH:" 
// - handlerDoSet is called when input equals key e.g. "IP"
// - handlerGet is called when input equals key with an additional question mark e.g. "IP?"
typedef struct {
  const char* key;
  dispatcherFun_t handlerPrefix;
  dispatcherFun_t handlerDoSet;
  dispatcherFun_t handlerGet;
} dispatcherEntry_t;

typedef struct dispatcher_s{
  // usage:
  // - call with new input "inp", '\0'-terminated C string
  // - return value is reply (or NULL if no reply), '\0'-terminated C string
  // - for non-null return value, call repeatedly with NULL input to fully retrieve response
  // - function must terminate each line with '\n'
  // note: multi-line response is possible e.g. long memory dump, ADC capture etc.
  //       client may use follow-up "ECHO XYZ" with a suitable XYZ token to confirm sync afterwards
  //       response validity is guaranteed only up to next call on any 'self' method
  uint32_t myIpAddr;
  int port;
  errMan_t errMan;
  char buf[256]; // e.g. return message
  void(*writeFun)(const char* data, size_t n);
  } dispatcher_t;

// writeFun returns 1 if successful, 0 for disconnect
void dispatcher_init(dispatcher_t* self, int(*writeFun)(const char* data, size_t n));
const char* dispatcher_execCmd(dispatcher_t* self, const char* inp); // old, delete

// called by command feed for parsing
dispatcher_exec_e dispatcher_exec
(dispatcher_t* self, // dispatcher state e.g. funcptr to write return data
 void* payload, // argument for handler funcs
 const char* itBegin, const char* itEnd, // input data
 dispatcherEntry_t* dispEntries // NULL-terminated list of keywords to match (one level of parse tree)
 );

