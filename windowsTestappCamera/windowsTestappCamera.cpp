#error use exceptions for disconnect handling
// mingw: pacman -S mingw-w64-x86_64-fltk
// fltk-config --compile windowsTestappCamera.cpp
#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Window.H>
#include "jpgd.cpp"
#include <fstream>    // ifstream

#include <cstdint>
#include <cstdlib>

#define WIN32_LEAN_AND_MEAN
#define NOSOUND
#include <windows.h>
#include <assert.h>
#include <winsock.h>
#include <stdio.h>
#include <string.h>/* memcpy */
#include <cstdint>
#include <stdlib.h>
#include "threads.h"

#include <stdio.h>
//#include <sys/socket.h>
//#include <netinet/in.h>
//#include <arpa/inet.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
typedef int socklen_t;

#define PORT 80
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

void loadBinaryFile(const char* fname, unsigned char** mem, size_t* len){
  std::ifstream is(fname, std::ifstream::binary);
  if (!is) fail("ifstream");
  is.seekg(0, std::ios_base::end);
  size_t nBytes = is.tellg();
  is.seekg(0, std::ios_base::beg);
  char* m = (char*)malloc(nBytes); if (!m) fail("malloc");
  is.read((char*)m, nBytes);
  if (!is) fail("ifstream read");
  *mem = (unsigned char*)m;
  *len = nBytes;
}

extern "C" void* udpListen(void* p);
extern "C" void* tcpThread(void* p);

class An_Image_Window: public Fl_Window{
  //  friend void* udpListen(void* p);
public:
  int jpegWidth;
  int jpegHeight;
  unsigned char* pImg;
public:
  An_Image_Window(): Fl_Window(640, 480){
    pImg = NULL;
  }
  ~An_Image_Window(){
    jpgd::jpgd_free(pImg);    
  }
  
  virtual void draw(){
    if (pImg)
      fl_draw_image(pImg, 0, 0, jpegWidth, jpegHeight, /*nComps*/3);
  }
};

extern "C" void* udpListen(void* p){
  An_Image_Window* w = (An_Image_Window*) p;
  struct sockaddr_in servaddr, cliaddr;
  memset(&cliaddr, 0, sizeof(cliaddr)); 
  int sockfd;
  
  // Filling server information 
  servaddr.sin_family    = AF_INET; // IPv4 
  servaddr.sin_addr.s_addr = INADDR_ANY; 
  servaddr.sin_port = htons(PORT); 
  
  // Creating socket file descriptor 
  if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
    fail("socket creation failed"); 
  } 
  
  // Bind the socket with the server address 
  if ( bind(sockfd, (const struct sockaddr *)&servaddr,  
            sizeof(servaddr)) < 0 ) { 
    fail("bind");
  } 
  socklen_t len;
  size_t count = 0;
  while (1){
    int n; 

    const size_t nBuf = 65536*16;
    char buf[nBuf];
    len = sizeof(cliaddr);  //len is value/result 
    n = recvfrom(sockfd, (char *)buf, nBuf,  
		 /*MSG_WAITALL*/0, ( struct sockaddr *) &cliaddr, 
		 &len);
    //    printf("Received : %d\n", n);
    //fflush(stdout);
  
    // === decode ===
    size_t jpegSize;
    int actualComps;    
    int newWidth;
    int newHeight;
    //    printf("decomp...\n", count++); fflush(stdout);
    unsigned char* newPImg = jpgd::decompress_jpeg_image_from_memory((unsigned char*)buf, /*data size*/n, &newWidth, &newHeight, &actualComps, /*req_comps*/3);
    assert(actualComps == 3);
    //printf("... decomp done\n", count++); fflush(stdout);
    if (newPImg){
      Fl::lock();
      unsigned char* prevImg = w->pImg; // store for free outside lock
      w->jpegWidth = newWidth;
      w->jpegHeight = newHeight;
      w->pImg = newPImg;
      Fl::unlock();
      jpgd::jpgd_free(prevImg); // free outside lock
    } else {
      //printf("%i\t%i\n", newWidth, newHeight);
      jpgd::jpgd_free(newPImg); // DEBUG only
      
      //      fail("jpeg decode");
    }
    
    // === notify main thread ===
    //printf("%i\n", count++); fflush(stdout);
    Fl::awake(w);
  }
  return 0;
}

int write(SOCKET s, const char* msg){
  //printf("write: %s\n", msg); fflush(stdout);
  char buf[65536];
  sprintf(buf, "%s\n", msg);
  size_t n = strlen(buf);
  const char* p = buf;
  while (n){
    int nSent = send(s, p, n, 0);
    if (nSent < 0)
      return 0; // fail
    p += nSent;
    n -= nSent;
  }
  return 1;
}

