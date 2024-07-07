#include "feature_WIFI.h"
#include "feature_nvsMan.h"
#include "feature_errMan.h"
#include "esp_log.h"
extern nvsMan_t nvsMan; // required feature (for accessing NVS)
// static const char *TAG = "feature_WIFI";
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#define CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK 1 // ??
#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi station";
static uint32_t myIp;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data){
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
      esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
      esp_wifi_connect();
      ESP_LOGI(TAG, "retry to connect to the AP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
      ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
      ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
      myIp = event->ip_info.ip.addr;
      xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

int WIFI_init(){
  s_wifi_event_group = xEventGroupCreate();
  
  //ESP_ERROR_CHECK(esp_netif_init()); // assuming main thread has done this already e.g. shared with ETH
  
  // ESP_ERROR_CHECK(esp_event_loop_create_default()); // assuming main thread has done this already
  esp_netif_create_default_wifi_sta();
  
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  
  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
						      ESP_EVENT_ANY_ID,
						      &event_handler,
						      NULL,
						      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
						      IP_EVENT_STA_GOT_IP,
						      &event_handler,
						      NULL,
						      &instance_got_ip));
  
  char* ssid = nvsMan_get_str(&nvsMan, "WIFI_SSID");
  char* pw = nvsMan_get_str(&nvsMan, "WIFI_PW");
  printf("SSID=%s pw=%s\n", ssid, pw);
  wifi_config_t wifi_config = {
    .sta = {
      .threshold.authmode = WIFI_AUTH_OPEN,
      .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
      .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
    }
  };
  strcpy((char*)wifi_config.sta.ssid, ssid);
  strcpy((char*)wifi_config.sta.password, pw);
  free(ssid);
  free(pw);
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
  ESP_ERROR_CHECK(esp_wifi_start() );
  
  ESP_LOGI(TAG, "wifi_init_sta finished.");
  #if 0

  // can't wait for WIFI to connect, otherwise the UART never becomes available to set up WIFI SSID/PW...
  // continuing doesn't seem to have any ill effects on the WIFI thread, though (TBD check what happens exactly. Probably some socket call blocks correctly until connected and everything is fine.) 
  



  
  /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
   * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
					 WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
					 pdFALSE,
					 pdFALSE,
					 portMAX_DELAY);
  
  /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
   * happened. */
  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "WIFI connected");
    return 1;
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGI(TAG, "Failed to connect WIFI");
    return 0;
  } else {
    ESP_LOGE(TAG, "UNEXPECTED EVENT");
    return 0;
  }
  #else
  return 1;
  #endif
}

uint32_t WIFI_getIp(){
  return myIp;
}



// FIXME this is copy-and-paste nonsense from ETH fixed IP when WLAN uses DHCP
static const char* NVS_KEY_IP = "WIFI_ip";
static const char* NVS_KEY_GW = "WIFI_gw";
static const char* NVS_KEY_MASK = "WIFI_mask";
typedef enum {VAR_IP, VAR_GW, VAR_MASK, VAR_SSID, VAR_PW} WIFI_variant_e;
static const char* getNvsKey(WIFI_variant_e v){
  switch (v){
  case VAR_IP:
    return NVS_KEY_IP;
  case VAR_GW:
    return NVS_KEY_GW;
  case VAR_MASK:
    return NVS_KEY_MASK;
  default:
    assert(0);
    return NULL;    
  }
}


static void util_printIp(char* buf, uint32_t ip){
  sprintf(buf, "%d.%d.%d.%d", (int)(ip >> 0) & 0xFF, (int)(ip >> 8) & 0xFF, (int)(ip >> 16) & 0xFF, (int)(ip >> 24) & 0xFF);
}

static void WIFI_XYZ_handlerGet(dispatcher_t* disp, char* inp, WIFI_variant_e v){
  if (!dispatcher_getArgsNull(disp, inp))
    return;
  
  char tmp[20];
  util_printIp(tmp, nvsMan_get_u32(&nvsMan, getNvsKey(v)));
  // ESP_LOGI(TAG, "CMD_WIFI_XYZ?(%s):%s", key, tmp);
  dispatcher_connWriteCString(disp, tmp);  
}

