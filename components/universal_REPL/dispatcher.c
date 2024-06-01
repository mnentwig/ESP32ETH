#include "esp_log.h" // ESP_LOGX
#include "dispatcher.h"
#include <string.h> // strlen
#include "esp_netif.h" // esp_ip4addr_aton
#include "esp_attr.h" // IRAM_ATTR

//static const char *TAG = "dispatcher";

// === inputAsciiBuf ===
// helper class for REPL
typedef struct {
  char buf[256]; // note: don't need additional space for '\0', as command terminator char is changed in-place
  size_t nBytesMax;
  size_t nBytes;
  size_t cursorBegin; // inclusive (first char with non-WS data)
  size_t cursorEnd; // exclusive (next char to scan)
  int overflow;
} inputAsciiBuf_t;

void inputAsciiBuf_init(inputAsciiBuf_t* self){
  self->nBytes = 0;
  self->cursorBegin = 0;
  self->cursorEnd = 0;
  self->overflow = 0;
  self->nBytesMax = sizeof(self->buf) / sizeof(char);
}
void inputAsciiBuf_getBufSpace(inputAsciiBuf_t* self, char** p, size_t* nBytesFree){
  *p = &self->buf[self->nBytes];
  *nBytesFree = self->nBytesMax - self->nBytes;
}
void inputAsciiBuf_applyNextRead(inputAsciiBuf_t* self, size_t nBytes){
  self->nBytes += nBytes;
  //ESP_LOGI(TAG, "asciiBuf now %d bytes", self->nBytes);
  assert(self->nBytes <= self->nBytesMax);
}

static int isNonTermWhitespace(char c){
  switch (c){
  case ' ':
  case '\t':
  case '\v':
    return 1;
  case '\r':
  case '\n':
  case '\0':
  default:
    return 0;
  }    
}

static int isTerm(char c){
  switch (c){
  default:
  case ' ':
  case '\t':
  case '\v':
    return 0;
  case '\r':
  case '\n':
  case '\0':
    return 1;
  }    
}

char* inputAsciiBuf_extractNextCmd(inputAsciiBuf_t* self){
  // === skip leading whitespace ===
  while (self->cursorBegin < self->nBytes)
    if (isNonTermWhitespace(self->buf[self->cursorBegin])){
      ++self->cursorBegin;
      self->cursorEnd = self->cursorBegin;
    } else
      break;
  
  // === locate terminator ===
  while (self->cursorEnd < self->nBytes)
    if (isTerm(self->buf[self->cursorEnd])){
      self->buf[self->cursorEnd] = '\0';
      char* r = &self->buf[self->cursorBegin];
      ++self->cursorEnd;
      self->cursorBegin = self->cursorEnd; 
      if (self->overflow)
	break;
      else{
	//ESP_LOGI(TAG, "asciiBuf cmd '%s'", r);
	return r;
      }
    } else {
      ++self->cursorEnd;
    } // if (not) term char
  
  // === remove processed data to free buffer for next read ===
  if (self->cursorBegin){ // note: treating 0 as special case is redundant
    //ESP_LOGI(TAG, "asciiBuf removing %d chars", self->cursorBegin);
    size_t nKeep = self->nBytes - self->cursorBegin;
    if (nKeep == self->nBytesMax){
      // no space in buffer for a terminating char => overflow condition
      ++self->overflow;
      nKeep = 0; // clear buffer to skip current command
    } else if (self->overflow){
      self->overflow = 0;
    }
    memcpy(/*dest*/self->buf, /*src*/self->buf+self->cursorBegin, /*n*/nKeep);
    self->nBytes -= self->cursorBegin;
    self->cursorEnd -= self->cursorBegin;
    self->cursorBegin = 0;
  }
  return NULL;
}

int inputAsciiBuf_getNewOverflow(inputAsciiBuf_t* self){
  return self->overflow == 1;
}

// === dispatcher ===
void dispatcher_init(dispatcher_t* self, void* appObj, dispatcher_writeFun_t writeFn, dispatcher_readFun_t readFn, void* connSpecArg){
  self->connectState = 0;
  self->appObj = appObj;
  self->writeFn = writeFn;
  self->readFn = readFn;
  self->connSpecArg = connSpecArg;
  errMan_init(&self->errMan);
}

int dispatcher_getConnectState(dispatcher_t* self){
  return self->connectState;
}

void dispatcher_setConnectState(dispatcher_t* self, int connectState){
  self->connectState = connectState;
}

