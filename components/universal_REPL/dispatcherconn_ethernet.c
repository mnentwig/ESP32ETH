#include "dispatcherconn_ethernet.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_log.h"

static const char *TAG = "dpConn_ETH";

void dpConnEthArgs_init(dpConnEthArgs_t* self, dispatcher_t* disp, int port, uint32_t myIpAddr, void* userArg, dispatcherEntry_t* parseRoot){
  self->disp = disp;
  self->port = port;
  self->myIpAddr = myIpAddr;
  self->userArg = userArg;
  self->parseRoot = parseRoot;
}


void dpConnEth_write(void* _arg, const char* data, size_t nBytes){
  dpConnEthArgs_t* args = (dpConnEthArgs_t*)_arg;
  while (1){
    int nBytesSent = sendto(args->client_socket, data, nBytes, 0, (struct sockaddr *)&args->remote_addr, sizeof(struct sockaddr_in));
    if (nBytesSent < 0){
      ESP_LOGI(TAG, "disconnect during write on port %d", args->port);
      dispatcher_setConnectState(args->disp, 0);
      return;
    }
    if (nBytesSent == nBytes)
      return; // OK
    assert(nBytesSent < nBytes);
    nBytes -= nBytesSent;
    data += nBytesSent;    
  }
}

int dpConnEth_read(void* _arg, char* data, size_t nBytesMax){
  dpConnEthArgs_t* args = (dpConnEthArgs_t*)_arg;
  assert(nBytesMax > 0);
  
  // IOCTL needs CONFIG_LWIP_SO_RCVBUF  
  size_t nBytesAvailable;
  ioctl(args->client_socket, FIONREAD, &nBytesAvailable);
  size_t n = nBytesMax < nBytesAvailable ? nBytesMax : nBytesAvailable;
  n = n ? n : 1; // read at least one byte
  int nBytesReceived = recv(args->client_socket, data, n, /*blocking read*/0);
  if (nBytesReceived < 0){
    dispatcher_setConnectState(args->disp, 0);
    ESP_LOGI(TAG, "disconnect during read on port %d", args->port);
    return 0;
  }
  return nBytesReceived;
}

