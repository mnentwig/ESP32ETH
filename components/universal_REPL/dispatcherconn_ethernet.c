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
