#define F_CPU 11059200LU

#include <stdio.h>
#include <stdlib.h>
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <inttypes.h>


volatile int adc_val = 0 ;
volatile unsigned char flag = 0 ;
char msg[17];
ISR(ADC_vect)
{
    adc_val = ADC;  

    // IMPORTANT: keep your working behavior
    TIFR |= (1 << OCF1B);  

    flag = 1;
}

int main(void)
{
    //---sampling_config : this function will config the ADC and timers for desired 
    //now your system is always taking samples from any voltage input at ADC3 mike 
    sampling_config_sampling_rate_8khz();

    sei();

    // -------- LCD --------
    LCD_Init();
    LCD_Gotoxy(0,0);

    float analog_val = 0.0;
    float centered = 0.0;
    
    unsigned int number_of_sample = 1000;
    unsigned int Buffer_size = 125 ;
    unsigned int buffer_index = 0 ;
    
    float STE[8] = {0};
    float ZCE[8] = {0};
    unsigned char last_sample_sign = 0 ;
    unsigned char first_sample = 1  ; 

    while (1)
    {
        if(flag){ //new sample is read 
            flag = 0 ; 

            analog_val = (adc_val/1024.0) * 5 ; 
            // Center around mic bias (MAX9814 ~1.25V)
            centered = analog_val - 1.25;
            //no one is taking this is just a noise 
            //this is no overlapping
            if(analog_val < 0.1){
                continue;
            }
            else{
                //real voice is going in lets take  
                if(Buffer_size){    
                    STE[buffer_index] += centered*centered ;
                     
                    unsigned char curr_sample_sing ;
                    if(centered > 0){
                        curr_sample_sing = 1 ;
                    }
                    else{
                        curr_sample_sing = 0 ;
                    }
                    if(first_sample){
                        first_sample = 0 ;
                        last_sample_sign = curr_sample_sing;
                    }
                    if(!(curr_sample_sing == last_sample_sign)){
                        ZCE[buffer_index] += 1 ;
                        last_sample_sign = curr_sample_sing ;
                    }

                }
                else{
                    Buffer_size = 125 ; 
                    STE[buffer_index] /= Buffer_size ;
                    ZCE[buffer_index] /= Buffer_size ;
                    buffer_index++;
                    STE[buffer_index] += centered*centered ;
                    first_sample = 1 ; 
                    sprintf(msg , "E:%1.3f", STE[buffer_index-1]);
                    LCD_String_xy(0, 0, msg);
                    //sprintf(msg, "Z:%1.3f", ZCR);
                }
                if(number_of_sample == 0){
                    Buffer_size = 125 ;
                    buffer_index = 0 ;
                    first_sample = 1 ; 
                    //our 2 arrays full we can calssify here 
                }
                Buffer_size--;
                number_of_sample--;
            }
        }
    }

    return 0;
}