// === ethernet receive loop, shuts down task on disconnect ===
void dpConnEth_task(void* _arg){
  dpConnEthArgs_t* etArgs = (dpConnEthArgs_t*)_arg;
  ESP_LOGI(TAG, "task running");
  
  // === socket: create ===
  int listen_socket = -1;
  int client_socket = -1;
  int opt = 1;
  int err = 0;
  struct sockaddr_in remote_addr;
  socklen_t addr_len = sizeof(struct sockaddr);
  struct sockaddr_storage listen_addr = { 0 };
  
  struct sockaddr_in listen_addr4 = { 0 };
  
  listen_addr4.sin_family = AF_INET;
  listen_addr4.sin_port = htons(etArgs->port);
  listen_addr4.sin_addr.s_addr = etArgs->myIpAddr;
  
  listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listen_socket < 0){
    ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
    ESP_ERROR_CHECK(ESP_FAIL);
  }    
  setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  ESP_LOGI(TAG, "Socket created for port %d", etArgs->port);
  
  // === socket: bind ===
  err = bind(listen_socket, (struct sockaddr *)&listen_addr4, sizeof(listen_addr4));
  if (err){
    ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
    ESP_ERROR_CHECK(ESP_FAIL);
  }
  
  // === socket: listen ===
  err = listen(listen_socket, /*backlog*/5);
  if (err){
    ESP_LOGE(TAG, "listen: errno %d", errno);
    ESP_ERROR_CHECK(ESP_FAIL);
  }
  memcpy(&listen_addr, &listen_addr4, sizeof(listen_addr4));
  
  // === socket accept loop ===
  while (1){
    client_socket = accept(listen_socket, (struct sockaddr *)&remote_addr, &addr_len);
    if (client_socket < 0){
      ESP_LOGE(TAG, "accept: errno %d", errno);
      ESP_ERROR_CHECK(ESP_FAIL);
    }
    ESP_LOGI(TAG, "eth accept: %s, %d", inet_ntoa(remote_addr.sin_addr), htons(remote_addr.sin_port));

    int flag = 1;
    int result = setsockopt(client_socket,            /* socket affected */
			    IPPROTO_TCP,     /* set option at TCP level */
			    TCP_NODELAY,     /* name of option */
			    (char *) &flag,  /* the cast is historical cruft */
			    sizeof(int));    /* length of option value */
    if (result < 0){
      ESP_LOGE(TAG, "setsockopt");
      ESP_ERROR_CHECK(ESP_FAIL);
    }
    
    errMan_clear(&etArgs->disp->errMan);
    etArgs->client_socket = client_socket;
    etArgs->remote_addr = remote_addr;
    
    dispatcher_REPL(etArgs->disp, etArgs->parseRoot);
    
    ESP_LOGI(TAG, "eth disconnect");
  } // while 
  
    // if (client_socket != -1) {
    //  close(client_socket);
    //}
  
    //if (listen_socket != -1) {
    //  shutdown(listen_socket, 0);
    //  close(listen_socket);
  
  // vTaskDelete(NULL);      
}
#if 0
ESP_LOGI(TAG, "disconnect");
uart_flush(args->uartNum);
} // accept loop


    
    dispatcher_setConnectState(etArgs->disp, 1);
    
    const uint32_t nBytesMax = 255; // +1 for C string zero termination
    uint32_t nBytesBuf = 0;
    uint32_t nParsed = 0;
    char cmdBuf[256];
    int state = 0;
    int overrun = 0;
    while (1){
      int blockingRead = !(state & 0x1);
      uint32_t nBytesNextRead = blockingRead ? 1 : nBytesMax - nBytesBuf;
      uint32_t flags = blockingRead ? 0 : /*non-blocking*/MSG_DONTWAIT;
      
      if (nBytesNextRead == 0){
	if (!overrun){
	  errMan_reportError(&etArgs->disp->errMan, "max cmd length exceeded");
	  overrun = 1;
	}
	nBytesBuf = 0;
	nBytesNextRead = blockingRead ? 1 : nBytesMax;
      }

      int nBytesReceived = recv(client_socket, cmdBuf+nBytesBuf, nBytesNextRead, flags);
      if (!dispatcher_getConnectState(etArgs->disp)) goto disconnect; /* "break mainloop" */
      if (nBytesReceived < 0) goto disconnect; /* "break mainloop" */
      if (nBytesReceived > nBytesNextRead){
	ESP_LOGE(TAG, "recvfrom: ?!excess data?!");
	ESP_ERROR_CHECK(ESP_FAIL);
      }
      
      nBytesBuf += nBytesReceived;
      state = (state + 1) & 0x1;
      
      // === scan for newline \n ===
      int scanPos = nParsed;
      while (scanPos < nBytesBuf){
	char c = cmdBuf[scanPos];
	// robust end-of-line sequence:
	// Windows: "\r\n"
	// Unix: "\n"
	// Telnet (default): "\r\n" default
	// Telnet (alt): "\r\0"
	// The 2nd terminating character gets suppressed the same way as leading whitespace
	int isTermChar = (c == '\n') || (c == '\r') || (c == '\0');
	int isLeadingWhitespace = ((c == ' ') || (c == '\t') || (c == '\v')) && (scanPos == 0);
	if (isTermChar){
	  if (overrun){
	    // error has already been reported but we do salvage data following the newline
	    overrun = 0;
	  } else {
	    cmdBuf[scanPos] = '\0'; // convert input to standard C null-terminated string
	    int r = dispatcher_exec(etArgs->disp, cmdBuf, etArgs->parseRoot);
	    if (!dispatcher_getConnectState(etArgs->disp))
	      goto disconnect; /* "break" back into accept loop" */
	    if (!r)
	      errMan_throwSYNTAX(&etArgs->disp->errMan);
	    break;
	  } // if not overrun
	} // if termChar
	
	if (isTermChar || isLeadingWhitespace){
	  // remove up to (including) scanPos from buffer:
	  // move data beyond scanPos ("beyond": scanPos +1 and length -1) to the head of buf
	  assert(nBytesBuf > nParsed);
	  memcpy(cmdBuf, cmdBuf+scanPos+1, nBytesBuf-nParsed-1);
	  
	  nBytesBuf -= (scanPos+1);
	  nParsed = 0;
	  scanPos = 0;
	} else { // regular character	  
	  ++nParsed;
	  ++scanPos;
	}
      } // foreach unparsed char
    } // while connection
  disconnect:;

#endif
