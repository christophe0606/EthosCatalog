// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0 AND LicenseRef-Alif-Software-License
// New Arm contributions are Apache-2.0. The Alif-derived receive routine
// remains under the Alif terms; this notice does not relicense vendor code.
// See NOTICE.md and LICENSES/Alif-Software-License.txt.
// Uses the Alif Ensemble pack's APS512XXN and OSPI XIP implementations.
// Bounded HyperBus receive adapted from the pack's ospi.c:
// Copyright (C) 2023 Alif Semiconductor - All Rights Reserved.
// Use, distribution and modification permitted under the Alif Semiconductor
// Software License Agreement: https://alifsemi.com/license
#include "phonon_psram.h"
#include "ospi_psram_xip.h"
#include "APS512XXN_PSRAM.h"
#include "Driver_IO.h"
#include "pinconf.h"
#include "ospi.h"
#include "sys_utils.h"
#include "RTE_Components.h"
#include CMSIS_device_header

volatile uint32_t phonon_psram_verified_bytes;
volatile uint32_t phonon_psram_error_address;
volatile uint32_t phonon_psram_baud;
volatile uint32_t phonon_hyperram_id0;
volatile uint32_t phonon_hyperram_id1;
volatile uint32_t phonon_psram_probe_stage;
volatile uint32_t phonon_hyperram_samples[24][4];
volatile uint32_t phonon_hyperram_counts[24][4];
volatile uint32_t phonon_hyperram_words[24][4][4];
volatile uint32_t phonon_hyperram_rxds;
volatile uint32_t phonon_hyperram_cr0[2];
volatile uint32_t phonon_hyperram_wait_cycles;

// Stable inspection point before the first transaction through the RAM window.
__attribute__((noinline)) void phonon_psram_test_begin(void) {
    __DSB();
}

static int receive_hyperram_register(OSPI_Type *ospi, uint32_t ca_high, uint32_t address, uint32_t *id,
                                    uint32_t *received, volatile uint32_t *words)
{
    ospi_disable(ospi);
    uint32_t val = ospi->OSPI_CTRLR0;
    val &= ~(SPI_CTRLR0_SPI_FRF_MASK | SPI_CTRLR0_TMOD_MASK | SPI_CTRLR0_SSTE_MASK);
    ospi->OSPI_CTRLR0 = val | (SPI_FRF_OCTAL << SPI_CTRLR0_SPI_FRF) |
        SPI_CTRLR0_TMOD_RECEIVE_ONLY | SPI_CTRLR0_SPI_HYPERBUS_ENABLE;
    // Register-space bursts repeat the same 16-bit register (ISSI Rev A1 p20).
    ospi->OSPI_CTRLR1 = 3;
    ospi->OSPI_SPI_CTRLR0 = SPI_TRANS_TYPE_FRF_DEFINED |
        (1 << SPI_CTRLR0_SPI_DDR_EN_OFFSET) | (3 << 24) |
        (SPI_INST_L_0_BIT << SPI_CTRLR0_INST_L_OFFSET) |
        (SPI_ADDR_L_48_BIT << SPI_CTRLR0_ADDR_L_OFFSET) |
        (12 << SPI_CTRLR0_WAIT_CYCLES_OFFSET);
    ospi_enable(ospi);
    ospi->OSPI_DR[0] = ca_high;
    ospi->OSPI_DR[0] = address;
    for (unsigned us = 0; us < 10000; ++us) {
        if (!ospi_busy(ospi)) {
            *received = 0;
            while (ospi->OSPI_RXFLR && *received < 16) {
                *id = ospi->OSPI_DR[0];
                if (*received < 4) words[*received] = *id;
                ++*received;
            }
            if (!*received || ospi->OSPI_RXFLR) return -1;
            return 0;
        }
        sys_busy_loop_us(1);
    }
    ospi_disable(ospi);
    return -1;
}

static int32_t identify_hyperram(OSPI_Type *ospi, AES_Type *aes)
{
    ospi_set_dfs(ospi,16);
    phonon_psram_probe_stage = 2;
    int selected = -1;
    for (unsigned delay = 0; delay < 24; ++delay) {
        aes_set_rxds_delay(aes, delay);
        for (unsigned sample = 0; sample < 4; ++sample) {
            uint32_t id = 0, count = 0;
            int result = receive_hyperram_register(ospi, 0xE0000000U, sample / 2, &id, &count,
                phonon_hyperram_words[delay][sample]);
            phonon_hyperram_samples[delay][sample] = result ? ~0U : id;
            phonon_hyperram_counts[delay][sample] = count;
        }
        uint32_t id0 = phonon_hyperram_samples[delay][0];
        uint32_t id1 = phonon_hyperram_samples[delay][2];
        if (id0 == phonon_hyperram_samples[delay][1] &&
            id1 == phonon_hyperram_samples[delay][3] &&
            (id0 & 15) == 3 && (id1 & 15) == 1 &&
            ((id0 >> 8) & 31) >= 13 && ((id0 >> 4) & 15) >= 7 &&
            (selected < 0 || delay == 8))
            selected = (int)delay;
    }
    phonon_psram_probe_stage = 3;
    if (selected < 0) return -1;
    phonon_hyperram_rxds = (uint32_t)selected;
    aes_set_rxds_delay(aes, (uint32_t)selected);
    phonon_hyperram_id0 = phonon_hyperram_samples[selected][0];
    phonon_hyperram_id1 = phonon_hyperram_samples[selected][2];
    // ID0 row/column fields count address bits minus one; each word is 16 bits.
    unsigned rows = ((phonon_hyperram_id0 >> 8) & 31) + 1;
    unsigned columns = ((phonon_hyperram_id0 >> 4) & 15) + 1;
    if (rows + columns + 1 != 26) return -1; // This build requires 64 MiB.
    for (unsigned die=0;die<2;++die) {
        uint32_t count=0,id=0;
        volatile uint32_t words[4];
        // CR0 register address, die selected by CA[37] (ISSI Rev A1 p18).
        if (receive_hyperram_register(ospi,0xE0000100U|(die<<21),0,&id,&count,words)) return -1;
        phonon_hyperram_cr0[die]=id;
    }
    if ((phonon_hyperram_cr0[0]&0xffff)!=(phonon_hyperram_cr0[1]&0xffff) ||
        !(phonon_hyperram_cr0[0]&8)) return -1;
    unsigned latency=(phonon_hyperram_cr0[0]>>4)&15;
    if (latency<=3) phonon_hyperram_wait_cycles=latency+5;
    else if (latency>=14) phonon_hyperram_wait_cycles=latency-11;
    else return -1;
    // Command-mode reads leave TMOD=RX. Restore bidirectional transfer before
    // the pack enables the independently configured memory-mapped XIP path.
    ospi_set_tmod(ospi,SPI_TMOD_TX_AND_RX);
    phonon_psram_probe_stage = 4;
    return 0;
}

