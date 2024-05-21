#pragma once
#include <stdint.h> // uint32_t
#include "errMan.h"

typedef struct dispatcher_s dispatcher_t;
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
} dispatcher_t;

void dispatcher_init(dispatcher_t* self);
const char* dispatcher_execCmd(dispatcher_t* self, const char* inp);
