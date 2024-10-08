// compile using e.g. MINGW on windows
// g++ windowsTestapp.c -lWs2_32 -O -static
#define WIN32_LEAN_AND_MEAN
#define NOSOUND
#include <windows.h>
#include <assert.h>
#include <winsock.h>
#include <stdio.h>
/* memcpy */
#include <string.h>
#include <cstdint>
#include <stdlib.h>
//#pragma comment(lib, "Ws2_32.lib")

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
char* readBinary(SOCKET s);
void checkNoError(SOCKET s);
void writeCheckErr(SOCKET s, const char* cmd){
  write(s, cmd);
  checkNoError(s);
}

void ESC_alg(SOCKET s){
  writeCheckErr(s, "ADC:PAT 32 33");
  writeCheckErr(s, "ADC:RATE 1000000");
  writeCheckErr(s, "ADC:ATT HIGH");
  write(s, "ADC:READ? 500000");
  char* b = readBinary(s);
  checkNoError(s);

  uint16_t vA[250000];
  uint16_t vB[250000];
  size_t ixA = 0;
  size_t ixB = 0;
  uint16_t* p = (uint16_t*) b;
  for (size_t ix = 0; ix < 500000; ++ix){
    uint32_t v = (uint32_t)*(p++);
    uint32_t chan = v >> 12;
    uint32_t val = v & 0x0FFF;
    if (chan == 4)
      vA[ixA++] = val;
    else if (chan == 5)
      vB[ixB++] = val;
    else{
      fprintf(stderr, "unexpected ADC channel: %u\n", chan);
      exit(EXIT_FAILURE); 
    }
  }
  free(b);
  
  size_t n = ixA > ixB ? ixB: ixA;

  FILE* f = fopen("out.txt", "wb");
  for (size_t ix = 0; ix < n; ++ix)
    fprintf(f, "%i\t%i\n", (int)vA[ix], (int)vB[ix]);
  fclose(f);

  printf("done! %lu %lu\n", ixA, ixB);
  exit(EXIT_SUCCESS);
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

char* writeRead(SOCKET s, const char* cmd){
  write(s, cmd);
  return read(s);
}

char readChar(SOCKET s){
  char c;
  while (1){
    int n = recv(s, &c, 1, 0);
    if (n < 0)fail("recv");
    assert(n <= 1);
    if (n) 
      return c;
  }
}

size_t readDigit(SOCKET s){
  char c = readChar(s);
  if ((c < '0') || (c > '9')) fail("readDigit: digit expected");
  return c - '0';
}

// caller must free() result
char* readBinary(SOCKET s){
  // === expect leading hash character for binary data ===
  char c = readChar(s); assert(c == '#');  

  // === read number of byte digits ===
  size_t nDigits = readDigit(s);
  size_t n = 0;

  // == read number of bytes ===
  while (nDigits--){    
    size_t dig = readDigit(s);
    n *= 10;
    n += dig;
  }
  char* buf = (char*)malloc(n);
  char* p = buf;
  while (n){
    int nRec = recv(s, p, n, 0);
    if (n <= 0) fail("recv");
    assert(nRec <= (int)n);
    n -= nRec;
    p += nRec;
  }
  return buf;
}

void adcRateSweep(SOCKET s){
  char buf[256];
  float confRate_Hz = 20000;
  while (confRate_Hz < 2e6){
    sprintf(buf, "ADC:RATE %i", (int)confRate_Hz);
    write(s, buf);

    float tNom_s = 1.0f;
    
    sprintf(buf, "ADC:READ? %i", (int)(tNom_s*confRate_Hz+0.5));
    write(s, buf);
    char* b = readBinary(s); free(b);
    
    char* r = writeRead(s, "ADC:LASTRATE?");
    printf("%i\t%s\n", (int)(confRate_Hz+0.5), r);
    fflush(stdout);
    confRate_Hz *= 1.1;
  }
}

void checkNoError(SOCKET s){
  write(s, "ERR?");
  char* b = read(s);
  if (!strcmp(b, "NO_ERROR"))
    return;
  fprintf(stderr, "%s\n", b);
  exit(EXIT_FAILURE);
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
  #if 0
  struct timeval timeout;      
  timeout.tv_sec = 3;
  timeout.tv_usec = 0;
  
  if (setsockopt (s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof timeout) < 0)
    fail("setsockopt (RCVTIMEO)\n");
  
  if (setsockopt (s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof timeout) < 0)
    fail("setsockopt (SNDTIMEO)\n");
#endif  
  /***********************************************/
  /* connect socket to given address */
  /***********************************************/
  struct sockaddr_in peeraddr_in;
  memset(&peeraddr_in, 0, sizeof(struct sockaddr_in));  
  peeraddr_in.sin_addr.s_addr = inet_addr("192.168.178.172"); // 123
  peeraddr_in.sin_family = AF_INET;
  unsigned short port = 76;
  peeraddr_in.sin_port = htons(port);
  
  if (connect(s, (const struct sockaddr*)&peeraddr_in,
	      sizeof(struct sockaddr_in)) == SOCKET_ERROR){ 
    fail("connect");
  }
  printf("connected\n"); fflush(stdout);
  checkNoError(s);

  //  ESC_alg(s);
  
  //adcRateSweep(s);
#if 1
  for (size_t ix = 0; ix < 1000; ++ix){
    write(s, "ERR?");
    char* b = read(s);
    //    if (ix % 50 == 0)
      printf("%i received %s\n", ix, b);
  }
  return 0;
#endif  

  uint32_t confRate_Hz = 200000;
  sprintf(buf, "ADC:RATE %i", (int)confRate_Hz);
  write(s, buf);
  checkNoError(s);

  write(s, "ADC:ATT HIGH");
  checkNoError(s);
  char* r = writeRead(s, "ADC:ATT?");
  printf("ATT is %s\n", r);

  write(s, "ADC:PAT 32 33");
  checkNoError(s);

  r = writeRead(s, "ADC:PAT?");
  printf("PAT is %s\n", r);
  
  float tNom_s = 1.0f;
  size_t nSamples = (int)(tNom_s*confRate_Hz+0.5);
  sprintf(buf, "ADC:READ? %lu", nSamples);
  write(s, buf);
  char* b = readBinary(s);
  
  // === write output ===
  uint16_t* p = (uint16_t*) b;
  FILE* f = fopen("out.txt", "wb");
  for (size_t ix = 0; ix < nSamples; ++ix){
    uint32_t v = (uint32_t)*(p++);
    uint32_t chan = v >> 12;
    uint32_t val = v & 0x0FFF;
    printf("%i\t%i\n", chan, val);
    fprintf(f, "%i\t%i\n", chan, val);
  }
  free(b);
  fclose(f);
  checkNoError(s);

  // === report rate ===
  r = writeRead(s, "ADC:LASTRATE?");
  printf("rConf\trActual\n");
  printf("%i\t%s\n", (int)(confRate_Hz+0.5), r);
  checkNoError(s);
  
  closesocket(s);
  exit(EXIT_SUCCESS);
}
