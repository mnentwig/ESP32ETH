#include "feature_ETH.h"
#include "util.h"
#include "feature_nvsMan.h"
#include "feature_errMan.h"
#include "esp_log.h"
extern nvsMan_t nvsMan; // required feature (for accessing NVS)
extern errMan_t errMan; // required feature (for dealing with incorrect input)
static const char *TAG = "feature_ETH";

typedef enum {VAR_IP, VAR_GW, VAR_MASK} ETH_variant_e;

static void ETH_XYZ_handlerGet(dispatcher_t* disp, char* inp, ETH_variant_e v){
  if (!dispatcher_getArgsNull(disp, inp))
    return;

  char tmp[20];
  const char* key;
  switch (v){
  case VAR_IP: default: key = "ip"; break;
  case VAR_GW: key = "gw"; break;
  case VAR_MASK: key = "netmask"; break;
  }
  util_printIp(tmp, nvsMan_get_u32(&nvsMan, key));
  char tmp2[21];
  sprintf(tmp2, "%s\n", tmp);
  ESP_LOGI(TAG, "CMD_ETH_XYZ?(%s):%s", key, tmp2);
  dispatcher_reply(disp, tmp2);  
}

static void ETH_IP_handlerDoSet(dispatcher_t* disp, char* inp){}
static void ETH_IP_handlerGet(dispatcher_t* disp, char* inp){
  ETH_XYZ_handlerGet(disp, inp, VAR_IP);
}
static void ETH_GW_handlerDoSet(dispatcher_t* disp, char* inp){}
static void ETH_GW_handlerGet(dispatcher_t* disp, char* inp){
  ETH_XYZ_handlerGet(disp, inp, VAR_GW);
}
static void ETH_MASK_handlerDoSet(dispatcher_t* disp, char* inp){}
static void ETH_MASK_handlerGet(dispatcher_t* disp, char* inp){
  ETH_XYZ_handlerGet(disp, inp, VAR_MASK);
}

static dispatcherEntry_t ETH_dispEntries[] = {
  {.key="IP", .handlerPrefix=NULL, .handlerDoSet=ETH_IP_handlerDoSet, .handlerGet=ETH_IP_handlerGet},
  {.key="GW", .handlerPrefix=NULL, .handlerDoSet=ETH_GW_handlerDoSet, .handlerGet=ETH_GW_handlerGet},
  {.key="MASK", .handlerPrefix=NULL, .handlerDoSet=ETH_MASK_handlerDoSet, .handlerGet=ETH_MASK_handlerGet},
};

void ETH_handlerPrefix(dispatcher_t* disp, char* inp){
  dispatcher_exec(disp, inp, ETH_dispEntries);
}

#if 0
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
#endif
