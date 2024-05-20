//https://stackoverflow.com/questions/60657032/using-select-with-multiple-sockets
#include <stdio.h>
#include <string.h>
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
#include "nvs_flash.h"
#include "nvs.h"
//#include <sys/param.h>
#include <sys/socket.h>

static const char *TAG = "eth_example";
static nvs_handle_t NVS_handle;

typedef struct {
  void(*dataCallback)(const char*);
  uint32_t myIpAddr;
  int port;
} ethRxLoopArg_t;

// === error manager ===
typedef struct {
  char msg[256];
  int errCount;  
  SemaphoreHandle_t mutex;
} errMan_t;

static errMan_t errMan;
void errMan_init(errMan_t* self){
  self->errCount = 0;
  self->mutex = xSemaphoreCreateMutex();
}

void errMan_reportError(errMan_t* self, const char* msg){
  xSemaphoreTake(self->mutex, /*wait indefinitely*/portMAX_DELAY);

  if (!self->errCount) // only store first error
    strcpy(self->msg, msg);  
  ++self->errCount;

  xSemaphoreGive(self->mutex);
}

int errMan_getError(errMan_t* self, char* dest){
  int retVal = 0; // retval: no error
  xSemaphoreTake(self->mutex, /*wait indefinitely*/portMAX_DELAY); // --------
  if (self->errCount){
    strcpy(dest, self->msg);
    retVal = self->errCount; // retval: number of logged errors since last readout
    self->errCount = 0;
  }
  
  xSemaphoreGive(self->mutex); // ---------------------------------------------
  return retVal;
}

static void IRAM_ATTR send_to_tcpIp(int send_socket, struct sockaddr* dest_addr, uint8_t* data, int nBytes){
  int actual_send = sendto(send_socket, data, nBytes, 0, dest_addr, sizeof(struct sockaddr_in));
  if (actual_send != nBytes) {
    ESP_LOGI(TAG, "sendto");
    ESP_ERROR_CHECK(ESP_FAIL);
  }
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
	  errMan_reportError(&errMan, "max cmd length exceeded");
	  overrun = 1;
	}
	nBytesBuf = 0;
	nBytesNextRead = blockingRead ? 1 : nBytesMax;
      }

      ESP_LOGI(TAG, "receiving %d bytes", (int)nBytesNextRead);
      
      int nBytesReceived = recv(client_socket, cmdBuf+nBytesBuf, nBytesNextRead, flags);
      ESP_LOGI(TAG, "got %d bytes", (int)nBytesReceived);
      if (nBytesReceived < 0){ // DISCONNECT
	ESP_LOGI(TAG, "disconnect on port %d", arg->port);
	close(client_socket);
	vTaskDelete(NULL);      
	return;
      }
      if (nBytesReceived > nBytesNextRead){
	ESP_LOGE(TAG, "recvfrom: ?!excess data?!");
	ESP_ERROR_CHECK(ESP_FAIL);
      }

      nBytesBuf += nBytesReceived;
      state = (state + 1) & 0x1;
    
      // === scan for newline \n ===
      int scanPos = nParsed;
      while (scanPos < nBytesBuf){
	ESP_LOGI(TAG, "scanPos %d nBytesBuf %d char %d", (int)scanPos, (int)nBytesBuf, (int)cmdBuf[scanPos]);
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
	    // echo data (including newline) 
	    send_to_tcpIp(client_socket, (struct sockaddr *)&remote_addr, /*data*/(uint8_t*)cmdBuf, /*nBytes*/scanPos+1);
	    cmdBuf[scanPos] = 0; // convert newline char to C end-of-string null
	    arg->dataCallback(cmdBuf);
	  }
	}
	
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

      uint32_t addr = esp_ip4addr_aton("192.168.178.123");
      esp_err_t err = nvs_set_u32(NVS_handle, "ip", addr);
      if (err != ESP_OK){
	ESP_LOGE(TAG, "nvs_set_i32");
	ESP_ERROR_CHECK(ESP_FAIL);
      }
      err = nvs_commit(NVS_handle);
      if (err != ESP_OK){
	ESP_LOGE(TAG, "nvs_commit");
	ESP_ERROR_CHECK(ESP_FAIL);
      }      
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

