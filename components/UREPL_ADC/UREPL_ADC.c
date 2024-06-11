#include "UREPL_ADC.h"
#include "feature_errMan.h"
#include "esp_err.h" // ESP_ERROR_CHECK
#include "esp_log.h"
#include "dispatcher.h"
#include "string.h" // memset
#include "soc/soc_caps.h" // SOC_ADC_SAMPLE_FREQ_THRES_LOW; SOC_ADC_SAMPLE_FREQ_THRES_HIGH

#include "esp_adc/adc_continuous.h"
#include "esp_timer.h"

static const char *TAG = "ADC";

#define MAXTRANSFER_BYTES 1024
// === consider below fields of singleton object ==
static uint8_t transferBuf[MAXTRANSFER_BYTES];
static adc_continuous_handle_t adcHandle;
static uint32_t nOverflow;
static uint32_t nCapt;
static uint64_t lastCaptBegin;
static uint64_t lastCaptEnd;
static uint32_t convRate_Hz;
static uint32_t convRateFudged_Hz;
static adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {0};
static size_t nChanEnabled;

#define RATE_FUDGE_FACTOR 0.8181818f // as fraction: 3^2/11
static bool IRAM_ATTR cbConvDone(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data){
  nCapt += edata->size / sizeof(adc_digi_output_data_t);
  return 0;
}

static bool IRAM_ATTR cbOverflow(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data){
  ++nOverflow;
  return 0;
}

static void defaultAdcConfig(){
  // === default settings if no PAT is given ===
  nChanEnabled = 1;
  adc_pattern[0].atten = ADC_ATTEN_DB_12;
  adc_pattern[0].channel = ADC_CHANNEL_6 & 0x7;
  adc_pattern[0].unit = ADC_UNIT_1;
  adc_pattern[0].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;  
}

void ADC_init(){
  convRate_Hz = 80000;
  convRateFudged_Hz = convRate_Hz / RATE_FUDGE_FACTOR;
  nOverflow = 0;
  nCapt = 0;
  lastCaptBegin = 0;
  lastCaptEnd = 1;

  // === ADC handle ===
  adc_continuous_handle_cfg_t adc_config = {
    .max_store_buf_size = 4*1024,
    .conv_frame_size = MAXTRANSFER_BYTES,
  };
  ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &adcHandle));

#if 0
  // dump list of ADC channel pins
  for (size_t ix = 0; ix < 8; ++ix){
    int ionum;
    ESP_ERROR_CHECK(adc_continuous_channel_to_io(ADC_UNIT_1, ADC_CHANNEL_0+ix, &ionum));
    ESP_LOGI("main", "chan %u gpio %d", ix, ionum);
  }
#endif    
  // chan 0 gpio 36
  // chan 1 gpio 37
  // chan 2 gpio 38
  // chan 3 gpio 39
  // chan 4 gpio 32
  // chan 5 gpio 33
  // chan 6 gpio 34
  // chan 7 gpio 35

  // === set callbacks ===
  adc_continuous_evt_cbs_t cbs = {
    .on_conv_done = cbConvDone,
    .on_pool_ovf = cbOverflow
  };
  ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(adcHandle, &cbs, /*user_data*/NULL));
  
  defaultAdcConfig();
}

adc_atten_t att;
static void ATT_handlerDoSet(dispatcher_t* disp, char* inp, void* payload){
  char* args[1];
  if (!dispatcher_getArgs(disp, inp, 1, args)) return;

  if (!strcasecmp(args[0], "OFF")) att = ADC_ATTEN_DB_0;
  else if (!strcasecmp(args[0], "LOW")) att = ADC_ATTEN_DB_2_5;
  else if (!strcasecmp(args[0], "MID")) att = ADC_ATTEN_DB_6;
  else if (!strcasecmp(args[0], "HIGH")) att = ADC_ATTEN_DB_12;
  else errMan_throwARG_INVALID(&disp->errMan);
}

static void ATT_handlerGet(dispatcher_t* disp, char* inp, void* payload){
  if (!dispatcher_getArgsNull(disp, inp)) return;
  switch (att){
  case ADC_ATTEN_DB_0:
    dispatcher_connWriteCString(disp, "OFF"); break;
  case ADC_ATTEN_DB_2_5:
    dispatcher_connWriteCString(disp, "LOW"); break;
  case ADC_ATTEN_DB_6:
    dispatcher_connWriteCString(disp, "MID"); break;
  case ADC_ATTEN_DB_12:
    dispatcher_connWriteCString(disp, "HIGH"); break;
  default:
    assert(0);
  }
}

