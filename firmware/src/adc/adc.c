
//  * This file is part of the Tinymovr-Firmware distribution
//  * (https://github.com/yconst/tinymovr-firmware).
//  * Copyright (c) 2020-2023 Ioannis Chatzikonstantinou.
//  *
//  * This program is free software: you can redistribute it and/or modify
//  * it under the terms of the GNU General Public License as published by
//  * the Free Software Foundation, version 3.
//  *
//  * This program is distributed in the hope that it will be useful, but
//  * WITHOUT ANY WARRANTY; without even the implied warranty of
//  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
//  * General Public License for more details.
//  *
//  * You should have received a copy of the GNU General Public License
//  * along with this program. If not, see <http://www.gnu.org/licenses/>.

#include <math.h>
#include <string.h>
#include <src/motor/motor.h>
#include <src/controller/controller.h>
#include <src/adc/adc.h>

#define AIO0to5_DIFF_AMP_MODE 0x40u
#define AIO6789_IO_MODE 0x00u
#define HP_DIS_LP_EN_PR1 0x20u

#define AIO_INPUT 0x00u
#define AIO_OUTPUT 0x20u

#define AIO_ACTIVE_H 0x00u
#define AIO_ACTIVE_L 0x08u

#define OFFSET_EN 0x08u
#define OFFSET_DIS 0x00u

#define CAL_OFFSET_EN 0x04u
#define CAL_OFFSET_DIS 0x00u

#define LP_HP_EN_1us 0x01u

#define LP54_EN 0x04u
#define LP32_EN 0x02u
#define LP10_EN 0x01u

#define LP_PROT (LP54_EN | LP32_EN | LP10_EN)

#define LSPR1EN 0x20u

// Digital_MUX_Setting (DMUX_DOUTx_Default)
#define DMUX_DOUTx_Default 0

void ADC_AIO_Init(void);
void ADC_DTSE_Init(void);

static ADCState adc_state = {0};

static ADCConfig adc_config = {
    .I_phase_offset = {0},
    .Iphase_limit = 60.0f,
    .I_phase_offset_tau = 0.2f,
    .I_phase_offset_k = 0.0f
};

