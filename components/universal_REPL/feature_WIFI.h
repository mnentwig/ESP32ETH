#pragma once
#include "dispatcher.h"
int WIFI_init();
void WIFI_handlerPrefix(dispatcher_t* disp, char* inp, void* payload);
uint32_t WIFI_getIp();

