/**
 * \file
 * <!--
 * This file is part of BeRTOS.
 *
 * Bertos is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As a special exception, you may use this file as part of a free software
 * library without restriction.  Specifically, if other files instantiate
 * templates or use macros or inline functions from this file, or you compile
 * this file and link it with other files to produce an executable, this
 * file does not by itself cause the resulting executable to be covered by
 * the GNU General Public License.  This exception does not however
 * invalidate any other reasons why the executable file might be covered by
 * the GNU General Public License.
 *
 * Copyright 2010 Develer S.r.l. (http://www.develer.com/)
 * All Rights Reserved.
 * -->
 *
 * \brief AFSK modem hardware-specific definitions.
 *
 *
 * \author Francesco Sacchi <batt@develer.com>
 */


#include "hw_afsk.h"

#include <net/afsk.h>
#include <cpu/irq.h>

#include <avr/io.h>
#include <avr/interrupt.h>

#include <string.h>

#define DC_FILTER_SHIFT 5
#define DC_FILTER_SIZE 32

typedef struct DCFilter
{
    int8_t buffer[32];
    uint8_t pos;
    uint8_t tail;
    uint8_t count;
} DCFilter;

static DCFilter dc_filter;

/**
 * Takes a normalized ADC (-512/+511) value and computes the attenuation
 * required to keep the average volume within (-128/+127) and returns the
 * attenuated result.
 */
INLINE int8_t input_attenuation(Afsk* af, int16_t adc)
{
    int16_t result = (adc >> af->input_volume_gain);

    if (result < -128) result = -128;
    else if (result > 127) result = 127;

    return result;
}

INLINE void capture(Afsk* af, int16_t adc)
{
    int8_t min = 127;
    int8_t max = -128;
    int16_t avg = 0;

    for (size_t i = 0; i < DC_FILTER_SIZE; ++i)
    {
        const int8_t tmp = dc_filter.buffer[i];
        min = tmp < min ? tmp : min;
        max = tmp > max ? tmp : max;
        avg += tmp;
    }

    avg >>= DC_FILTER_SHIFT;

    af->input_volume = max - min;

    if ((af->input_volume) >= af->squelch_level)
    {
        carrier_on(af);
    }
    else
    {
        carrier_off(af);
    }

    afsk_adc_isr(af, dc_filter.buffer[dc_filter.pos] - avg);

    dc_filter.buffer[dc_filter.pos++] = input_attenuation(af, adc - 512);
    if (dc_filter.pos == DC_FILTER_SIZE) dc_filter.pos = 0;
}

/*
 * Here we are using only one modem. If you need to receive
 * from multiple modems, you need to define an array of contexts.
 */
static Afsk *ctx;


void hw_afsk_adcInit(int ch, Afsk *_ctx)
{
	ctx = _ctx;
	ASSERT(ch <= 5);

	dc_filter.pos = 0;
	dc_filter.tail = DC_FILTER_SIZE;
	dc_filter.count = 0;
	memset(dc_filter.buffer, 0x80, DC_FILTER_SIZE);

	AFSK_STROBE_INIT();
	AFSK_STROBE_OFF();

    /* Set reference to AVCC (3.3V) and select ADC channel. */
    ADMUX = BV(REFS0) | ch;

    DDRC &= ~BV(ch);
    PORTC &= ~BV(ch);
    DIDR0 |= BV(ch);

    // Set prescaler to 128 so we have a 125KHz clock source.
    // This provides almost exactly 9600 conversions a second.
    ADCSRA = (BV(ADPS2) | BV(ADPS1) | BV(ADPS0));

    // Put the ADC into free-running mode.
    ADCSRB &= ~(BV(ADTS2) | BV(ADTS1) | BV(ADTS0));

    // Set signal source to free running, start the ADC, start
    // conversion and enable interrupt.
    ADCSRA |= (BV(ADATE) | BV(ADEN) | BV(ADSC) | BV(ADIE));
}

bool hw_afsk_dac_isr;

/*
 * This is how you declare an ISR.
 */
DECLARE_ISR(ADC_vect)
{
	capture(ctx, ADC);
}

#if CONFIG_AFSK_PWM_TX == 1
DECLARE_ISR(TIMER0_OVF_vect)
{
    OCR0A = afsk_dac_isr(ctx);             // uses timer 0 on port D bit 5
}
#else
DECLARE_ISR(TIMER2_COMPA_vect)
{
    // TCNT2 = DAC_TIMER_VALUE;
    PORTD = ((PORTD & 0x03) | (afsk_dac_isr(ctx) & 0xF0));
}
#endif
