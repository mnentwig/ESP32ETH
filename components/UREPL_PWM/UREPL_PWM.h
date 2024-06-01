#pragma once
typedef struct dispatcher_s dispatcher_t;
void PWM_handlerPrefix(dispatcher_t* disp, char* inp, void* payload);
void PWM_handlerDoSet(dispatcher_t* disp, char* inp, void* payload);
void PWM_handlerGet(dispatcher_t* disp, char* inp, void* payload);

