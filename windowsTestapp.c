// compile using e.g. MINGW on windows
// g++ windowsTestapp.c -lWs2_32
#define WIN32_LEAN_AND_MEAN
#define NOSOUND
#include <windows.h>
#include <assert.h>
#include <winsock.h>
#include <stdio.h>
/* memcpy */
#include <string.h>
#pragma comment(lib, "Ws2_32.lib")

void fail(const char* msg){
  fprintf(stderr, "error: %s\n", msg);
  fflush(stderr);
  exit(-1);
}

void initWinsock(){
  WORD wVersionRequested; 
  WSADATA wsaData; 
  int err; 
  wVersionRequested = MAKEWORD(1, 1); 
  wVersionRequested = MAKEWORD(2, 0); 
  
  err = WSAStartup(wVersionRequested, &wsaData);  
  if (err) fail("WSAStartup");
}

void write(SOCKET s, const char* msg){
  char buf[65536];
  sprintf(buf, "%s\n", msg);
  int count = send(s, buf, strlen(buf), 0);
  if (count == SOCKET_ERROR) fail("send: SOCKET_ERROR");
}

char buf[65536];
char* read(SOCKET s){
  char* p = buf;
  while (1){
    int n = recv(s, p, 1, 0);
    if (n <= 0) fail("recv");
    if ((*p == '\n') || (*p == '\r') || (*p == '\0'))
      break;
    ++p;
  }
  *p = '\0';
  p = buf;
  while ((*p == '\n') || (*p == '\r') || (*p == ' ') || (*p == '\t'))
    ++p;
  return p;
}

int main(void){
  initWinsock();

  /***********************************************/
  /* create a socket */
  /***********************************************/
  SOCKET s;  
  s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == INVALID_SOCKET) fail("socket(): INVALID_SOCKET");
  
  /***********************************************
   * turn off Nagle's algorithm, set immediate writing
   * as commands have side effects in realtime
   ***********************************************/
  int flag = 1;
  int result = setsockopt(s,            /* socket affected */
			  IPPROTO_TCP,     /* set option at TCP level */
			  TCP_NODELAY,     /* name of option */
			  (char *) &flag,  /* the cast is historical cruft */
			  sizeof(int));    /* length of option value */
  if (result < 0) fail("setsockopts");
	    
  /***********************************************/
  /* connect socket to given address */
  /***********************************************/
  struct sockaddr_in peeraddr_in;
  memset(&peeraddr_in, 0, sizeof(struct sockaddr_in));  
  peeraddr_in.sin_addr.s_addr = inet_addr("192.168.178.123");
  peeraddr_in.sin_family = AF_INET;
  unsigned short port = 76;
  peeraddr_in.sin_port = htons(port);
  
  if (connect(s, (const struct sockaddr*)&peeraddr_in,
	      sizeof(struct sockaddr_in)) == SOCKET_ERROR){ 
    fail("connect");
  }
  printf("connected\n"); fflush(stdout);

  for (size_t ix = 0; ix < 1000; ++ix){
    write(s, "ERR?");
    char* b = read(s);
    if (ix % 50 == 0)
      printf("%i received %s\n", ix, b);
  }
  exit(EXIT_SUCCESS);
}
