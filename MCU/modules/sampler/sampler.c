#include "sampler.h"
#include "adc.h"

void sampling_config_sampling_rate_8khz(){
    /*
      init a adc conversion with fs = 8khz and it triggered by compare match on B of timer1  
    */
   TIMER1_CONFIG_CTC_8HZ_Square_wave();
   ADC_CONFIG_ADC3_TIMER1_B_MATCH();

}