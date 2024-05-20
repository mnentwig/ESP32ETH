#pragma once
#include <stdint.h> // uint32_t
#include "esp_attr.h" // IRAM_ATTR

void util_printIp(char* buf, uint32_t ip);
IRAM_ATTR int util_tokenize(const char* inp, const char** pBegin, const char** pEnd);
IRAM_ATTR int util_tokenEquals(const char* inp, const char* pBegin, const char* pEnd);
IRAM_ATTR size_t util_tokenCount(const char* inp);
IRAM_ATTR void util_token2cstring(const char* pArg1Begin, const char* pArg1End, char* dest);
