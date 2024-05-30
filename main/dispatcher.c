#include "esp_log.h" // ESP_LOGX
#include "dispatcher.h"
#include "util.h"
#include <string.h> // strlen
#include "esp_netif.h" // esp_ip4addr_aton

static const char *TAG = "dispatcher";

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
  ESP_LOGI(TAG, "exec on (%s)", inp);
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
      const int endOfInput = (*inputCursor == '\0') || (*inputCursor == ' ') || (*inputCursor == '\t') || (*inputCursor == '\v');
      if (!endOfKey){
	if (endOfInput){
	  ESP_LOGI(TAG, "input (%s) is shorter than key (%s)", inp, key);
	  goto breakNextDispEntry; // input too short
	}
	if (*keyCursor != *inputCursor){
	  ESP_LOGI(TAG, "input (%s) differs from key (%s)", inp, key);
	  goto breakNextDispEntry;
	}	  
	++keyCursor;
	++inputCursor;
	continue;
      } // if not end-of-key

      ESP_LOGI(TAG, "input (%s) contains key (%s)", inp, key);

      if (/* implied: endOfKey &&*/endOfInput){
	if (handlerDoSet){
	  ESP_LOGI(TAG, "handlerDoSet (key:%s)(args:%s)", key, inputCursor);
	  handlerDoSet(self, inputCursor);
	  return self->connectState; // 1 unless disconnected
	} else {
	  ESP_LOGI(TAG, "handlerDoSet (%s) is NULL", inp);
	  return 0;
	}
      } // if endOfInput

      // here, inputCursor points to the first unparsed character or '\0'
      
      const int nextInputIsQuestionmark = (*inputCursor == '?');
      const int nextInputIsColon = (*inputCursor == ':');
      if (nextInputIsQuestionmark){
	++inputCursor; // skip over "?"
	if (handlerGet){
	  ESP_LOGI(TAG, "handlerGet (key:%s)(args:%s)", key, inputCursor);
	  handlerGet(self, inputCursor);
	  return self->connectState; // 1 unless disconnected
	} else{
	  ESP_LOGI(TAG, "handlerGet for key (%s) is NULL", key);
	  return 0;
	}
      } else if (nextInputIsColon){
	++inputCursor; // skip over ":"
	if (handlerPrefix){
	  ESP_LOGI(TAG, "handlerPrefix for key (%s)", key);
	  handlerPrefix(self, inputCursor);
	  return self->connectState; // 1 unless disconnected
	} else {
	  ESP_LOGI(TAG, "handlerPrefix for key (%s) is NULL", key);
	  return 0;
	}
      } else {
	ESP_LOGI(TAG, "substring match, ignoring");	
	goto breakNextDispEntry;
      }
    } // while cursor
  breakNextDispEntry:
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
  if (inp == NULL)
    return 0; // nextToken may return itNext==NULL
  ESP_LOGI(TAG, " ------------- next token on '%s'", inp);	
  char* p = inp;
  
  // === scan for begin of token ===
  while (1){
    switch (*p){
    case '\n': // not expected, should have been translated to '\0' by now
    case '\r': // not expected, should have been translated to '\0' by now
    case '\0':
      ESP_LOGI(TAG, " ------------- next token, none found");	
     return 0; // no more data
    case ' ':
    case '\t':
    case '\v':
      ESP_LOGI(TAG, " ------------- next token, skipping ws");	
      ++p;
      continue; // skip over leading whitespace
    default:
      ESP_LOGI(TAG, " ------------- next token, found word char");	
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
      ESP_LOGI(TAG, " ------------- next token, end");	
      *itNext = NULL;
      return 1;
    case ' ':
    case '\t':
    case '\v':
      *p = '\0';
      ++p;
      *itNext = p;      
      ESP_LOGI(TAG, " ------------- next token, got some");	
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
  ESP_LOGI(TAG, "getArgs (%s)", inp);
  char* itBegin;
  char* itNext;
  while (n--){
    if (!nextToken(inp, &itBegin, &itNext)){
      errMan_throwARG_COUNT(&self->errMan); // expected arg, got none
      return 0;
    }
    *(args++) = itBegin;
    ESP_LOGI(TAG, "getArgs token (%s)", itBegin);
    inp = itNext;
  }
  if (0 != nextToken(inp, &itBegin, &itNext)){
    errMan_throwARG_COUNT(&self->errMan); // got unexpected arg
    return 0;
  }
  return 1;
}

int dispatcher_parseArg_IP(dispatcher_t* self, char* inp, uint32_t* result){
  uint32_t tmp = esp_ip4addr_aton(inp);
  char buf[20];
  util_printIp(buf, tmp);
  if (strcmp(buf, inp)){
    ESP_LOGI(TAG, "IP parse: input (%s) does not match parsed IP (%s)", inp, buf);
    errMan_throwARG_NOT_IP(&self->errMan);
    return 0;
  }
  *result = tmp;
  return 1; // success;
}
  
size_t dispatcher_connRead(dispatcher_t* self, char* buf, size_t nMax){
  return self->readFn(self->connSpecArg, buf, nMax);
}

void dispatcher_connWrite(dispatcher_t* self, const char* buf, size_t nBytes){
  self->writeFn(self->connSpecArg, buf, nBytes);
}
