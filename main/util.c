#include <string.h>
#include "stdio.h" // printf;
#include "esp_netif.h" // esp_ip4addr_aton

#include "util.h"
void util_printIp(char* buf, uint32_t ip){
  sprintf(buf, "%d.%d.%d.%d", (int)(ip >> 0) & 0xFF, (int)(ip >> 8) & 0xFF, (int)(ip >> 16) & 0xFF, (int)(ip >> 24) & 0xFF);
}

int util_parseIp(const char* inp, uint32_t* outp){
  uint32_t tmp = esp_ip4addr_aton(inp);
  char buf[256];
  util_printIp(buf, tmp);
  if (strcmp(buf, inp))
    return 0; // fail
  *outp = tmp;
  return 1; // success;
}

// pBegin, pEnd (non-inclusive) give next non-whitespace token
// return value is 1 if token is found, 0 otherwise
int util_tokenize(const char* inp, const char** pBegin, const char** pEnd){
  *pBegin = NULL;
  *pEnd = NULL;
  while (1){
    char c = *inp;
    *pEnd = inp;
    if (c == '\0'){
      // end of C-string
      return *pBegin != NULL;
    } else if ((c == ' ') || (c == '\t') || (c == '\v') || (c == '\r') || (c == '\n')){
      if (*pBegin)
	return 1; // whitespace terminates token (otherwise leading)
    } else {
      if (!*pBegin)
	*pBegin = inp; // first non-whitespace char
    }
    ++inp;
  }  
}

int util_tokenEquals(const char* inp, const char* pBegin, const char* pEnd){
  while (1){
    int end1 = (*inp == '\0');
    int end2 = (pBegin == pEnd);
    if (end1 && end2)
      return 1;
    if (end1 != end2)
      return 0;
    if (*inp != *pBegin)
      return 0; // character mismatch
    ++inp;
    ++pBegin;
  }
}

size_t util_tokenCount(const char* inp){
  const char* pBegin;
  const char* pEnd;
  size_t count = 0;
  while (util_tokenize(inp, &pBegin, &pEnd)){
    ++count;
    inp = pEnd;
  }
  return count;
}

void util_token2cstring(const char* pArg1Begin, const char* pArg1End, char* dest){
  while (pArg1Begin != pArg1End)
    *(dest++) = *(pArg1Begin++);
  *dest = '\0';  
}
