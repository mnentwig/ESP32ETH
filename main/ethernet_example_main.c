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
#include <sys/socket.h> // ?
#include "driver/gpio.h"
#include "driver/uart.h"

#include "nvsMan.h"
//#include "errMan.h"
#include "util.h"
#include "esp_vfs_dev.h"// blocking stdin (interrupt driver)
#include "esp_vfs.h"
#include "dispatcher.h"
static const char *TAG = "main";
nvsMan_t nvsMan;

dispatcher_exec_e ETH_handlerPrefix(dispatcher_t* disp, void* payload, const char* itBegin, const char* itEnd);
dispatcher_exec_e ETH_handlerDoSet(dispatcher_t* disp, void* payload, const char* itBegin, const char* itEnd);
dispatcher_exec_e ETH_handlerGet(dispatcher_t* disp, void* payload, const char* itBegin, const char* itEnd);
dispatcher_exec_e UART_handlerPrefix(dispatcher_t* disp, void* payload, const char* itBegin, const char* itEnd);

static dispatcherEntry_t dispEntriesRootLevel[] = {
  {.key="ETH", .handlerPrefix=ETH_handlerPrefix, .handlerDoSet=ETH_handlerDoSet, .handlerGet=ETH_handlerGet},
  {.key="UART", .handlerPrefix=UART_handlerPrefix, .handlerDoSet=NULL, .handlerGet=NULL}
};
  
// === ethernet receive loop, shuts down task on disconnect ===
static IRAM_ATTR void ethernet_task(void* _arg){
  dispatcher_t* arg = (dispatcher_t*)_arg;

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
    ESP_LOGI(TAG, "eth accept: %s, %d", inet_ntoa(remote_addr.sin_addr), htons(remote_addr.sin_port));
    errMan_clear(&arg->errMan);
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

	    dispatcher_exec_e r = dispatcher_exec(arg, NULL, cmdBuf, cmdBuf+scanPos, dispEntriesRootLevel);
	    switch (r){
	    case EXEC_OK:
	      break;
	    case EXEC_NOMATCH:
	      // top level SYNTAX ERROR handler here
	      break;
	    case EXEC_DISCONNECT:
	      goto disconnect; /* "break" back into accept loop" */
	    }
	    
	    cmdBuf[scanPos] = 0; // convert newline char to C end-of-string null
	    const char* resp = dispatcher_execCmd(arg, cmdBuf);
	    while (resp){
	      size_t nBytesToSend = strlen(resp);
	      size_t nBytesSent = sendto(client_socket, resp, nBytesToSend, 0, (struct sockaddr *)&remote_addr, sizeof(struct sockaddr_in));
	      if (nBytesSent != nBytesToSend) goto disconnect; /* "break" back into accept loop" */
	      resp = dispatcher_execCmd(arg, /*indicates retrieval of additional output from last cmd*/NULL);
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
  disconnect:;
    ESP_LOGI(TAG, "eth disconnect");	  
  } // while (1)
  
  ESP_LOGI(TAG, "disconnect on port %d", arg->port);
  close(client_socket);
  vTaskDelete(NULL);      
  return;  
}

static void console_task(void* _arg){
  dispatcher_t* arg = (dispatcher_t*)_arg;
  
  ESP_LOGI(TAG, "console task running");
  char buf[256];
  const int nBytesMax = 255; // need 1 char for \0
  int nBytes = 0;
  int prevNBytes = 0;

  fcntl(fileno(stdout), F_SETFL, 0);
  fcntl(fileno(stdin), F_SETFL, 0);
  while (1){

    char c;
    const int nReceived = uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &c, /*nBytes*/1, /*timeout*/1000 / portTICK_PERIOD_MS);
    if (!nReceived)
      continue;
    if ((c == 0x08) && nBytes){ // backspace deletes
      --nBytes;
    } else if (c == 27){// ESC key
      nBytes = 0; // clears line
    } else if ((c == '\r') || (c == '\n')){
      if (!nBytes)
	continue; // suppress empty line and 2nd char of \r\n
      fputc(c, stdout); // forward to console      
      buf[nBytes++] = 0; // C string termination

      printf("you wrote '%s'\r\n", buf); // action here

      // === send reply ===
      const char* resp = dispatcher_execCmd(arg, buf);
      while (resp){
	size_t nBytesToSend = strlen(resp);
	size_t nBytesSent = uart_write_bytes(CONFIG_ESP_CONSOLE_UART_NUM, resp, strlen(resp));	
	if (nBytesSent != nBytesToSend){
	  ESP_LOGE(TAG, "uart_write_bytes");
	  ESP_ERROR_CHECK(ESP_FAIL);
	}
	resp = dispatcher_execCmd(arg, /*indicates retrieval of additional output from last cmd*/NULL);
      } // while more output to send
      
      nBytes = 0;
      prevNBytes = 0;
    } else {
      if (!nBytes && ((c == ' ') || (c == '\t')))
	continue; // suppress leading whitespace
      if (nBytes == nBytesMax) // full buffer keeps changing last char
	--nBytes;
      
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

int dispReplyFun(const char* data, size_t n){
  ESP_LOGI(TAG, "disp_reply with %d bytes", n);
  return 1;
}


void app_main(void){{
    // === install interrupt-based UART driver ===
    // enables blocking read
    fflush(stdin);
    fflush(stdout);
    
    ESP_ERROR_CHECK( uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0));
    esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);

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
      ESP_LOGE(TAG, "expecting 1 ETH interface, got %d", eth_port_cnt);
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
    dispatcher_t tcpIpConnections[nConnections+1]; // +1 for UART; lifetime: below task must end before current scope is exited    
    for (size_t ixConn = 0; ixConn < nConnections; ++ixConn){
      dispatcher_init(&tcpIpConnections[ixConn], &dispReplyFun);
      tcpIpConnections[ixConn].port = 76 + ixConn; // port range defined here
      tcpIpConnections[ixConn].myIpAddr = info.ip.addr;
      int ret = xTaskCreatePinnedToCore(ethernet_task, "eth", /*stack*/4096, (void*)&tcpIpConnections[ixConn], tskIDLE_PRIORITY, NULL, portNUM_PROCESSORS - 1);
      if (ret != pdPASS) {
	ESP_LOGE(TAG, "failed to create eth task");
	ESP_ERROR_CHECK(ESP_FAIL);
      }
    }
    
    // === start console task ===
    dispatcher_init(&tcpIpConnections[nConnections], &dispReplyFun);
    int ret = xTaskCreatePinnedToCore(console_task, "myConsole", /*stack*/4096, (void*)&tcpIpConnections[nConnections], tskIDLE_PRIORITY, NULL, portNUM_PROCESSORS - 1);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "failed to create console task");
      ESP_ERROR_CHECK(ESP_FAIL);
    }
    
    while (1){
      vTaskDelay(1000/portTICK_PERIOD_MS);
    }
    
    // if (client_socket != -1) {
    //  close(client_socket);
    //}
    
    //if (listen_socket != -1) {
    //  shutdown(listen_socket, 0);
    //  close(listen_socket);
  } // while forever
}
