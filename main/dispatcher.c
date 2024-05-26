#include "esp_log.h" // ESP_LOGX
#include "dispatcher.h"
#include "util.h"
#include <string.h> // strlen

//static const char *TAG = "dispatcher";

void dispatcher_init(dispatcher_t* self, void* appObj, dispatcher_writeFun_t writeFn, dispatcher_readFun_t readFn, void* connSpecArg){
  self->disconnected = 0;
  self->appObj = appObj;
  self->writeFn = writeFn;
  self->readFn = readFn;
  self->connSpecArg = connSpecArg;
  errMan_init(&self->errMan);
}

void dispatcher_flagDisconnect(dispatcher_t* self){
  self->disconnected = 1;
}

dispatcher_exec_e dispatcher_exec(dispatcher_t* self, char* inp, dispatcherEntry_t* dispEntries){
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
	  return EXEC_NOMATCH; // input too short
	if (*keyCursor != *inputCursor)
	  return EXEC_NOMATCH; // character mismatch
	  
	++keyCursor;
	++inputCursor;
	continue;
      } // if not end-of-key
      
      if (/* implied: endOfKey &&*/endOfInput){
	if (handlerDoSet){
	  handlerDoSet(self, inputCursor);
	  return self->disconnected ? EXEC_DISCONNECT : EXEC_OK;
	} else
	  return EXEC_NOMATCH; // command recognized but no doSet handler
      } // if endOfInput
      
      const int nextInputIsQuestionmark = !endOfInput & (*(inputCursor+1) == '?');
      const int nextInputIsColon = !endOfInput & (*(inputCursor+1) == ':');
      if (nextInputIsQuestionmark){
	if (handlerGet){
	  handlerGet(self, inputCursor);
	  return self->disconnected ? EXEC_DISCONNECT : EXEC_OK;
	} else
	  return EXEC_NOMATCH; // command recognized but no doSet handler
      } else if (nextInputIsColon){
	if (handlerPrefix){
	  handlerPrefix(self, inputCursor);
	  return self->disconnected ? EXEC_DISCONNECT : EXEC_OK;
	} else
	  return EXEC_NOMATCH; // command path recognized but no doSet handler
      }
    } // while cursor
    ++dispEntries;
  }
  return EXEC_NOMATCH;
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
