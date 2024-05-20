//https://stackoverflow.com/questions/60657032/using-select-with-multiple-sockets
#include <stdio.h>
#include <string.h>
#include <stdint.h> // uint32_t
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "ethernet_init.h"
#include "sdkconfig.h"
#include "lwip/sockets.h"
#include "esp_err.h"
//#include <sys/param.h>
#include <sys/socket.h>
#include "driver/gpio.h"

#include "nvsMan.h"
#include "errMan.h"
#include "util.h"

static const char *TAG = "main";
static nvsMan_t nvsMan;

typedef struct ethRxLoopArg_s ethRxLoopArg_t;
typedef struct ethRxLoopArg_s{
  // usage:
  // - call with new input "inp", '\0'-terminated C string
  // - return value is reply (or NULL if no reply), '\0'-terminated C string
  // - for non-null return value, call repeatedly with NULL input to fully retrieve response
  // - function must terminate each line with '\n'
  // note: multi-line response is possible e.g. long memory dump, ADC capture etc.
  //       client may use follow-up "ECHO XYZ" with a suitable XYZ token to confirm sync afterwards
  //       response validity is guaranteed only up to next call on any 'self' method
  const char*(*dataCallback)(ethRxLoopArg_t* self, const char* inp);
  uint32_t myIpAddr;
  int port;
  errMan_t errMan;
  char buf[256]; // e.g. return message
} ethRxLoopArg_t;

void ethRxLoopArg_init(ethRxLoopArg_t* self){
  errMan_init(&self->errMan);
}

// === ethernet receive loop, shuts down task on disconnect ===
static IRAM_ATTR void ethRxLoop(void* _arg){
  ethRxLoopArg_t* arg = (ethRxLoopArg_t*)_arg;

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
  listen_addr4.sin_port = htons(arg->port);
  listen_addr4.sin_addr.s_addr = arg->myIpAddr;
  
  listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listen_socket < 0){
    ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
    ESP_ERROR_CHECK(ESP_FAIL);
  }    
  setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  ESP_LOGI(TAG, "Socket created for port %d", arg->port);
  
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
    //struct timeval timeout = { 0 };
    //timeout.tv_sec = IPERF_SOCKET_RX_TIMEOUT;
    //setsockopt(listen_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    client_socket = accept(listen_socket, (struct sockaddr *)&remote_addr, &addr_len);
    if (client_socket < 0){
      ESP_LOGE(TAG, "accept: errno %d", errno);
      ESP_ERROR_CHECK(ESP_FAIL);
    }
    ESP_LOGI(TAG, "accept: %s,%d", inet_ntoa(remote_addr.sin_addr), htons(remote_addr.sin_port));
      
    //timeout.tv_sec = IPERF_SOCKET_RX_TIMEOUT;
    //setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
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
	  errMan_reportError(&arg->errMan, "max cmd length exceeded");
	  overrun = 1;
	}
	nBytesBuf = 0;
	nBytesNextRead = blockingRead ? 1 : nBytesMax;
      }

      int nBytesReceived = recv(client_socket, cmdBuf+nBytesBuf, nBytesNextRead, flags);
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
	    cmdBuf[scanPos] = 0; // convert newline char to C end-of-string null
	    const char* resp = arg->dataCallback(arg, cmdBuf);
	    while (resp){
	      size_t nBytesToSend = strlen(resp);
	      size_t nBytesSent = sendto(client_socket, resp, nBytesToSend, 0, (struct sockaddr *)&remote_addr, sizeof(struct sockaddr_in));
	      if (nBytesSent != nBytesToSend) goto disconnect; /* "break mainloop" */
	      resp = arg->dataCallback(arg, /*indicates retrieval of additional output from last cmd*/NULL);
	    } // while more output to send
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
    }
  }
  disconnect:
    
    ESP_LOGI(TAG, "disconnect on port %d", arg->port);
	close(client_socket);
	vTaskDelete(NULL);      
	return;

}