void ADC_Init(void)
{
    adc_config.I_phase_offset_k = PWM_PERIOD_S / adc_config.I_phase_offset_tau;
    // --- Begin CAFE2 Initialization

    // Write all CAFE registers
    // If any power manager error bits set on startup, clear them
    if (pac5xxx_tile_register_read(ADDR_STATUS) != 0)
    {
        pac5xxx_tile_register_write(ADDR_STATUS, 0xFF);
    }

    pac5xxx_tile_register_write(ADDR_PWRCTL, 0x04);                   // Wake Up Timer == 1sec
    pac5xxx_tile_register_write(ADDR_MISC, MODULE_MISC_BITS_DEFAULT); // Set MCU Alive, Enable Signal Manager

    // Reset AFE registers to default state
    pac5xxx_tile_register_write(ADDR_PROTINTM, 0x00);
#if !defined(PAC5556) && !defined(PAC5527)
    pac5xxx_tile_register_write(ADDR_CFGDRV0, 0x00);
#endif
    pac5xxx_tile_register_write(ADDR_CFGDRV1, 0x00);
    pac5xxx_tile_register_write(ADDR_CFGDRV2, 0x00);
    pac5xxx_tile_register_write(ADDR_CFGDRV3, 0x00);
    pac5xxx_tile_register_write(ADDR_SIGSET, 0x00);
    pac5xxx_tile_register_write(ADDR_SHCFG1, 0x00); // Disable ADC Scan (EMUX), Disable ADC Buffer

    // Clear any error bits
    pac5xxx_tile_register_write(ADDR_PROTSTAT, 0xFF); // clear any set bits

    // Configure PR event driver settings
#if !defined(PAC5556) && !defined(PAC5527)
    pac5xxx_tile_register_write(ADDR_CFGDRV0, 0x00); // Diff Amp AIO10/32 Prop Delay 0 ns
#endif
    // Diff Amp AIO54 Prop Delay 0 ns (0x00),
    // Disable HS drivers on H/L-PROT (0x08),
    // Disable LS drivers on H/L-PROT (0x04),
    // Enable Break-Before-Make (0x01)
    pac5xxx_tile_register_write(ADDR_CFGDRV1, 0x00);
    pac5xxx_tile_register_write(ADDR_CFGDRV2, 0x00);
    pac5xxx_tile_register_write(ADDR_CFGDRV3, 0x00);

    // Configure overcurrent protection
    uint16_t lpdac_val = (uint16_t)(adc_config.Iphase_limit * ONE_OVER_SHUNT_SCALING_FACTOR);
    pac5xxx_tile_register_write(ADDR_LPDACH, (lpdac_val >> 2) & 0xFFu); // 8b MSB
    pac5xxx_tile_register_write(ADDR_LPDACL, lpdac_val & 0x03u);        // 2b LSB

    // LPROT interrupt flag clear
    pac5xxx_tile_register_write(ADDR_PROTSTAT, LP_PROT);
    // Enable protection interrupt
    pac5xxx_tile_register_write(ADDR_PROTINTM, LP_PROT);

    // Disable all HS and LS drivers upon protection threshold trip
    // CFGDRV1: L-PROT1 will disable HS and LS drivers
    pac5xxx_tile_register_write(ADDR_CFGDRV1, LSPR1EN);
    // Enable comparator hysteresis on LPROT comparators
    pac5xxx_tile_register_write(ADDR_SIGSET, (0x08 | 0x04));

    // EMUX configuration
    // Enable EMUX (0x10), Enable ADC Buffer (0x08), Enable S/H for Diff Amp 10/32/54 (0x07)
    pac5xxx_tile_register_write(ADDR_SHCFG1, (0x10 | 0x08 | 0x07));

    // If any power manager fault bits set on startup, clear them
    if (pac5xxx_tile_register_read(ADDR_FAULT) != 0)
    {
        pac5xxx_tile_register_write(ADDR_FAULT, 0xFF);
    }

    // --- End CAFE2 Initialization

    pac5xxx_adc_enable(0);

    // EMUX configuration
    // Configure device IO for EMUX
    pac5xxx_adc_config_emux_io();

    // Configure ADC input pin PG7(AD0) (==0x01) and
    // PF6 (=0x40) to read analog input from AFE
    pac5xxx_adc_config_io(0x01 | 0x40);

    // Configure EMUX to do conversions from ADC sequencer, /2 EMUX divider (HCLK=50MHz/2 = 25MHz)
    pac5xxx_adc_emux_config(ADCEMUXCTL_DTSE_SEQ, ADCEMUXCTL_EMUXDIV_DIV16);
    pac5xxx_adc_config(ADCCTL_MODE_DTSE, ADCCTL_CLKDIV_DIV8, 0);

    // AIO configuration
    ADC_AIO_Init();

    // Sequencer Configuration
    ADC_DTSE_Init();

    // Set ADC interrupt priority and enable ADC interrupt in the NVIC
    NVIC_SetPriority(ADC0_IRQn, 1);
    NVIC_EnableIRQ(ADC0_IRQn);

    // Enable ADC
    pac5xxx_adc_enable(1);
    // Start ADC sequencer
    pac5xxx_adc_start();
}

