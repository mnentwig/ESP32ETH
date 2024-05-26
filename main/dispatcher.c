#include "esp_log.h" // ESP_LOGX
#include "dispatcher.h"
#include "util.h"
#include <string.h> // strlen

//static const char *TAG = "dispatcher";

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
  while (dispEntries->key){
    const char* key = dispEntries->key;
    const dispatcherFun_t handlerPrefix = dispEntries->handlerPrefix;
    const dispatcherFun_t handlerDoSet = dispEntries->handlerDoSet;
    const dispatcherFun_t handlerGet = dispEntries->handlerGet;

    const char* keyCursor = key;
    char* inputCursor = inp;
    // === scan input and key ===
    while (1){
      const int endOfKey = *keyCursor == '\0';
      const int endOfInput = (*inputCursor != '\0') || (*inputCursor == ' ') || (*inputCursor == '\t') || (*inputCursor == '\v');
      if (!endOfKey){
	if (endOfInput)
	  return 0; // input too short
	if (*keyCursor != *inputCursor)
	  return 0; // character mismatch
	  
	++keyCursor;
	++inputCursor;
	continue;
      } // if not end-of-key
      
      if (/* implied: endOfKey &&*/endOfInput){
	if (handlerDoSet){
	  handlerDoSet(self, inputCursor);
	  return self->connectState; // 1 unless disconnected
	} else
	  return 0; // command recognized but no doSet handler
      } // if endOfInput
      
      const int nextInputIsQuestionmark = !endOfInput & (*(inputCursor+1) == '?');
      const int nextInputIsColon = !endOfInput & (*(inputCursor+1) == ':');
      if (nextInputIsQuestionmark){
	if (handlerGet){
	  handlerGet(self, inputCursor);
	  return self->connectState; // 1 unless disconnected
	} else
	  return 0; // command recognized but no doSet handler
      } else if (nextInputIsColon){
	if (handlerPrefix){
	  handlerPrefix(self, inputCursor);
	  return self->connectState; // 1 unless disconnected
	} else
	  return 0; // command path recognized but no doSet handler
      }
    } // while cursor
    ++dispEntries;
  }
  return 0;
}

void dispatcher_reply(dispatcher_t* self, const char* str){
  self->writeFn(self->connSpecArg, str, strlen(str));
}

// itBegin returns first token in inp, skipping leading / trailing whitespace, terminated with '0' (inp gets modified!)
// itNext returns remainder of inp or NULL at end-of-input
// returns 1 if successful, 0 at end of data
IRAM_ATTR int nextToken(char* inp, char** itBegin, char** itNext){
  char* p = inp;
  
  // === scan for begin of token ===
  while (1){
    *itBegin = p;
    char c = *p;
    ++p;
    switch (c){
    case '\0':
      return 0; // no more data
    case ' ':
    case '\t':
    case '\v':
      break;
    default:
      break;
    }
  }
  
  // scan for end of token ===
  while (1){
    *itNext = p;
    char c = *p;
    switch (c){
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

size_t dispatcher_connRead(dispatcher_t* self, char* buf, size_t nMax){
  return self->readFn(self->connSpecArg, buf, nMax);
}

void dispatcher_connWrite(dispatcher_t* self, const char* buf, size_t nBytes){
  self->writeFn(self->connSpecArg, buf, nBytes);
}