static void PAT_handlerDoSet(dispatcher_t* disp, char* inp, void* payload){
  char* args[8];
  size_t nArgs = 8;
  dispatcher_getArgsUpToN(disp, inp, &nArgs, args);
  if (!nArgs) goto failWithArgErr;
  nChanEnabled = 0;
  for (size_t ixArg = 0; ixArg < nArgs; ixArg++){
    uint32_t GPIO;
    if (!dispatcher_parseArgMinMax_UINT32(disp, args[ixArg], &GPIO, 32, 39)) goto fail;
    adc_channel_t chan;
    adc_unit_t adcUnit;
    if (adc_continuous_io_to_channel(GPIO, &adcUnit, &chan) != ESP_OK) goto failWithArgErr;
    if (adcUnit != ADC_UNIT_1) goto failWithArgErr;
       
    adc_pattern[nChanEnabled].atten = att;
    adc_pattern[nChanEnabled].channel = chan & 0x7;
    adc_pattern[nChanEnabled].unit = ADC_UNIT_1;
    adc_pattern[nChanEnabled].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    ++nChanEnabled;
    ESP_LOGI("main", "ADC:PAT enables chan %u", chan);
  } // while argument pairs
  return;
 failWithArgErr:
  errMan_throwARG_INVALID(&disp->errMan);
 fail:
  nChanEnabled = 0; // triggers config reset if started
}

static void PAT_handlerGet(dispatcher_t* disp, char* inp, void* payload){
  if (!dispatcher_getArgsNull(disp, inp)) return;
  char buf[16];
  for (size_t ixChan = 0; ixChan < nChanEnabled; ++ixChan){
    if (ixChan)
      dispatcher_connWrite(disp, " ", /*length*/1);
    int ionum;
    ESP_ERROR_CHECK(adc_continuous_channel_to_io(ADC_UNIT_1, adc_pattern[ixChan].channel, &ionum));
    sprintf(buf, "%i", ionum);
    dispatcher_connWrite(disp, buf, strlen(buf));
  }
  dispatcher_connWriteCString(disp, ""); // terminator
}

static void READ_handlerGet(dispatcher_t* disp, char* inp, void* payload){
  char* args[1];
  if (!dispatcher_getArgs(disp, inp, /*n*/1, args)) return;
  uint32_t nSamples;
  if (!dispatcher_parseArg_UINT32(disp, args[0], &nSamples))
    return;

  ESP_LOGI(TAG, "READ %lu", nSamples);
  // note: nChanEnabled==0 results in header for 0-byte binary chunk
  size_t nBytes = nSamples*nChanEnabled*2; //16 bit words
  dispatcher_connWriteBinaryHeader(disp, nBytes);

  if (!nChanEnabled){
    errMan_throwFAIL(&disp->errMan); // zero channels is result of invalid config => throw error if trying to use it for data acquisition
    return;
  }

  // === digi ctrl config ===
  for (size_t ixChan = 0; ixChan < nChanEnabled; ++ixChan)
    adc_pattern[ixChan].atten = att;

  adc_continuous_config_t dig_cfg = {
    .sample_freq_hz = convRateFudged_Hz,
    .conv_mode = ADC_CONV_SINGLE_UNIT_1,
    .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
    .pattern_num = nChanEnabled
  };

  dig_cfg.adc_pattern = adc_pattern;
  if (ESP_OK != adc_continuous_config(adcHandle, &dig_cfg)){
    ESP_LOGE(TAG, "adc_continuous_config (=>errMan)");
    errMan_throwFAIL(&disp->errMan);
    return;
  }
  
  // === start conversion ===
  nOverflow = 0;
  nCapt = 0;
  ESP_ERROR_CHECK(adc_continuous_flush_pool(adcHandle));
  ESP_ERROR_CHECK(adc_continuous_start(adcHandle));
  lastCaptBegin = esp_timer_get_time();

  // === forward data ===  
  while (nBytes){
    uint32_t nTryRead = nBytes < MAXTRANSFER_BYTES ? nBytes : MAXTRANSFER_BYTES;
    uint32_t nActualRead;

    while (nTryRead & 0x3) // prevent odd number of requested frames (driver fails assertion for size%4==0)
      ++nTryRead;
    
    esp_err_t ret = adc_continuous_read(adcHandle, transferBuf, nTryRead, &nActualRead, /*blocking*/ADC_MAX_DELAY);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "adc_continuous_read");
      ESP_ERROR_CHECK(ESP_FAIL);
    }
    
    // truncate excess requested data to force even frame size
    nActualRead = nActualRead <= nBytes ? nActualRead : nBytes;
    if (nActualRead == nBytes)
      lastCaptEnd = esp_timer_get_time();
	  
    dispatcher_connWrite(disp, (char*)transferBuf, nActualRead);
    assert(nActualRead <= nBytes);
    nBytes -= nActualRead;
  }
  
  // === stop conversion ===
  ESP_ERROR_CHECK(adc_continuous_stop(adcHandle));
}