static void console_task(void *arg){
  ESP_LOGI(TAG, "console task running");
  char buf[256];
  const int nBytesMax = 256;
  TickType_t delay_interval = 20 / portTICK_PERIOD_MS;
  int nBytes = 0;
  int prevNBytes = 0;
  while (1){
    const char c = fgetc(stdin);
    if (c == 0xFF){
      vTaskDelay(delay_interval);
      continue;
    }
    if (nBytes == nBytesMax) // full buffer keeps changing last char
      --nBytes;
    if ((c == 0x08) && nBytes){ // backspace deletes
      --nBytes;
    } else if (c == 27){// ESC key
      nBytes = 0; // clears line
    } else if (c == '\n'){
      fputc(c, stdout); // forward to console
      buf[nBytes++] = 0; // C string termination
      printf("you wrote '%s'\r\n", buf); // action here
      nBytes = 0;
      prevNBytes = 0;
    } else {
      buf[nBytes++] = c; // other chars append
    }
    
    // === overwrite past line if it gets shorter ===
    // note: clearing first to put cursor into correct place
    if (nBytes < prevNBytes){
      fputc('\r', stdout);
      for (int ix = 0; ix < prevNBytes; ++ix)
	fputc(' ', stdout);
    }

    // === write current line ===
    fputc('\r', stdout);
    for (int ix = 0; ix < nBytes; ++ix)
      fputc(buf[ix], stdout);
    fflush(stdout);
    prevNBytes = nBytes;
  }
}

/** Event handler for Ethernet events */
static void eth_event_handler(void *arg, esp_event_base_t event_base,
			      int32_t event_id, void *event_data){
  uint8_t mac_addr[6] = {0};
  /* we can get the ethernet driver handle from event data */
  esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;
  
  switch (event_id) {
  case ETHERNET_EVENT_CONNECTED:
    esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
    ESP_LOGI(TAG, "Ethernet Link Up");
    ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
	     mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    break;
  case ETHERNET_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "Ethernet Link Down");
    break;
  case ETHERNET_EVENT_START:
    ESP_LOGI(TAG, "Ethernet Started");
    break;
  case ETHERNET_EVENT_STOP:
    ESP_LOGI(TAG, "Ethernet Stopped");
    break;
  default:
    break;
  }
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
				 int32_t event_id, void *event_data){
  ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
  const esp_netif_ip_info_t *ip_info = &event->ip_info;
  
  ESP_LOGI(TAG, "Ethernet Got IP Address");
  ESP_LOGI(TAG, "~~~~~~~~~~~");
  ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
  ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
  ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
  ESP_LOGI(TAG, "~~~~~~~~~~~");
}

const char* myCallback(ethRxLoopArg_t* self, const char* inp){
  if (inp){
    // === extract command token ===
    const char *pTokenBegin, *pTokenEnd;
    if (util_tokenize(inp, &pTokenBegin, &pTokenEnd)){
      // special case: omit token count (optimization for loopback benchmarking)
      if (util_tokenEquals("ECHO", pTokenBegin, pTokenEnd)){
	// ECHO without argument returns empty line
	// only a single whitespace character is skipped. That is, "ECHO  x" returns " x"
	sprintf(self->buf, "%s\n", (*pTokenEnd=='\0') ? "" : pTokenEnd+1);
	return self->buf;	
      }

      // === count argument tokens ===
      size_t nArgs = util_tokenCount(pTokenEnd);

      if(util_tokenEquals("ERR?", pTokenBegin, pTokenEnd)){
	if (nArgs != 0){
	  errMan_reportError(&self->errMan, "PARAMETER_COUNT");
	  return NULL;
	}
	
	const char* errMsg;
	int nErr = errMan_getError(&self->errMan, &errMsg);
	sprintf(self->buf, "%i,%s\n", nErr, (!nErr) ? "NO_ERROR" : errMsg);	
	return self->buf;
      } else if(util_tokenEquals("ERRCLR", pTokenBegin, pTokenEnd)){
	if (nArgs != 0){
	  errMan_reportError(&self->errMan, "PARAMETER_COUNT");
	  return NULL;
	}
	const char* errMsg;
	errMan_getError(&self->errMan, &errMsg);
	return NULL;
      } else if(util_tokenEquals("RESTART", pTokenBegin, pTokenEnd)){
	if (nArgs != 0){
	  errMan_reportError(&self->errMan, "PARAMETER_COUNT");
	  return NULL;
	}

	ESP_LOGI(TAG, "RESTARTing...");
	esp_restart();
      } else if(util_tokenEquals("ETH_IP?", pTokenBegin, pTokenEnd)){
	if (nArgs != 0){
	  errMan_reportError(&self->errMan, "PARAMETER_COUNT");
	  return NULL;
	}

	char tmp[20];
	util_printIp(tmp, nvsMan_get_u32(&nvsMan, "ip"));
	sprintf(self->buf, "%s\n", tmp);
	return self->buf;
      } else if(util_tokenEquals("ETH_IP", pTokenBegin, pTokenEnd)){
	if (nArgs != 1){
	  errMan_reportError(&self->errMan, "PARAMETER_COUNT");
	  return NULL;
	}
	const char *pArg1Begin, *pArg1End;
	util_tokenize(pTokenEnd, &pArg1Begin, &pArg1End);
	char tmp[256];
	util_token2cstring(pArg1Begin, pArg1End, tmp);
	
	printf("ETH_IP: '%s'\n", tmp);
	return NULL;
      } else {
	errMan_reportError(&self->errMan, "SYNTAX_ERROR");
      }
    }
    
    sprintf(self->buf, "you wrote: '%s'\n", inp);
    return self->buf;
  } else {
    return NULL;
  }
}
 
