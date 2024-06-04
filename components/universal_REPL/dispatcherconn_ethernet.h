#pragma once
#include <stdint.h> // uint32_t
#include "dispatcher.h"
#include "lwip/sockets.h"

// usable by this specific connection, but not accessible to dispatcher
typedef struct {
  dispatcher_t* disp;
  int port;
  uint32_t myIpAddr;
  void* userArg; // note: userArg would typically keep own pointer to dispatcher

  int client_socket;
  struct sockaddr_in remote_addr;
  dispatcherEntry_t* parseRoot;
} dpConnEthArgs_t;

void dpConnEthArgs_init(dpConnEthArgs_t* self, dispatcher_t* disp, int port, uint32_t myIpAddr, void* userArg, dispatcherEntry_t* parseRoot);

// task function, receives above args as argument
IRAM_ATTR void dpConnEth_task(void* _arg);

// common interface: to write to connection
IRAM_ATTR void dpConnEth_write(void* _arg, const char* data, size_t nBytes);
// common interface: read from connection
IRAM_ATTR int dpConnEth_read(void* _arg, char* data, size_t nBytesMax);
