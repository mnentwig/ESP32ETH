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

// task function, receives above args as argument
void dpConnUart_task(void* _arg){
  dpConnUartArgs_t* args = (dpConnUartArgs_t*)_arg;
  
  ESP_LOGI(TAG, "task running");
  
  while (1){ // accept loop equivalent    
    dispatcher_REPL(args->disp, args->parseRoot);
    
    ESP_LOGI(TAG, "disconnect");
    uart_flush(args->uartNum);
  } // accept loop
}
