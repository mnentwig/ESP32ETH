#include "UREPL_ADC.h"
#include "feature_errMan.h"
#include "esp_err.h" // ESP_ERROR_CHECK
#include "esp_log.h"
#include "dispatcher.h"
#include "string.h" // memset

#include "esp_adc/adc_continuous.h"

extern errMan_t errMan; // required feature (for dealing with incorrect input)
static const char *TAG = "ADC";

#define MAXTRANSFER_BYTES 1024
#define NCHAN 2	
static uint8_t transferBuf[MAXTRANSFER_BYTES];
static adc_continuous_handle_t adcHandle;
static uint32_t nOverflow;
static uint32_t nCapt;

static bool IRAM_ATTR cbConvDone(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data){
	nCapt += edata->size / sizeof(adc_digi_output_data_t);
	return 0;
}

static bool IRAM_ATTR cbOverflow(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data){
	++nOverflow;
    return 0;
}

void ADC_init(){	
	nOverflow = 0;
	nCapt = 0;

	// === ADC handle ===
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 4*1024,
        .conv_frame_size = MAXTRANSFER_BYTES,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &adcHandle));

	// === digi ctrl config ===
    adc_continuous_config_t dig_cfg = {
		.sample_freq_hz = 40000,
		.conv_mode = ADC_CONV_SINGLE_UNIT_1,
		.format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
		.pattern_num = NCHAN
    };

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

    adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {0};
    adc_pattern[0].atten = ADC_ATTEN_DB_6;
	adc_pattern[0].channel = ADC_CHANNEL_6 & 0x7;
    adc_pattern[0].unit = ADC_UNIT_1;
    adc_pattern[0].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    adc_pattern[1].atten = ADC_ATTEN_DB_6;
	adc_pattern[1].channel = ADC_CHANNEL_7 & 0x7;
    adc_pattern[1].unit = ADC_UNIT_1;
    adc_pattern[1].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    dig_cfg.adc_pattern = adc_pattern;
    ESP_ERROR_CHECK(adc_continuous_config(adcHandle, &dig_cfg));

	// === set callbacks ===
    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = cbConvDone,
		.on_pool_ovf = cbOverflow
    };
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(adcHandle, &cbs, /*user_data*/NULL));
}

void READ_handlerGet(dispatcher_t* disp, char* inp, void* payload){
  char* args[1];
  if (!dispatcher_getArgs(disp, inp, /*n*/1, args)) return;
  uint32_t nSamples;
  if (!dispatcher_parseArg_UINT32(disp, args[0], &nSamples))
    return;

  ESP_LOGI(TAG, "READ %lu", nSamples);
  size_t nBytes = nSamples*2; //16 bit words
  dispatcher_connWriteBinaryHeader(disp, nBytes);

  // === start conversion ===
  ESP_ERROR_CHECK(adc_continuous_flush_pool(adcHandle));
  ESP_ERROR_CHECK(adc_continuous_start(adcHandle));

  // === forward data ===  
  while (nBytes){
	  uint32_t nTryRead = nBytes < MAXTRANSFER_BYTES ? nBytes : MAXTRANSFER_BYTES;
	  uint32_t nActualRead;
	  esp_err_t ret = adc_continuous_read(adcHandle, transferBuf, nTryRead, &nActualRead, /*blocking*/ADC_MAX_DELAY);
	  if (ret != ESP_OK) {
		ESP_LOGE(TAG, "adc_continuous_read");
		ESP_ERROR_CHECK(ESP_FAIL);
	  }
	  dispatcher_connWrite(disp, (char*)transferBuf, nActualRead);
	  assert(nActualRead <= nBytes);
	  nBytes -= nActualRead;
  }
  
  // === stop conversion ===
  ESP_ERROR_CHECK(adc_continuous_stop(adcHandle));
}

void ADC_handlerGet(dispatcher_t* disp, char* inp, void* payload){
  errMan_throwSYNTAX(&disp->errMan);
}

void ADC_handlerDoSet(dispatcher_t* disp, char* inp, void* payload){
  errMan_throwSYNTAX(&disp->errMan);
}

 static dispatcherEntry_t ADC_dispEntries[] = {
  {.key="READ", .handlerPrefix=NULL, .handlerDoSet=NULL, .handlerGet=READ_handlerGet, .payload=NULL},
  {.key=NULL, .handlerPrefix=NULL, .handlerDoSet=NULL, .handlerGet=NULL, .payload=NULL} // end marker
};

void ADC_handlerPrefix(dispatcher_t* disp, char* inp, void* payload){
  if (!dispatcher_exec(disp, inp, ADC_dispEntries))
    errMan_throwSYNTAX(&disp->errMan);        
}