#if 0
static IRAM_ATTR int getc_via_tcpIp(int recv_socket, struct sockaddr* dest_addr, int nBytes, uint8_t* data){
  socklen_t socklen = sizeof(struct sockaddr_in);
  while (1){
    int nRec = recvfrom(recv_socket, data, nBytes, 0, dest_addr, &socklen);
    if (nRec < 0)
      return 0; // DISCONNECT
    if (nRec > nBytes){
      ESP_LOGI(TAG, "recvfrom: excess data?!");
      ESP_ERROR_CHECK(ESP_FAIL);
    }
    nBytes -= nRec;
    if (!nBytes)
      return 1; // SUCCESS
    data += nRec;
  }
}
#endif

void myCallback(const char* string){
}
 
void app_main(void){{
    // === initialize NVS ===
    ESP_LOGI(TAG, "initializing NVS");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      // NVS partition was truncated and needs to be erased
      // Retry nvs_flash_init
      ESP_ERROR_CHECK(nvs_flash_erase());
      err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    {
      // === open NVS ===
      ESP_LOGI(TAG, "initializing NVS");
      err = nvs_open("storage", NVS_READWRITE, &NVS_handle);
      if (err != ESP_OK) {
	ESP_LOGE(TAG, "nvs_open (%s)", esp_err_to_name(err));
	ESP_ERROR_CHECK(ESP_FAIL);
      }

      // == set up default values if not found in NVM ===
      char* ip= "192.168.178.123";
      char* gateway = "192.168.178.1";
      char* netmask = "255.255.255.0";
      esp_netif_ip_info_t info;
      memset(&info, 0, sizeof(esp_netif_ip_info_t));
      info.ip.addr = esp_ip4addr_aton(ip);
      info.gw.addr = esp_ip4addr_aton(gateway);
      info.netmask.addr = esp_ip4addr_aton(netmask);
  
      err = nvs_get_u32(NVS_handle, "ip", &info.ip.addr);
      switch (err) {
      case ESP_OK:
	ESP_LOGI(TAG, "ip found in nvs");
	break;
      case ESP_ERR_NVS_NOT_FOUND:
	ESP_LOGI(TAG, "no ip in nvs, using default");
	break;
      default :
	ESP_LOGE(TAG, "nvs_get_u32 (%s)", esp_err_to_name(err));
	ESP_ERROR_CHECK(ESP_FAIL);    
      }

      err = nvs_get_u32(NVS_handle, "gateway", &info.gw.addr);
      switch (err) {
      case ESP_OK:
	ESP_LOGI(TAG, "gateway found in nvs");
	break;
      case ESP_ERR_NVS_NOT_FOUND:
	ESP_LOGI(TAG, "no gateway in nvs, using default");
	break;
      default :
	ESP_LOGE(TAG, "nvs_get_u32 (%s)", esp_err_to_name(err));
	ESP_ERROR_CHECK(ESP_FAIL);    
      }

      err = nvs_get_u32(NVS_handle, "netmask", &info.netmask.addr);
      switch (err) {
      case ESP_OK:
	ESP_LOGI(TAG, "netmask found in nvs");
	break;
      case ESP_ERR_NVS_NOT_FOUND:
	ESP_LOGI(TAG, "no netmask in nvs, using default");
	break;
      default :
	ESP_LOGE(TAG, "nvs_get_u32 (%s)", esp_err_to_name(err));
	ESP_ERROR_CHECK(ESP_FAIL);    
      }

      ESP_LOGI(TAG, "ip:" IPSTR, IP2STR(&info.ip));
      ESP_LOGI(TAG, "netmask:" IPSTR, IP2STR(&info.netmask));
      ESP_LOGI(TAG, "gateway:" IPSTR, IP2STR(&info.gw));
    }
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


    ethRxLoopArg_t ethLoopArg; // lifetime: below task must end before current scope is exited
    ethLoopArg.dataCallback = myCallback;
    ethLoopArg.port = 79;
    ethLoopArg.myIpAddr = info.ip.addr;
    int ret = xTaskCreatePinnedToCore(ethRxLoop, "eth", /*stack*/4096, (void*)&ethLoopArg, tskIDLE_PRIORITY, NULL, portNUM_PROCESSORS - 1);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "failed to create eth task");
    }





  
    // === start console task ===
     ret = xTaskCreatePinnedToCore(console_task, "myConsole", /*stack*/4096, NULL, tskIDLE_PRIORITY, NULL, portNUM_PROCESSORS - 1);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "failed to create console task");
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
