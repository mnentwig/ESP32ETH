#pragma once
typedef struct dispatcher_s dispatcher_t;
void ADC_init();
void ADC_handlerPrefix(dispatcher_t* disp, char* inp, void* payload);
void ADC_handlerDoSet(dispatcher_t* disp, char* inp, void* payload);
void ADC_handlerGet(dispatcher_t* disp, char* inp, void* payload);

