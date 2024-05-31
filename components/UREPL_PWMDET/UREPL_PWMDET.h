#pragma once
typedef struct dispatcher_s dispatcher_t;
void PWMDET_handlerPrefix(dispatcher_t* disp, char* inp, void* payload);
void PWMDET_handlerDoSet(dispatcher_t* disp, char* inp, void* payload);
void PWMDET_handlerGet(dispatcher_t* disp, char* inp, void* payload);
void* PWMDET_getPayload();