void ADC_AIO_Init(void)
{
    // TODO: Verify if Low-side protection has any effect
    pac5xxx_tile_register_write(ADDR_CFGAIO0, AIO0to5_DIFF_AMP_MODE | ADC_GAIN_VAL | LP_HP_EN_1us);
    pac5xxx_tile_register_write(ADDR_CFGAIO1, HP_DIS_LP_EN_PR1 | OFFSET_EN | CAL_OFFSET_DIS | LP_HP_EN_1us);

    pac5xxx_tile_register_write(ADDR_CFGAIO2, AIO0to5_DIFF_AMP_MODE | ADC_GAIN_VAL | LP_HP_EN_1us);
    pac5xxx_tile_register_write(ADDR_CFGAIO3, HP_DIS_LP_EN_PR1 | OFFSET_EN | CAL_OFFSET_DIS | LP_HP_EN_1us);

    pac5xxx_tile_register_write(ADDR_CFGAIO4, AIO0to5_DIFF_AMP_MODE | ADC_GAIN_VAL | LP_HP_EN_1us);
    pac5xxx_tile_register_write(ADDR_CFGAIO5, HP_DIS_LP_EN_PR1 | OFFSET_EN | CAL_OFFSET_DIS | LP_HP_EN_1us);
}

void ADC_DTSE_Init(void)
{
    //========================================================================================================
    // Setup DTSE Sequence Trigger - See "DTSE Trigger Mapping Table" in the PAC55XX Family User's Guide
    //========================================================================================================

    //  Timer A CCR2 = Trigger 0 and initiate sequence starting at DTSE Entry 0
    PAC55XX_ADC->DTSETRIGENT0TO3.FORCE0 = 0;                          // Reset to 0
    PAC55XX_ADC->DTSETRIGENT0TO3.TRIG0CFGIDX = 0;                     // DTSE Trigger 0 Sequence Configuration Entry Index
    PAC55XX_ADC->DTSETRIGENT0TO3.TRIG0EDGE = ADCDTSE_TRIGEDGE_RISING; // PWMA0 rising edge

    pac5xxx_timer_a_ccctr0_value_set(1);

    //===== Setup DTSE Sequence A (sense offset) - Starts at Entry 0 =====
    pac5xxx_dtse_seq_config(0, ADC0, EMUX_AB10, 0, 0);
    pac5xxx_dtse_seq_config(1, ADC0, EMUX_AB10, 0, 0);
    pac5xxx_dtse_seq_config(2, ADC0, EMUX_AB13, 0, 0); // Get VPTAT result at DTSERES2
    pac5xxx_dtse_seq_config(3, ADC0, EMUX_AB13, 0, 0);
    pac5xxx_dtse_seq_config(4, ADC0, EMUX_AIO10, 0, 0); // Get VBus result at DTSERES4
    pac5xxx_dtse_seq_config(5, ADC0, EMUX_AIO10, 0, 0);
    pac5xxx_dtse_seq_config(6, ADC0, EMUX_AIO32, 0, 0); // Get result at DTSERES6
    pac5xxx_dtse_seq_config(7, ADC0, EMUX_AIO32, 0, 0);
    pac5xxx_dtse_seq_config(8, ADC0, EMUX_AIO54, 0, 0); // Get result at DTSERES8
    pac5xxx_dtse_seq_config(9, ADC0, EMUX_AIO54, 0, 0);
    pac5xxx_dtse_seq_config(10, ADC0, 0, 0, SEQ_END); // Get result at DTSERES10

    //  Timer A CCR2 = Trigger 0 and initiate sequence starting at DTSE Entry 0
    PAC55XX_ADC->DTSETRIGENT0TO3.FORCE1 = 0;                          // Reset to 0
    PAC55XX_ADC->DTSETRIGENT0TO3.TRIG1CFGIDX = 12;                    // DTSE Trigger 1 Sequence Configuration Entry Index
    PAC55XX_ADC->DTSETRIGENT0TO3.TRIG1EDGE = ADCDTSE_TRIGEDGE_RISING; // PWMA0 rising edge

    pac5xxx_timer_a_ccctr1_value_set((timer_freq_hz / 2 / PWM_FREQ_HZ) - 2);

    //===== Setup DTSE Sequence B (sense current) - Starts at Entry 12 =====
    pac5xxx_dtse_seq_config(12, ADC0, EMUX_AIO10, 0, 0);
    pac5xxx_dtse_seq_config(13, ADC0, EMUX_AIO10, 0, 0);
    pac5xxx_dtse_seq_config(14, ADC0, EMUX_AIO32, 0, 0); // Get result at DTSERES14
    pac5xxx_dtse_seq_config(15, ADC0, EMUX_AIO32, 0, 0);
    pac5xxx_dtse_seq_config(16, ADC0, EMUX_AIO54, 0, 0); // Get result at DTSERES16
    pac5xxx_dtse_seq_config(17, ADC0, EMUX_AIO54, 0, 0);
    pac5xxx_dtse_seq_config(18, ADC0, 0, ADC_IRQ0_EN, SEQ_END); // Get result at DTSERES18, Interrupt
}

