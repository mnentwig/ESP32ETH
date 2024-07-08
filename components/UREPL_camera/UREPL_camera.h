#pragma once
typedef struct dispatcher_s dispatcher_t;
void camera_init(void);
void CAM_handlerPrefix(dispatcher_t* disp, char* inp, void* payload);
