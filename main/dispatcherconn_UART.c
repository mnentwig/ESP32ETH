#include "dispatcherconn_UART.h"
#include "driver/uart.h" // uart_write_bytes
#include "esp_log.h"
#include "esp_err.h"
#include <string.h> // memcpy
static const char *TAG = "dpConn_UART";
void dpConnUartArgs_init(dpConnUartArgs_t* self, dispatcher_t* disp, int uartNum, void* userArg, dispatcherEntry_t* parseRoot){
  self->disp = disp;
  self->uartNum = uartNum;
  self->userArg = userArg;
  self->parseRoot = parseRoot;
}

void dpConnUart_write(void* _arg, const char* data, size_t nBytes){
  dpConnUartArgs_t* args = (dpConnUartArgs_t*)_arg;
  
  size_t nBytesSent = uart_write_bytes(args->uartNum, data, nBytes);	
  if (nBytesSent != nBytes){
    ESP_LOGE(TAG, "uart_write_bytes");
    ESP_ERROR_CHECK(ESP_FAIL);
  }
}

int dpConnUart_read(void* _arg, char* data, size_t nBytesMax){
  dpConnUartArgs_t* args = (dpConnUartArgs_t*)_arg;
  assert(nBytesMax > 0);

  size_t nAvailable;
  ESP_ERROR_CHECK(uart_get_buffered_data_len(args->uartNum, &nAvailable));
  size_t n = nBytesMax < nAvailable ? nBytesMax : nAvailable;
  n = n ? n : 1; // read at least one byte

  int nBytesReceived;
  while (1){
    nBytesReceived = uart_read_bytes(args->uartNum, data, /*nBytes*/n, /*timeout*/1000 / portTICK_PERIOD_MS);
    if (nBytesReceived)
      break;
  }

  return nBytesReceived;
}

typedef struct {
  char buf[256]; // note: don't need additional space for '\0', as command terminator char is changed in-place
  size_t nBytesMax;
  size_t nBytes;
  size_t cursorBegin; // inclusive (first char with non-WS data)
  size_t cursorEnd; // exclusive (next char to scan)
  int overflow;
} inputAsciiBuf_t;

void inputAsciiBuf_init(inputAsciiBuf_t* self){
  self->nBytes = 0;
  self->cursorBegin = 0;
  self->cursorEnd = 0;
  self->overflow = 0;
  self->nBytesMax = sizeof(self->buf) / sizeof(char);
}
void inputAsciiBuf_getNextRead(inputAsciiBuf_t* self, char** p, size_t* nBytesMaxRead){
  *p = &self->buf[self->nBytes];
  *nBytesMaxRead = self->nBytesMax - self->nBytes;
}
void inputAsciiBuf_applyNextRead(inputAsciiBuf_t* self, size_t nBytes){
  self->nBytes += nBytes;
  ESP_LOGI(TAG, "asciiBuf now %d bytes", self->nBytes);
  assert(self->nBytes <= self->nBytesMax);
}

static int isNonTermWhitespace(char c){
  switch (c){
  case ' ':
  case '\t':
  case '\v':
    return 1;
  case '\r':
  case '\n':
  case '\0':
  default:
    return 0;
  }    
}

static int isTerm(char c){
  switch (c){
  default:
  case ' ':
  case '\t':
  case '\v':
    return 0;
  case '\r':
  case '\n':
  case '\0':
    return 1;
  }    
}

char* inputAsciiBuf_extractNextCmd(inputAsciiBuf_t* self){
  // === skip leading whitespace ===
  while (self->cursorBegin < self->nBytes)
    if (isNonTermWhitespace(self->buf[self->cursorBegin])){
      ++self->cursorBegin;
      self->cursorEnd = self->cursorBegin;
    } else
      break;
  
  // === locate terminator ===
  while (self->cursorEnd < self->nBytes)
    if (isTerm(self->buf[self->cursorEnd])){
      self->buf[self->cursorEnd] = '\0';
      char* r = &self->buf[self->cursorBegin];
      ++self->cursorEnd;
      self->cursorBegin = self->cursorEnd; 
      if (self->overflow)
	break;
      else{
	ESP_LOGI(TAG, "asciiBuf cmd '%s'", r);
	return r;
      }
    } else {
      ++self->cursorEnd;
    } // if (not) term char
  
  // === remove processed data to free buffer for next read ===
  if (self->cursorBegin){ // note: treating 0 as special case is redundant
    ESP_LOGI(TAG, "asciiBuf removing %d chars", self->cursorBegin);
    size_t nKeep = self->nBytes - self->cursorBegin;
    if (nKeep == self->nBytesMax){
      // no space in buffer for a terminating char => overflow condition
      ++self->overflow;
      nKeep = 0; // clear buffer to skip current command
    } else if (self->overflow){
      self->overflow = 0;
    }
    memcpy(/*dest*/self->buf, /*src*/self->buf+self->cursorBegin, /*n*/nKeep);
    self->nBytes -= self->cursorBegin;
    self->cursorEnd -= self->cursorBegin;
    self->cursorBegin = 0;
  }
  return NULL;
}

int inputAsciiBuf_getNewOverflow(inputAsciiBuf_t* self){
  return self->overflow == 1;
}

// task function, receives above args as argument
void dpConnUart_task(void* _arg){
  dpConnUartArgs_t* args = (dpConnUartArgs_t*)_arg;
  
  ESP_LOGI(TAG, "task running");
  inputAsciiBuf_t inputAsciiBuf;
  
  while (1){ // accept loop equivalent    
    inputAsciiBuf_init(&inputAsciiBuf);
    dispatcher_setConnectState(args->disp, 1);
    while (1){ // connection loop
      char* p;
      size_t nBytesMax;
      inputAsciiBuf_getNextRead(&inputAsciiBuf, &p, &nBytesMax);
      size_t nBytesRead = dispatcher_connRead(args->disp, p, nBytesMax);
      if (!dispatcher_getConnectState(args->disp))
	goto disconnect; /* "break" back into accept loop" */
      inputAsciiBuf_applyNextRead(&inputAsciiBuf, nBytesRead);
      
      while (1){
	char* cmd = inputAsciiBuf_extractNextCmd(&inputAsciiBuf);
	if (inputAsciiBuf_getNewOverflow(&inputAsciiBuf))
	  errMan_throwOVERFLOW(&args->disp->errMan);            
	if (!cmd)
	  break;
	ESP_LOGI(TAG, "cmd %s", cmd);
	int r = dispatcher_exec(args->disp, cmd, args->parseRoot);
	if (!dispatcher_getConnectState(args->disp))
	  goto disconnect; /* "break" back into accept loop" */
	if (!r)
	  errMan_throwSYNTAX(&args->disp->errMan);      
      } // while commands to parse in asciiBuffer
    } // while connected
  disconnect:
    ESP_LOGI(TAG, "disconnect");
    uart_flush(args->uartNum);
  } // accept loop
}