int dispatcher_exec(dispatcher_t* self, char* inp, dispatcherEntry_t* dispEntries){
  //ESP_LOGI(TAG, "exec on (%s)", inp);
  while (dispEntries->key){
    const char* key = dispEntries->key;
    const dispatcherFun_t handlerPrefix = dispEntries->handlerPrefix;
    const dispatcherFun_t handlerDoSet = dispEntries->handlerDoSet;
    const dispatcherFun_t handlerGet = dispEntries->handlerGet;
    void* payload = dispEntries->payload;
    
    const char* keyCursor = key;
    char* inputCursor = inp;
    // === scan input and key ===
    while (1){
      const int endOfKey = *keyCursor == '\0';
      const int endOfInput = (*inputCursor == '\0') || (*inputCursor == ' ') || (*inputCursor == '\t') || (*inputCursor == '\v');
      if (!endOfKey){
	if (endOfInput){
	  //ESP_LOGI(TAG, "input (%s) is shorter than key (%s)", inp, key);
	  goto breakNextDispEntry; // input too short
	}
	if (*keyCursor != *inputCursor){
	  //ESP_LOGI(TAG, "input (%s) differs from key (%s)", inp, key);
	  goto breakNextDispEntry;
	}	  
	++keyCursor;
	++inputCursor;
	continue;
      } // if not end-of-key

      //ESP_LOGI(TAG, "input (%s) contains key (%s)", inp, key);

      if (/* implied: endOfKey &&*/endOfInput){
	if (handlerDoSet){
	  //ESP_LOGI(TAG, "handlerDoSet (key:%s)(args:%s)", key, inputCursor);
	  handlerDoSet(self, inputCursor, payload);
	  return self->connectState; // 1 unless disconnected
	} else {
	  //ESP_LOGI(TAG, "handlerDoSet (%s) is NULL", inp);
	  return 0;
	}
      } // if endOfInput

      // here, inputCursor points to the first unparsed character or '\0'
      
      const int nextInputIsQuestionmark = (*inputCursor == '?');
      const int nextInputIsColon = (*inputCursor == ':');
      if (nextInputIsQuestionmark){
	++inputCursor; // skip over "?"
	if (handlerGet){
	  //ESP_LOGI(TAG, "handlerGet (key:%s)(args:%s)", key, inputCursor);
	  handlerGet(self, inputCursor, payload);
	  return self->connectState; // 1 unless disconnected
	} else{
	  //ESP_LOGI(TAG, "handlerGet for key (%s) is NULL", key);
	  return 0;
	}
      } else if (nextInputIsColon){
	++inputCursor; // skip over ":"
	if (handlerPrefix){
	  //ESP_LOGI(TAG, "handlerPrefix for key (%s)", key);
	  handlerPrefix(self, inputCursor, payload);
	  return self->connectState; // 1 unless disconnected
	} else {
	  //ESP_LOGI(TAG, "handlerPrefix for key (%s) is NULL", key);
	  return 0;
	}
      } else {
	//ESP_LOGI(TAG, "substring match, ignoring");	
	goto breakNextDispEntry;
      }
    } // while cursor
  breakNextDispEntry:
    ++dispEntries;
  }
  return 0;
}

// itBegin returns first token in inp, skipping leading / trailing whitespace, terminated with '0' (inp gets modified!)
// itNext returns remainder of inp or NULL at end-of-input
// returns 1 if successful, 0 at end of data
IRAM_ATTR int nextToken(char* inp, char** itBegin, char** itNext){
  if (inp == NULL)
    return 0; // nextToken may return itNext==NULL
  char* p = inp;
  
  // === scan for begin of token ===
  while (1){
    switch (*p){
    case '\n': // not expected, should have been translated to '\0' by now
    case '\r': // not expected, should have been translated to '\0' by now
    case '\0':
     return 0; // no more data
    case ' ':
    case '\t':
    case '\v':
      ++p;
      continue; // skip over leading whitespace
    default:
      *itBegin = p;
      goto breakWhitespaceSearch;
    }
  } // while whitespace search
 breakWhitespaceSearch:
  
  // scan for end of token ===
  while (1){
    *itNext = p;
    char c = *p;
    switch (c){
    case '\n': // not expected, should have been translated to '\0' by now
    case '\r': // not expected, should have been translated to '\0' by now
    case '\0':
      *itNext = NULL;
      return 1;
    case ' ':
    case '\t':
    case '\v':
      *p = '\0';
      ++p;
      *itNext = p;      
      return 1; // got token
    default:
      ++p;
    }
  }
}