int phonon_psram_init_and_test(void)
{
    // Quiesce retained XIP before resetting a device after a core-only reset.
#if SOC_FEAT_OSPI_HAS_CLK_ENABLE
    enable_ospi_clk(OSPI_INSTANCE_0);
#endif
    aes_disable_xip((AES_Type *)AES0_BASE);
    ospi_disable((OSPI_Type *)OSPI0_BASE);
    // The HyperBus XIP initializer does not select master mode (the pack's
    // PSRAM initializer does). Do not depend on retained flash/debug state.
    ospi_mode_master((OSPI_Type *)OSPI0_BASE);
    const uint32_t data_pad = PADCTRL_READ_ENABLE |
        PADCTRL_OUTPUT_DRIVE_STRENGTH_12MA | PADCTRL_SLEW_RATE_FAST;
    for (unsigned pin = 0; pin < 8; ++pin)
        if (pinconf_set(PORT_2, pin, PINMUX_ALTERNATE_FUNCTION_1, data_pad)) return -500;
    for (unsigned pin = 0; pin < 3; ++pin)
        if (pinconf_set(PORT_3, pin, PINMUX_ALTERNATE_FUNCTION_1, data_pad)) return -500;
    if (pinconf_set(PORT_1, PIN_6, PINMUX_ALTERNATE_FUNCTION_1, data_pad) ||
        pinconf_set(PORT_15, PIN_6, PINMUX_ALTERNATE_FUNCTION_0, 0)) return -500;
    extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(15);
    ARM_DRIVER_GPIO *gpio = &ARM_Driver_GPIO_(15);
    if (gpio->Initialize(6, NULL) != ARM_DRIVER_OK ||
        gpio->PowerControl(6, ARM_POWER_FULL) != ARM_DRIVER_OK ||
        gpio->SetDirection(6, GPIO_PIN_DIRECTION_OUTPUT) != ARM_DRIVER_OK ||
        gpio->SetValue(6, GPIO_PIN_OUTPUT_STATE_LOW) != ARM_DRIVER_OK) return -500;
    sys_busy_loop_us(10);
    if (gpio->SetValue(6, GPIO_PIN_OUTPUT_STATE_HIGH) != ARM_DRIVER_OK) return -500;
    sys_busy_loop_us(1000);
    phonon_psram_probe_stage = 1;
    // Start with the observed board setting. The read-only identification
    // callback captures actual CR0 and derives the final initial latency.
    ospi_psram_xip_config config = {
        .instance = OSPI_INSTANCE_0,
        .bus_speed = PHONON_PSRAM_HZ,
        .ram_init = identify_hyperram,
        .ddr_drive_edge = 1,
        .rxds_delay = 8,
        .wait_cycles = 7,
        .slave_select = 0,
        .dfs = 32,
        .spi_frf = OSPI_SPI_FRF_OCTAL,
        .ram_type = RAM_TYPE_HYPERRAM,
        .config_mode = CONFIG_MODE_USER_PARAMETERS,
#if SOC_FEAT_AES_OSPI_SIGNALS_DELAY
        .signal_delay = 0,
#endif
    };
    if (ospi_psram_xip_init(&config)) return -501;
    // The controller wait field takes CR0's initial latency, even though
    // this dual-die RAM uses twice that latency on the wire. Verified by
    // mapped multiword write/readback; passing the doubled value shifts data.
    ospi_hyperbus_xip_init((OSPI_Type *)OSPI0_BASE,phonon_hyperram_wait_cycles,false);
    phonon_psram_baud = ((OSPI_Type *)OSPI0_BASE)->OSPI_BAUDR;
    phonon_psram_test_begin();
    volatile uint32_t *memory = (volatile uint32_t *)PHONON_PSRAM_BASE;
    // Complete physical readback, with an address-dependent pattern. Flush
    // CPU cache so successful cache hits cannot stand in for fitted RAM.
    for (uint32_t i = 0; i < PHONON_PSRAM_BYTES / 4; ++i)
        memory[i] = (i * 0x9E3779B9U) ^ 0xA53C965AU;
    SCB_CleanInvalidateDCache();
    for (uint32_t i = 0; i < PHONON_PSRAM_BYTES / 4; ++i) {
        if (memory[i] != ((i * 0x9E3779B9U) ^ 0xA53C965AU)) {
            phonon_psram_error_address = PHONON_PSRAM_BASE + 4 * i;
            return -502;
        }
    }
    phonon_psram_verified_bytes = PHONON_PSRAM_BYTES;
    return 0;
}