int checkNoError(SOCKET s);
void writeCheckErr(SOCKET s, const char* cmd){
  write(s, cmd);
  checkNoError(s);
}

char buf[65536]; // get rid of this use malloc
const char* read(SOCKET s){
  char* p = buf;
  while (1){
    int n = recv(s, p, 1, 0);
    if (n <= 0) return NULL;
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

int checkNoError(SOCKET s){
  write(s, "ERR?");
  const char* b = read(s);
  if (!b) return 0;
  if (!strcmp(b, "NO_ERROR"))
    return 1;
  fprintf(stderr, "%s\n", b);
  exit(EXIT_FAILURE);
}

const char* writeRead(SOCKET s, const char* cmd){
  if (!write(s, cmd))
    return NULL;
  return read(s);
}

char readChar(SOCKET s){ // TBD fail reporting
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
char* readBinary(SOCKET s, size_t* nBytes){
  //printf("readBinary\n"); fflush(stdout);
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
  //printf("readBinary for %d bytes\n", n); fflush(stdout);
  char* buf = (char*)malloc(n);
  size_t nRet = n;
  char* p = buf;
  while (n){
    int nRec = recv(s, p, n, 0);
    if (n <= 0) fail("recv");
    assert(nRec <= (int)n);
    n -= nRec;
    p += nRec;
  }
  
  if (nBytes)
    *nBytes = nRet;
  //printf("readBinary done\n");
  return buf;
}

void tcpThread_connect(SOCKET s, const char* ipAddr, unsigned short port){
  /***********************************************/
  /* connect socket to given address */
  /***********************************************/
  struct sockaddr_in peeraddr_in;
  memset(&peeraddr_in, 0, sizeof(struct sockaddr_in));  
  peeraddr_in.sin_addr.s_addr = inet_addr(ipAddr); // 92/172
  peeraddr_in.sin_family = AF_INET;
  peeraddr_in.sin_port = htons(port);
  
  if (connect(s, (const struct sockaddr*)&peeraddr_in,
	      sizeof(struct sockaddr_in)) == SOCKET_ERROR){ 
    fail("connect");
  }
  printf("connected\n"); fflush(stdout);

}
int tcpThread_loadImage(An_Image_Window* w, SOCKET s){
  if (!write(s, "CAM:CAPT?")) return 0;
  size_t nBytes;
  char* b = readBinary(s, &nBytes);
  if (!b)
    return 0;
  
  size_t jpegSize;
  int actualComps;    
  int newWidth;
  int newHeight;
  unsigned char* newPImg = jpgd::decompress_jpeg_image_from_memory((unsigned char*)b, /*data size*/nBytes, &newWidth, &newHeight, &actualComps, /*req_comps*/3);
  assert(actualComps == 3);
  if (!newPImg)
    return 0;
  
  Fl::lock();
  unsigned char* prevImg = w->pImg; // store for free outside lock
  w->jpegWidth = newWidth;
  w->jpegHeight = newHeight;
  w->pImg = newPImg;
  Fl::unlock();
  jpgd::jpgd_free(prevImg); // free outside lock
  
  // === notify main thread ===
  Fl::awake(w);
  
  free(b);
  return 1;
}

extern "C" void* tcpThread(void* p){
  An_Image_Window* w = (An_Image_Window*) p;
  while (1){
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
#if 1
    struct timeval timeout;      
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    
    if (setsockopt (s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof timeout) < 0)
      fail("setsockopt (RCVTIMEO)\n");
    
    if (setsockopt (s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof timeout) < 0)
      fail("setsockopt (SNDTIMEO)\n");
#endif
    tcpThread_connect(s, "192.168.178.92", /*port*/76);
    if (!checkNoError(s))
      goto disconnect;
    while (1){
      if (!tcpThread_loadImage(w, s))
	goto disconnect;
    }
  disconnect:
    printf("disconnect\n");
    close(s);
  } // endless loop
  
#if 0
  int count2 = 0;
  while(1){
    checkNoError(s);
    printf("... tick %i\n", count2++); fflush(stdout);
  }
#endif
}



int main(int argc, char *argv[]){
  initWinsock();
  An_Image_Window window;  
  window.end();
  Fl::lock();
  Fl_Thread t1;
  //fl_create_thread(t1, udpListen, &window); // legacy variant; UDP packet size is too limited for larger images
  //tcpThread(&window); // !!!
  fl_create_thread(t1, tcpThread, &window); // 

  window.show(argc, argv);  
  Fl::unlock();
  while (Fl::wait() > 0) {
    if ((Fl::thread_message()) != NULL) {
      //printf("awake\n"); fflush(stdout);
      // === adjust window size ===
      window.size(window.jpegWidth, window.jpegHeight);
      
      // === flag redraw ===
      window.redraw();
      //printf("going to sleep\n"); fflush(stdout);
    }
  }
}
