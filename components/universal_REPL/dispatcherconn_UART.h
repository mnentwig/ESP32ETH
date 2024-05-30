#pragma once
#include <stdint.h> // uint32_t
#include "esp_attr.h" // IRAM_ATTR
#include "dispatcher.h"

// usable by this specific connection, but not accessible to dispatcher
typedef struct {
  dispatcher_t* disp;
  int uartNum;
  void* userArg; // note: userArg would typically keep own pointer to dispatcher
  dispatcherEntry_t* parseRoot;
} dpConnUartArgs_t;

void dpConnUartArgs_init(dpConnUartArgs_t* self, dispatcher_t* disp, int uartNum, void* userArg, dispatcherEntry_t* parseRoot);

// task function, receives above args as argument
IRAM_ATTR void dpConnUart_task(void* _arg);

// common interface: to write to connection
IRAM_ATTR void dpConnUart_write(void* _arg, const char* data, size_t nBytes);
// common interface: read from connection
IRAM_ATTR int dpConnUart_read(void* _arg, char* data, size_t nBytesMax);
