#define F_CPU 11059200LU
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "External_libraries/uart.h"
#include "External_libraries/goertzel.h"

#define SPEECH_STE_THRESHOLD 70u
#define FRICATIVE_GOERTZEL_THRESHOLD 2u
#define ZCE_VAD_THRESHOLD 15u

typedef enum { IDLE, RECORDING, DONE } State;

/* ── Ping-pong buffers: written by ISR, read by main ─────────────────────── */
volatile int16_t adc_buf[2][128];
volatile uint16_t adc_raw_buf[2][128];
volatile uint8_t buffer_ready = 0;
volatile uint16_t dc_bias_snapshot = 0;

volatile State state = IDLE;

/* ─────────────────────────────────────────────────────────────────────────── */
ISR(ADC_vect)
{
    static uint32_t dc_ema   = 256ul * 1024ul;
    static uint8_t  buf_idx  = 0;
    static uint8_t  samp_idx = 0;

    uint16_t raw = ADC;
    TIFR |= (1 << OCF1B);
    dc_ema += raw;
    dc_ema -= (dc_ema >> 10);
    dc_bias_snapshot = (uint16_t)(dc_ema >> 10);

    adc_raw_buf[buf_idx][samp_idx] = raw;
    adc_buf[buf_idx][samp_idx] = (int16_t)raw - (int16_t)(dc_ema >> 10);

    if (++samp_idx == 128) {
        samp_idx     = 0;
        buffer_ready = buf_idx + 1;
        buf_idx     ^= 1;
    }
}

/* ─────────────────────────────────────────────────────────────────────────── */
int main(void)
{
    UART_init(230400);
    UART_stdio_init();

    TCCR1A = 0x00;
    TCCR1B = (1 << WGM12) | (1 << CS11);
    OCR1A  = 172;
    OCR1B  = 172;

    ADMUX  = (1 << MUX1) | (1 << MUX0);
    ADCSRA = (1 << ADEN) | (1 << ADSC) | (1 << ADATE) |
             (1 << ADIE) | (1 << ADPS2) | (1 << ADPS1);
    SFIOR &= ~(0x07 << ADTS0);
    SFIOR |=  (1 << ADTS2) | (1 << ADTS0);

    sei();

    // ── IDLE VAD state ────────────────────────────────────────────────────────
    uint32_t noise_floor_ema = 3ul * 1024ul;
    uint8_t  vad_sign        = 0;

    // ── RECORDING state ───────────────────────────────────────────────────────
    uint8_t  block_count                     = 0;
    uint8_t  first_block                     = 1;
    uint8_t  consec_silent                   = 0;
    uint16_t trigger_raw_buf[128];
    uint8_t  trigger_frame_pending           = 0;

    printf("START_READY\r\n");

    while (1)
    {
        if (!buffer_ready) continue;

        uint8_t ready = buffer_ready;
        buffer_ready  = 0;
        const volatile int16_t *buf = adc_buf[ready - 1];
        const volatile uint16_t *raw_buf = adc_raw_buf[ready - 1];

        // ── IDLE: block-level VAD ─────────────────────────────────────────────
        if (state == IDLE)
        {
            uint32_t sum_abs = 0;
            uint8_t  zce_cnt = 0;
            int16_t  dz      = (int16_t)((noise_floor_ema * 3ul) >> 10);

            for (uint8_t k = 0; k < 128; k++)
            {
                int16_t  s = buf[k];
                uint16_t a = (s < 0) ? (uint16_t)(-s) : (uint16_t)s;
                sum_abs += a;

                noise_floor_ema += a;
                noise_floor_ema -= (noise_floor_ema >> 10);

                if      (s >  dz && vad_sign == 0) { vad_sign = 1; zce_cnt++; }
                else if (s < -dz && vad_sign == 1) { vad_sign = 0; zce_cnt++; }
            }

            uint8_t avg_ste = (uint8_t)(sum_abs >> 7);
            if (avg_ste > SPEECH_STE_THRESHOLD || zce_cnt > ZCE_VAD_THRESHOLD)
            {
                state         = RECORDING;
                block_count   = 0;
                first_block   = 1;
                consec_silent = 0;
                vad_sign      = 0;
                for (uint8_t k = 0; k < 128; k++)
                    trigger_raw_buf[k] = raw_buf[k];
                trigger_frame_pending = 1;

                printf("START\r\n");
                uint16_t dc_bias_val = dc_bias_snapshot;
                UART_putChar((char)((dc_bias_val >> 8) & 0xFF), stdout);
                UART_putChar((char)(dc_bias_val & 0xFF), stdout);
                /* Fall through: process this same buffer as recording block 0 */
            }
        }

        // ── RECORDING: block feature extraction + early-stop detection ────────
        if (state == RECORDING)
        {
            /* 1. Compute current block: STE, ZCE, Goertzel pseudo-magnitudes. */
            uint32_t    sum_abs  = 0;
            uint8_t     zce_cnt  = 0;
            uint8_t     last_sgn = (buf[0] > 0) ? 1 : 0;
            GoertzelState gs;
            uint8_t use_trigger_frame = (first_block && trigger_frame_pending);
            goertzel_reset(&gs);

            for (uint8_t k = 0; k < 128; k++)
            {
                int16_t s = buf[k];
                uint16_t a = (s < 0) ? (uint16_t)(-s) : (uint16_t)s;
                sum_abs += a;

                uint8_t sgn = (s > 0) ? 1 : 0;
                if (sgn != last_sgn) { zce_cnt++; last_sgn = sgn; }

                goertzel_update(&gs, s);

                /* Transmit samples */
                uint16_t raw = use_trigger_frame ? trigger_raw_buf[k] : raw_buf[k];
                UART_putChar((char)((raw >> 8) & 0xFF), stdout);
                UART_putChar((char)(raw & 0xFF), stdout);
            }

            uint16_t curr_goertzel[GOERTZEL_NUM_BINS];
            for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
                curr_goertzel[b] = goertzel_pseudo_magnitude(&gs, b);

            if (first_block)
            {
                /* Block 0: nothing to analyze yet. */
                first_block = 0;
                trigger_frame_pending = 0;
            }
            else
            {
                /* 2. Silence check for early stop. */
                uint8_t ste_silent = ((sum_abs >> 7) <= SPEECH_STE_THRESHOLD);
                uint8_t g_silent   = 1;
                for (uint8_t b = 0; b < GOERTZEL_NUM_BINS; b++)
                {
                    if ((curr_goertzel[b] >> GOERTZEL_SHIFTS[b]) > FRICATIVE_GOERTZEL_THRESHOLD)
                    {
                        g_silent = 0;
                        break;
                    }
                }
                if (ste_silent && g_silent) consec_silent++;
                else                        consec_silent = 0;

                /* 3. Check stop conditions. */
                if (block_count >= 10 && consec_silent >= 12)
                    state = DONE;

                block_count++;
            }
        }

        // ── DONE: finalize ────────────────────────────────────────────────────
        if (state == DONE)
        {
            printf("END\r\n");
            state = IDLE;
            vad_sign = 0;
            trigger_frame_pending = 0;
        }
    }
}
