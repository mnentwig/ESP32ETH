#include "esp_log.h" // ESP_LOGX
#include "nvsMan.h"
#include "dispatcher.h"
#include "util.h"

extern nvsMan_t nvsMan;
static const char *TAG = "dispatcher";

void dispatcher_init(dispatcher_t* self, int(*writeFun)(const char* data, size_t n)){
  self->writeFun = writeFun;
  errMan_init(&self->errMan);
}

static const char* cmd_ETH_XYZ_get(dispatcher_t* self, const char* pTokenBegin, const char* pTokenEnd, const char* key){
  if (util_tokenCount(pTokenEnd) != 0){
    errMan_reportError(&self->errMan, "ARG_COUNT");
    return NULL;
  }

  char tmp[20];
  util_printIp(tmp, nvsMan_get_u32(&nvsMan, key));
  sprintf(self->buf, "%s\n", tmp);
  ESP_LOGI(TAG, "CMD_ETH_XYZ?(%s):%s", key, tmp);
  return self->buf;  
}

static const char* cmd_ETH_XYZ_set(dispatcher_t* self, const char* pTokenBegin, const char* pTokenEnd, const char* key){
  if (util_tokenCount(pTokenEnd) != 1){
    errMan_reportError(&self->errMan, "ARG_COUNT");
    return NULL;
  }
  
  const char *pArg1Begin, *pArg1End;
  util_tokenize(pTokenEnd, &pArg1Begin, &pArg1End);
  
  char tmp[256];
  util_token2cstring(pArg1Begin, pArg1End, tmp);
  uint32_t newIp;
  if (!util_parseIp(tmp, &newIp)){
    errMan_reportError(&self->errMan, "ARG_PARSEAS_IP");
  } else {
    ESP_LOGI(TAG, "CMD_ETH_XYZ(%s):%s", key, tmp);
    nvsMan_set_u32(&nvsMan, key, newIp);
  }
  return NULL;
}	

const char* dispatcher_execCmd(dispatcher_t* self, const char* inp){
  if (inp){
    // === extract command token ===
    const char *pTokenBegin, *pTokenEnd;
    if (util_tokenize(inp, &pTokenBegin, &pTokenEnd)){
      if (util_tokenEquals("ECHO", pTokenBegin, pTokenEnd)){
	// ECHO without argument returns empty line
	// only a single whitespace character is skipped. That is, "ECHO  x" returns " x"
	sprintf(self->buf, "%s\n", (*pTokenEnd=='\0') ? "" : pTokenEnd+1);
	return self->buf;	
      } else if(util_tokenEquals("ERR?", pTokenBegin, pTokenEnd)){
	if (util_tokenCount(pTokenEnd) != 0){
	  errMan_reportError(&self->errMan, "ARG_COUNT");
	  return NULL;
	}
	
	const char* errMsg;
	int nErr = errMan_getError(&self->errMan, &errMsg);
	sprintf(self->buf, "%i,%s\n", nErr, (!nErr) ? "NO_ERROR" : errMsg);	
	return self->buf;
      } else if(util_tokenEquals("ERRCLR", pTokenBegin, pTokenEnd)){
	if (util_tokenCount(pTokenEnd) != 0){
	  errMan_reportError(&self->errMan, "ARG_COUNT");
	  return NULL;
	}
	const char* errMsg;
	errMan_getError(&self->errMan, &errMsg);
	return NULL;
      } else if(util_tokenEquals("RESTART", pTokenBegin, pTokenEnd)){
	if (util_tokenCount(pTokenEnd) != 0){
	  errMan_reportError(&self->errMan, "ARG_COUNT");
	  return NULL;
	}

	ESP_LOGI(TAG, "RESTARTing...");
	esp_restart();	
      } else if(util_tokenStartsWith(pTokenBegin, pTokenEnd, "ETH:")){
	const size_t prefixLen = 4;
	pTokenBegin += prefixLen;
	if(util_tokenEquals("IP?", pTokenBegin, pTokenEnd)){
	  return cmd_ETH_XYZ_get(self, pTokenBegin, pTokenEnd, "ip");	
	} else if(util_tokenEquals("GW?", pTokenBegin, pTokenEnd)){
	  return cmd_ETH_XYZ_get(self, pTokenBegin, pTokenEnd, "gw");	
	} else if(util_tokenEquals("MASK?", pTokenBegin, pTokenEnd)){
	  return cmd_ETH_XYZ_get(self, pTokenBegin, pTokenEnd, "netmask");	
	} else if(util_tokenEquals("IP", pTokenBegin, pTokenEnd)){
	  return cmd_ETH_XYZ_set(self, pTokenBegin, pTokenEnd, "ip");	
	} else if(util_tokenEquals("GW", pTokenBegin, pTokenEnd)){
	  return cmd_ETH_XYZ_set(self, pTokenBegin, pTokenEnd, "gw");	
	} else if(util_tokenEquals("MASK", pTokenBegin, pTokenEnd)){
	  return cmd_ETH_XYZ_set(self, pTokenBegin, pTokenEnd, "netmask");	
	}
      } else {
	errMan_reportError(&self->errMan, "SYNTAX_ERROR");
      }
    }
    
    sprintf(self->buf, "you wrote: '%s'\n", inp);
    return self->buf;
  } else {
    return NULL;
  }
}


// ==================================

dispatcher_exec_e dispatcher_exec(dispatcher_t* self, void* payload, const char* itBegin, const char* itEnd, dispatcherEntry_t* dispEntries){
  while (dispEntries){
    const char* key = dispEntries->key;
    const dispatcherFun_t handlerPrefix = dispEntries->handlerPrefix;
    const dispatcherFun_t handlerDoSet = dispEntries->handlerDoSet;
    const dispatcherFun_t handlerGet = dispEntries->handlerGet;

    char* keyCursor = key;
    const char* inputCursor = itBegin;
    // === scan input and key ===
    while (true){
      const int endOfKey = *key == '\0';
      const int endOfInput = (inputCursor == itEnd) || (*inputCursor == ' ') || (*inputCursor == '\t') || (*inputCursor == '\v');
      if (endOfKey && endOfInput){
	if (handlerDoSet){
	  return handlerDoSet(self, payload, inputCursor, itEnd);
	}
      }
      const int nextInputIsQuestionmark = !endOfInput & (*(inputCursor+1) == '?');
      const int nextInputIsColon = !endOfInput & (*(inputCursor+1) == ':');
      
    }
    
  }
  return EXEC_NOMATCH;
}


