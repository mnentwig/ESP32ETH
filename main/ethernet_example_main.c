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
#include "esp_err.h"
#include "driver/uart.h" // for driver install

#include "esp_vfs_dev.h"// blocking stdin (interrupt driver)
#include "esp_vfs.h"
#include "feature_nvsMan.h"
nvsMan_t nvsMan; // required feature (for accessing NVS)
#include "dispatcher.h"
#include "feature_ETH.h" // ETH command handler
#include "feature_UART.h" // UART command handler

#include "dispatcherconn_ethernet.h" // connection interface to dispatcher
#include "dispatcherconn_uart.h" // connection interface to dispatcher

static const char *TAG = "main";
static dispatcherEntry_t dispEntriesRootLevel[] = {
  {.key="ERR", .handlerPrefix=ERR_handlerPrefix, .handlerDoSet=ERR_handlerDoSet, .handlerGet=ERR_handlerGet},
  {.key="ETH", .handlerPrefix=ETH_handlerPrefix, .handlerDoSet=NULL, .handlerGet=NULL},
  {.key="UART", .handlerPrefix=UART_handlerPrefix, .handlerDoSet=NULL, .handlerGet=NULL},
  {.key=NULL, .handlerPrefix=NULL, .handlerDoSet=NULL, .handlerGet=NULL} // end marker
};


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

void app_main(void){
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
  dispatcher_t ethDispatchers[nConnections]; //lifetime: below task must end before current scope is exited
    
  dpConnEthArgs_t etArgs[nConnections];
  for (size_t ixConn = 0; ixConn < nConnections; ++ixConn){
    dpConnEthArgs_init(&etArgs[ixConn], &ethDispatchers[ixConn], /*port*/76+ixConn, /*myIpAddr*/info.ip.addr, /*userArg*/NULL, &dispEntriesRootLevel[0]);
    dispatcher_init(&ethDispatchers[ixConn], /*appObj*/NULL, dpConnEth_write, dpConnEth_read, (void*)&etArgs[ixConn]);
      
    int ret = xTaskCreatePinnedToCore(dpConnEth_task, "eth", /*stack*/4096, (void*)&etArgs[ixConn], tskIDLE_PRIORITY+1, NULL, portNUM_PROCESSORS - 1);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "failed to create eth task");
      ESP_ERROR_CHECK(ESP_FAIL);
    }
  }
    
  // === start console task ===
  dispatcher_t uartDispatcher; //lifetime: below task must end before current scope is exited	
  dpConnUartArgs_t uartArgs;
    
  dpConnUartArgs_init(&uartArgs, &uartDispatcher, CONFIG_ESP_CONSOLE_UART_NUM, /*userArg*/NULL, &dispEntriesRootLevel[0]);
  dispatcher_init(&uartDispatcher, /*appObj*/NULL, dpConnUart_write, dpConnUart_read, (void*)&uartArgs);
  int ret = xTaskCreatePinnedToCore(dpConnUart_task, "uart", /*stack*/4096, (void*)&uartArgs, tskIDLE_PRIORITY, NULL, portNUM_PROCESSORS - 1);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "failed to create uart task");
    ESP_ERROR_CHECK(ESP_FAIL);
  }
    
  while (1){
    vTaskDelay(1000/portTICK_PERIOD_MS);
  }
}