TM_RAMFUNC int16_t adc_get_mcu_temp(void)
{
    return adc_state.temp;
}

TM_RAMFUNC void ADC_GetPhaseCurrents(struct FloatTriplet *phc)
{
    phc->A = adc_state.I_phase_meas.A;
    if (motor_phases_swapped())
    {
        phc->B = adc_state.I_phase_meas.C;
        phc->C = adc_state.I_phase_meas.B;
    }
    else
    {
        phc->B = adc_state.I_phase_meas.B;
        phc->C = adc_state.I_phase_meas.C;
    }
}

TM_RAMFUNC void ADC_update(void)
{
    switch (controller_get_state())
    {
        case STATE_CALIBRATE:
        {
            adc_config.I_phase_offset.A += (((float)PAC55XX_ADC->DTSERES6.VAL * SHUNT_SCALING_FACTOR) - adc_config.I_phase_offset.A) * adc_config.I_phase_offset_k;
            adc_config.I_phase_offset.B += (((float)PAC55XX_ADC->DTSERES8.VAL * SHUNT_SCALING_FACTOR) - adc_config.I_phase_offset.B) * adc_config.I_phase_offset_k;
            adc_config.I_phase_offset.C += (((float)PAC55XX_ADC->DTSERES10.VAL * SHUNT_SCALING_FACTOR) - adc_config.I_phase_offset.C) * adc_config.I_phase_offset_k;
        }
        case STATE_CL_CONTROL:
        {
            const float i_a = (((float)PAC55XX_ADC->DTSERES14.VAL * SHUNT_SCALING_FACTOR) - adc_config.I_phase_offset.A);
            const float i_b = (((float)PAC55XX_ADC->DTSERES16.VAL * SHUNT_SCALING_FACTOR) - adc_config.I_phase_offset.B);
            const float i_c = (((float)PAC55XX_ADC->DTSERES18.VAL * SHUNT_SCALING_FACTOR) - adc_config.I_phase_offset.C);
            adc_state.I_phase_meas.A = ((1.0f - I_FILTER_K) * i_a) - (I_FILTER_K * (i_b + i_c));
            adc_state.I_phase_meas.B = ((1.0f - I_FILTER_K) * i_b) - (I_FILTER_K * (i_a + i_c));
            adc_state.I_phase_meas.C = ((1.0f - I_FILTER_K) * i_c) - (I_FILTER_K * (i_a + i_b));
        }
        default: break;
    }
    
    // Internal MCU temperature sensor reading at FTTEMP temperature in ADC counts.
    uint16_t TTEMPS = 0;
    memcpy(&TTEMPS, (const size_t *)0x0010041C, sizeof(uint16_t));
    // Temperature in oC at time of internal temperature sensor
    const int32_t FTTEMP = 27; // READ_UINT16(0x0010041E);
    const int32_t temp_val = (int32_t)(PAC55XX_ADC->DTSERES2.VAL);
    adc_state.temp = ((((FTTEMP + 273) * ((temp_val * 100) + 12288)) / (((int16_t)TTEMPS * 100) + 12288)) - 273);
}

ADCConfig *ADC_get_config(void)
{
    return &adc_config;
}

void ADC_restore_config(ADCConfig *config_)
{
    adc_config = *config_;
}