static void WIFI_XYZ_handlerDoSet(dispatcher_t* disp, char* inp, WIFI_variant_e v){
  const char* nvsKey = getNvsKey(v);

  char* args[1];
  if (!dispatcher_getArgs(disp, inp, /*n*/1, args))
    return;
  uint32_t ip;
  if (!dispatcher_parseArg_IP(disp, args[0], &ip))
    return;
  
  // ESP_LOGI(TAG, "CMD_WIFI_XYZ(%s):%s", nvsKey, args[0]);
  nvsMan_set_u32(&nvsMan, nvsKey, ip);
}

static void WIFI_IP_handlerDoSet(dispatcher_t* disp, char* inp, void* payload){
  // ESP_LOGI(TAG, "CMD_WIFI_IP (%s)", inp);
  WIFI_XYZ_handlerDoSet(disp, inp, VAR_IP);
}
static void WIFI_GW_handlerDoSet(dispatcher_t* disp, char* inp, void* payload){
  WIFI_XYZ_handlerDoSet(disp, inp, VAR_GW);
}
static void WIFI_MASK_handlerDoSet(dispatcher_t* disp, char* inp, void* payload){
  WIFI_XYZ_handlerDoSet(disp, inp, VAR_MASK);
}
static void SSID_handlerDoSet(dispatcher_t* disp, char* inp, void* payload){
  char* args[1];
  if (!dispatcher_getArgs(disp, inp, /*n*/1, args))
    return;
  nvsMan_set_str(&nvsMan, "WIFI_SSID", args[0]);
}
static void PW_handlerDoSet(dispatcher_t* disp, char* inp, void* payload){
  char* args[1];
  if (!dispatcher_getArgs(disp, inp, /*n*/1, args))
    return;
  nvsMan_set_str(&nvsMan, "WIFI_PW", args[0]);
}

static void WIFI_IP_handlerGet(dispatcher_t* disp, char* inp, void* payload){
  WIFI_XYZ_handlerGet(disp, inp, VAR_IP);
}
static void WIFI_GW_handlerGet(dispatcher_t* disp, char* inp, void* payload){
  WIFI_XYZ_handlerGet(disp, inp, VAR_GW);
}
static void WIFI_MASK_handlerGet(dispatcher_t* disp, char* inp, void* payload){
  WIFI_XYZ_handlerGet(disp, inp, VAR_MASK);
}
static void SSID_handlerGet(dispatcher_t* disp, char* inp, void* payload){
  char* r = nvsMan_get_str(&nvsMan, "WIFI_SSID");
  dispatcher_connWriteCString(disp, r);  
  free(r);
}
static void PW_handlerGet(dispatcher_t* disp, char* inp, void* payload){
  char* r = nvsMan_get_str(&nvsMan, "WIFI_PW");
  dispatcher_connWriteCString(disp, r);  
  free(r);
}

static dispatcherEntry_t WIFI_dispEntries[] = {
  {.key="IP", .handlerPrefix=NULL, .handlerDoSet=WIFI_IP_handlerDoSet, .handlerGet=WIFI_IP_handlerGet, .payload=NULL},
  {.key="GW", .handlerPrefix=NULL, .handlerDoSet=WIFI_GW_handlerDoSet, .handlerGet=WIFI_GW_handlerGet, .payload=NULL},
  {.key="MASK", .handlerPrefix=NULL, .handlerDoSet=WIFI_MASK_handlerDoSet, .handlerGet=WIFI_MASK_handlerGet, .payload=NULL},
  {.key="SSID", .handlerPrefix=NULL, .handlerDoSet=SSID_handlerDoSet, .handlerGet=SSID_handlerGet, .payload=NULL},
  {.key="PW", .handlerPrefix=NULL, .handlerDoSet=PW_handlerDoSet, .handlerGet=PW_handlerGet, .payload=NULL},
  {.key=NULL, .handlerPrefix=NULL, .handlerDoSet=NULL, .handlerGet=NULL, .payload=NULL} // end marker
};

void WIFI_handlerPrefix(dispatcher_t* disp, char* inp, void* payload){
  if (!dispatcher_exec(disp, inp, WIFI_dispEntries))
    errMan_throwSYNTAX(&disp->errMan);        
}