IRAM_ATTR int dispatcher_getArgsNull(dispatcher_t* self, char* inp){
  char* itBegin;
  char* itNext;
  if (0 != nextToken(inp, &itBegin, &itNext)){
    errMan_throwARG_COUNT(&self->errMan);
    return 0;
  }
  return 1;
}

IRAM_ATTR int dispatcher_getArgs(dispatcher_t* self, char* inp, size_t n, char** args){
  //  ESP_LOGI(TAG, "getArgs (%s)", inp);
  char* itBegin;
  char* itNext;
  while (n--){
    if (!nextToken(inp, &itBegin, &itNext)){
      errMan_throwARG_COUNT(&self->errMan); // expected arg, got none
      return 0;
    }
    *(args++) = itBegin;
    // ESP_LOGI(TAG, "getArgs token (%s)", itBegin);
    inp = itNext;
  }
  if (0 != nextToken(inp, &itBegin, &itNext)){
    errMan_throwARG_COUNT(&self->errMan); // got unexpected arg
    return 0;
  }
  return 1;
}

static void util_printIp(char* buf, uint32_t ip){
  sprintf(buf, "%d.%d.%d.%d", (int)(ip >> 0) & 0xFF, (int)(ip >> 8) & 0xFF, (int)(ip >> 16) & 0xFF, (int)(ip >> 24) & 0xFF);
}

int dispatcher_parseArg_IP(dispatcher_t* self, char* inp, uint32_t* result){
  uint32_t tmp = esp_ip4addr_aton(inp);
  char buf[20];
  util_printIp(buf, tmp);
  if (strcmp(buf, inp)){
    // ESP_LOGI(TAG, "IP parse: input (%s) does not match parsed IP (%s)", inp, buf);
    errMan_throwARG_NOT_IP(&self->errMan);
    return 0;
  }
  *result = tmp;
  return 1; // success;
}

int dispatcher_parseArg_UINT32(dispatcher_t* self, char* inp, uint32_t* outp){

  int base;
  char* inp2 = inp+1;
  if ((*inp == '0') && (*inp2 == 'x')){
    base = 16;
    inp += 2;
  } else {
    base = 10;
  }
  
  *outp = strtoul(inp, NULL, base);

  char buf[32];
  switch (base){
  case 10:
    sprintf(buf, "%lu", *outp); break;
  case 16:
    sprintf(buf, "%lx", *outp); break;
  default:
    assert(0);
  }
  if (strcasecmp(inp, buf)){
    errMan_throwARG_NOT_UINT32(&self->errMan);
    return 0;
  }
  return 1;
}

size_t dispatcher_connRead(dispatcher_t* self, char* buf, size_t nMax){
  return self->readFn(self->connSpecArg, buf, nMax);
}

IRAM_ATTR void dispatcher_connWrite(dispatcher_t* self, const char* buf, size_t nBytes){
  self->writeFn(self->connSpecArg, buf, nBytes);
}

IRAM_ATTR void dispatcher_connWriteCString(dispatcher_t* self, const char* str){
  self->writeFn(self->connSpecArg, str, strlen(str));
  self->writeFn(self->connSpecArg, "\n", 1);
}

IRAM_ATTR void dispatcher_REPL(dispatcher_t* self, dispatcherEntry_t* dispEntries){
  inputAsciiBuf_t inputAsciiBuf;
  inputAsciiBuf_init(&inputAsciiBuf);
  dispatcher_setConnectState(self, 1);
  
  while (1){ // connection loop
    char* p;
    size_t nBytesMax;
    inputAsciiBuf_getBufSpace(&inputAsciiBuf, &p, &nBytesMax);
    size_t nBytesRead = dispatcher_connRead(self, p, nBytesMax);
    if (!dispatcher_getConnectState(self))
      return;
    inputAsciiBuf_applyNextRead(&inputAsciiBuf, nBytesRead);
      
    while (1){
      char* cmd = inputAsciiBuf_extractNextCmd(&inputAsciiBuf);
      if (inputAsciiBuf_getNewOverflow(&inputAsciiBuf))
	errMan_throwOVERFLOW(&self->errMan);            
      if (!cmd)
	break;
      // ESP_LOGI(TAG, "cmd %s", cmd);
      int r = dispatcher_exec(self, cmd, dispEntries);
      if (!dispatcher_getConnectState(self))
	return;
      if (!r)
	errMan_throwSYNTAX(&self->errMan);      
    } // while commands to parse in asciiBuffer
  } // while connected
}