static void LASTRATE_handlerGet(dispatcher_t* disp, char* inp, void* payload){
  if (!dispatcher_getArgsNull(disp, inp)) return;
  char buf[20];
  float duration_s = (float)(lastCaptEnd - lastCaptBegin)*1e-6f;
  float rate_SPS = (float)nCapt / duration_s; // callback is triggered by full buffers => use multiple of buffer size, not set amount
  sprintf(buf, "%1.3f", rate_SPS);
  dispatcher_connWriteCString(disp, buf);
}

static void RATE_handlerGet(dispatcher_t* disp, char* inp, void* payload){
  if (!dispatcher_getArgsNull(disp, inp)) return;
  dispatcher_connWriteUINT32(disp, convRate_Hz);
}

static void RATE_handlerDoSet(dispatcher_t* disp, char* inp, void* payload){
  char* args[1];
  if (!dispatcher_getArgs(disp, inp, /*n*/1, args)) return;
  uint32_t r_Hz;
  if (!dispatcher_parseArg_UINT32(disp, args[0], &r_Hz))
    return;

  uint32_t rFudged_Hz  = (float)r_Hz / RATE_FUDGE_FACTOR; // FW bug see https://esp32.com/viewtopic.php?f=2&t=40200
  if (rFudged_Hz < SOC_ADC_SAMPLE_FREQ_THRES_LOW)
    errMan_throwARG_INVALID(&disp->errMan);
  else if (rFudged_Hz > SOC_ADC_SAMPLE_FREQ_THRES_HIGH)
    errMan_throwARG_INVALID(&disp->errMan);
  else {    
    convRate_Hz = r_Hz;
    convRateFudged_Hz = rFudged_Hz;
  }
}

// === top level functionality ===
static dispatcherEntry_t ADC_dispEntries[] = {
  {.key="READ", .handlerPrefix=NULL, .handlerDoSet=NULL, .handlerGet=READ_handlerGet, .payload=NULL},
  {.key="RATE", .handlerPrefix=NULL, .handlerDoSet=RATE_handlerDoSet, .handlerGet=RATE_handlerGet, .payload=NULL},
  {.key="LASTRATE", .handlerPrefix=NULL, .handlerDoSet=NULL, .handlerGet=LASTRATE_handlerGet, .payload=NULL},
  {.key="PAT", .handlerPrefix=NULL, .handlerDoSet=PAT_handlerDoSet, .handlerGet=PAT_handlerGet, .payload=NULL},
  {.key="ATT", .handlerPrefix=NULL, .handlerDoSet=ATT_handlerDoSet, .handlerGet=ATT_handlerGet, .payload=NULL},
  {.key=NULL, .handlerPrefix=NULL, .handlerDoSet=NULL, .handlerGet=NULL, .payload=NULL} // end marker
};

void ADC_handlerPrefix(dispatcher_t* disp, char* inp, void* payload){
  if (!dispatcher_exec(disp, inp, ADC_dispEntries))
    errMan_throwSYNTAX(&disp->errMan);        
}

void ADC_handlerGet(dispatcher_t* disp, char* inp, void* payload){
  errMan_throwSYNTAX(&disp->errMan);
}

void ADC_handlerDoSet(dispatcher_t* disp, char* inp, void* payload){
  errMan_throwSYNTAX(&disp->errMan);
}