void app_main(void){{
    // === initialize NVS ===
    nvsMan_init(&nvsMan);

    // Initialize Ethernet driver
    uint8_t eth_port_cnt = 0;
    esp_eth_handle_t *eth_handles;
    ESP_ERROR_CHECK(example_eth_init(&eth_handles, &eth_port_cnt));
  
    // Initialize TCP/IP network interface aka the esp-netif (should be called only once in application)
    ESP_ERROR_CHECK(esp_netif_init());
    // Create default event loop that running in background
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create instance(s) of esp-netif for Ethernet(s)
    if (eth_port_cnt != 1){
      ESP_LOGI(TAG, "expecting 1 ETH interface, got %d", eth_port_cnt);
      ESP_ERROR_CHECK(ESP_FAIL);
    }
  
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&cfg);
    // Attach Ethernet driver to TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handles[0])));
    
    // Register user defined event handers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    esp_netif_dhcpc_stop(eth_netif);
    char* ip= "192.168.178.123";
    char* gateway = "192.168.178.1";
    char* netmask = "255.255.255.0";
    esp_netif_ip_info_t info;
    memset(&info, 0, sizeof(esp_netif_ip_info_t));
    info.ip.addr = esp_ip4addr_aton(ip);
    info.gw.addr = esp_ip4addr_aton(gateway);
    info.netmask.addr = esp_ip4addr_aton(netmask);
    esp_netif_set_ip_info(eth_netif, &info);     
    
    // Start Ethernet driver state machine
    ESP_ERROR_CHECK(esp_eth_start(eth_handles[0]));
    
    // === start TCP/IP server threads ===
    // note: LWIP doesn't seem to have issues with multi-threaded, parallel accept() instead of a single select()
    const size_t nConnections = 4;
    ethRxLoopArg_t tcpIpConnections[nConnections]; // lifetime: below task must end before current scope is exited    
    for (size_t ixConn = 0; ixConn < nConnections; ++ixConn){
      ethRxLoopArg_init(&tcpIpConnections[ixConn]);
      tcpIpConnections[ixConn].dataCallback = myCallback;
      tcpIpConnections[ixConn].port = 76 + ixConn; // port range defined here
      tcpIpConnections[ixConn].myIpAddr = info.ip.addr;
      int ret = xTaskCreatePinnedToCore(ethRxLoop, "eth", /*stack*/4096, (void*)&tcpIpConnections[ixConn], tskIDLE_PRIORITY, NULL, portNUM_PROCESSORS - 1);
      if (ret != pdPASS) {
	ESP_LOGE(TAG, "failed to create eth task");
	ESP_ERROR_CHECK(ESP_FAIL);
      }
    }
    
    // === start console task ===
    int ret = xTaskCreatePinnedToCore(console_task, "myConsole", /*stack*/4096, NULL, tskIDLE_PRIORITY, NULL, portNUM_PROCESSORS - 1);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "failed to create console task");
      ESP_ERROR_CHECK(ESP_FAIL);
    }
    
    while (1){
      printf("zzz\n");    
      vTaskDelay(1000/portTICK_PERIOD_MS);
    }
    
#if 0    
    while (1){
      char c = fgetc(stdin);
      if (c != 0xFF){
	fprintf(stdout, "%02x\r\n", (int)c);
	break;
      }
    }
    fprintf(stdout, "thank you\r\n");
#endif
#if 0
  exit:
    if (client_socket != -1) {
      close(client_socket);
    }
      
    if (listen_socket != -1) {
      shutdown(listen_socket, 0);
      close(listen_socket);
      ESP_LOGI(TAG, "TCP Socket server is closed.");
    }
#endif
  } // while forever
}
