/* Local modifications for this demo (2026-10-07): Alif Ensemble 2.2.1 polling driver; E8 FlashOS adapter and bounded transfers.
 * Copyright (c) 2026 Arm Limited or its affiliates. All rights reserved.
 * New Arm contributions were generated with AI assistance, under Apache-2.0.
 * Reused vendor code retains the original licenses/notices below;
 * this notice does not relicense it. See THIRD_PARTY_NOTICES.md.
 */

/* E8 polling FlashOS adapter, adapted from Ensemble 2.2.1 ospi_drv.c,
 * issi_flash.c and IS25WX256.c. Register definitions are in ospi_private.h.
 * Copyright (C) 2022 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification permitted under the Alif Semiconductor
 * Software License Agreement: https://alifsemi.com/license
 *
 * The calling project must establish its clocks and programming handoff.
 * This loader performs no enclave calls,
 * uses no interrupts/RTOS, and has bounded polling. It implements E8 single-
 * FIFO address packing and the current vendor SDR probe before DDR setup.
 */
#include <stdint.h>
#include "FlashOS.h"
#include "ospi_private.h"

#define BASE 0xC0000000UL
#define LIMIT 0x04000000UL
#define ERASE_SECTOR_BYTES 131072UL
#define PROGRAM_BLOCK_BYTES 4096UL
#define PHYSICAL_PAGE_BYTES 256UL
#define SPI ((ssi_regs_t *)0x83002000UL)
#define AES ((aes_regs_t *)0x83003000UL)
#define REG(a) (*(volatile uint32_t *)(a))
#define POLL_LIMIT 2000000UL

/* Persist after a returned failure for debugger inspection; no console I/O. */
volatile uint32_t loader_stage, loader_error, loader_id, loader_status;
volatile uint32_t loader_jedec_id, loader_extended_id;
volatile uint32_t loader_erase_mode;
volatile uint32_t loader_protocol_mode;
/* Two uninterrupted CPU snapshots: end of Init and entry to EraseSector.
 * Each stores protocol metadata, MPU_CTRL, R9 and controller descriptor.
 */
volatile uint32_t loader_trace[8];
static uint32_t static_base_register(void)
{
    uint32_t value;
    __asm volatile("mov %0, r9" : "=r"(value));
    return value;
}
static void barrier(void)
{
    __asm volatile("dsb sy" ::: "memory");
}

static void push(uint32_t value)
{
    SPI->data_reg = value;
    /* With MPU disabled the peripheral's 0x8300... address is in the core's
     * default Normal-memory range. Complete every FIFO store individually.
     */
    barrier();
}

static void select_flash(void)
{
    SPI->ser = 1;
    barrier();
}
#ifdef MODEL_LOADER_DIAGNOSTIC
volatile uint32_t loader_config_address_bytes = 3;
volatile uint8_t loader_probe_data[16];
volatile uint32_t loader_probe_result;
#endif

static void device_memory(void)
{
    /* Armv8-M MPU: replace retained application regions with explicit
     * Device-nGnRnE MMIO and a privileged default map for code/RAM/stack.
     * Cache and stack-limit quiescence is supplied by the calling project.
     */
    barrier();
    REG(0xE000ED94UL) = 0;
    uint32_t regions = (REG(0xE000ED90UL) >> 8) & 0xff;
    for (uint32_t i = 0; i < regions; ++i) {
        REG(0xE000ED98UL) = i;
        REG(0xE000EDA0UL) = 0;
    }
    REG(0xE000EDC0UL) = 0; /* Attr0 = Device-nGnRnE. */
    REG(0xE000ED98UL) = 0;
    REG(0xE000ED9CUL) = 0x83000001UL;
    REG(0xE000EDA0UL) = 0x83003FE1UL;
    REG(0xE000ED98UL) = 1;
    REG(0xE000ED9CUL) = 0x1A603001UL;
    REG(0xE000EDA0UL) = 0x1A603FE1UL;
    REG(0xE000ED94UL) = 5; /* ENABLE + PRIVDEFENA, HFNMIENA clear. */
    barrier();
    __asm volatile("isb sy" ::: "memory");
}

static int error(uint32_t code)
{
    loader_error = code;
    SPI->ser = 0;
    SPI->ssienr = 0;
    return 1;
}

static void configure(uint32_t receive, uint32_t address_bits,
                      uint32_t length, uint32_t wait, uint32_t single, uint32_t ddr)
{
    AES->aes_control = 0;
    SPI->ser = 0;
    SPI->ssienr = 0;
    SPI->ctrlr0 = CTRLR0_IS_MST | (single ? 0 : (OCTAL << CTRLR0_SPI_FRF_OFFSET)) |
        ((receive ? TMOD_RO : TMOD_TO) << CTRLR0_TMOD_OFFSET) |
        (ddr ? CTRLR0_DFS_16bit : CTRLR0_DFS_8bit);
    SPI->ctrlr1 = receive ? length - 1U : 0U;
    SPI->spi_ctrlr0 = ((ddr || single) ? TRANS_TYPE_FRF_DEFINED : TRANS_TYPE_STANDARD) |
        (ddr << CTRLR0_SPI_DDR_EN_OFFSET) | (CTRLR0_INST_L_8bit << CTRLR0_INST_L_OFFSET) |
        (address_bits << CTRLR0_ADDR_L_OFFSET) | (wait << CTRLR0_WAIT_CYCLES_OFFSET);
    /* Match IS25WX256.c ReadStatusReg and ospi_transfer_polling: register
     * transfers in octal DDR use 16-bit frames sampled with flash RXDS.
     * The vendor notes that 8-bit DDR receive still reads 16-bit DR values.
     */
    if (ddr && receive) SPI->spi_ctrlr0 |= 1U << CTRLR0_SPI_RXDS_EN_OFFSET;
    SPI->ssienr = 1;
    barrier();
}

static int finish_tx(void)
{
    for (uint32_t i = 0; i < POLL_LIMIT; ++i) {
        if ((SPI->sr & (SR_TF_EMPTY | SR_BUSY)) == SR_TF_EMPTY) {
            SPI->ser = 0;
            return 0;
        }
    }
    return error(10);
}

static int command(uint32_t opcode, uint32_t ddr)
{
    configure(0, 0, 0, 0, 0, ddr);
    push(opcode);
    select_flash();
    return finish_tx();
}

static int receive(uint32_t opcode, uint32_t size, uint32_t wait, uint8_t *buffer, uint32_t ddr)
{
    configure(1, 0, size, wait, 0, ddr);
    push(opcode);
    select_flash();
    for (uint32_t count = 0; count < size; ++count) {
        uint32_t tries = 0;
        while (!SPI->rxflr) if (++tries == POLL_LIMIT) return error(11);
        buffer[count] = (uint8_t)SPI->data_reg;
    }
    SPI->ser = 0;
    return 0;
}

static int set_config(uint32_t address, uint32_t value)
{
    if (command(0x06, 0)) return 1;
    /* SINGLE uses a legacy SPI byte stream. Keep configuration addressing
     * distinct from the packed 32-bit octal erase/program addresses.
     */
    /* Plain 1-1-1 SPI: transmit each byte without the E8 enhanced controller's
     * packed-address phase. The complete address is supplied below.
     */
    configure(0, 0, 0, 0, 1, 0);
    SPI->spi_ctrlr0 = 0;
    push(0x81);
#ifdef MODEL_LOADER_DIAGNOSTIC
    if (loader_config_address_bytes == 4) push(0);
#endif
    push(0);
    push(0);
    push(address);
    push(value);
    select_flash();
    return finish_tx();
}

#ifdef MODEL_LOADER_DIAGNOSTIC
/* Read-only legacy SPI probe executed by the CPU, avoiding debugger memory
 * caching of FIFO accesses. Intended for halted-debugger function calls.
 */
int loader_sdr_read(uint32_t opcode, uint32_t address_bytes, uint32_t address,
                    uint32_t size, uint32_t dummy_bytes)
{
    if (address_bytes > 4 || !size || size > sizeof(loader_probe_data) || dummy_bytes > 16) return 1;
    AES->aes_control = 0;
    SPI->ser = 0;
    SPI->ssienr = 0;
    SPI->ctrlr0 = CTRLR0_IS_MST | (3U << CTRLR0_TMOD_OFFSET) | CTRLR0_DFS_8bit;
    SPI->ctrlr1 = size - 1U;
    SPI->spi_ctrlr0 = 0;
    SPI->ssienr = 1;
    SPI->data_reg = opcode;
    for (uint32_t i = address_bytes; i; --i) SPI->data_reg = (address >> ((i - 1) * 8)) & 0xff;
    for (uint32_t i = 0; i < dummy_bytes; ++i) SPI->data_reg = 0;
    SPI->ser = 1;
    for (uint32_t i = 0; i < size; ++i) {
        uint32_t tries = 0;
        while (!SPI->rxflr) if (++tries == POLL_LIMIT) return error(11);
        loader_probe_data[i] = (uint8_t)SPI->data_reg;
    }
    SPI->ser = 0;
    return 0;
}
#endif

static int ready(void)
{
    uint8_t status;
    for (uint32_t i = 0; i < 200000U; ++i) {
        if (receive(0x70, 1, 8, &status, 1)) return 1;
        loader_status = status;
        if (status & 0x30) return error(12);
        if (status & 0x80) return 0;
    }
    return error(13);
}

static void xip(void)
{
    SPI->ser = 0;
    SPI->ssienr = 0;
    SPI->ctrlr0 = CTRLR0_IS_MST | (OCTAL << CTRLR0_SPI_FRF_OFFSET) |
        (TMOD_RO << CTRLR0_TMOD_OFFSET) | CTRLR0_DFS_16bit;
    /* Same XIP read configuration as the working vendor E8 application. */
    SPI->xip_ctrl = (OCTAL << XIP_CTRL_FRF_OFFSET) | (2U << XIP_CTRL_TRANS_TYPE_OFFSET) |
        (8U << XIP_CTRL_ADDR_L_OFFSET) | (2U << XIP_CTRL_INST_L_OFFSET) |
        (16U << XIP_CTRL_WAIT_CYCLES_OFFSET) | (1U << XIP_CTRL_DFC_HC_OFFSET) |
        (1U << XIP_CTRL_DDR_EN_OFFSET) | (1U << XIP_CTRL_RXDS_EN_OFFSET) |
        (1U << XIP_CTRL_INST_EN_OFFSET);
    SPI->rx_sample_dly = 0;
    SPI->xip_mode_bits = 0;
    SPI->xip_incr_inst = 0xFD;
    SPI->xip_wrap_inst = 0xFD;
    SPI->ssienr = 1;
    AES->aes_control = AES_CONTROL_XIP_EN;
}

int Init(unsigned long address, unsigned long clock, unsigned long function)
{
    (void)clock;
    loader_stage = 1;
    loader_error = loader_id = loader_status = 0;
    loader_jedec_id = loader_extended_id = 0;
    loader_protocol_mode = 0;
    for (uint32_t i = 0; i < 8; ++i) loader_trace[i] = 0;
    if (address < BASE || address >= BASE + LIMIT) return error(1);
    /* The calling project supplied the safe stack/cache/MPU state before this call. */
    __asm volatile("cpsid i" ::: "memory");
    device_memory();
    REG(0x4902F03CUL) |= 2U;
    AES->aes_control = 0;
    SPI->ser = 0;
    SPI->ssienr = 0;
    /* E8 flash pins; do not touch the unrelated P5_6 pin. */
    for (uint32_t pin = 5; pin <= 7; ++pin) REG(0x1A603120UL + pin * 4) = 0x00650001;
    for (uint32_t pin = 0; pin <= 4; ++pin) REG(0x1A603140UL + pin * 4) = 0x00650001;
    REG(0x1A60315CUL) = 0x00650007;
    REG(0x1A6030B4UL) = 0x00640001;
    REG(0x1A6030BCUL) = 0x00600001;
    REG(0x1A603100UL) = 0x00600001;
    REG(0x42002004UL) |= 0x80U;
    REG(0x42002000UL) &= ~0x80U;
    for (volatile uint32_t i = 0; i < 4000; ++i) {}
    REG(0x42002000UL) |= 0x80U;
    for (volatile uint32_t i = 0; i < 100000; ++i) {}
    SPI->baudr = 16; /* Conservative 25 MHz with the board's 400 MHz AXI. */
    SPI->rx_sample_dly = 0;
    SPI->txd_drive_edge = 1;
    /* E8 has two 8-bit RXDS delay fields (sys_ctrl_aes.h). */
    AES->aes_rxds_delay = 0x0B0B;
    loader_stage = 2;
    uint8_t id[48], decoded[6] = {0};
    if (receive(0x9E, sizeof(id), 0, id, 0)) return 1;
    for (uint32_t byte = 0; byte < sizeof(decoded); ++byte) {
        for (uint32_t bit = 0; bit < 8; ++bit) {
            decoded[byte] = (uint8_t)((decoded[byte] << 1) | ((id[byte * 8 + bit] >> 1) & 1U));
        }
    }
    loader_id = decoded[0];
    loader_jedec_id = ((uint32_t)decoded[0] << 16) | ((uint32_t)decoded[1] << 8) | decoded[2];
    loader_extended_id = decoded[4];
    /* Expansion beyond the former 32 MiB partition is permitted only for
     * the verified 128 MiB IS25WX01G addressing/program/erase command set.
     */
    if (loader_jedec_id != 0x9D5B1BU || loader_extended_id != 1U) return error(2);
    loader_stage = 3;
    if (set_config(7, 0xFD) || set_config(1, 16) || set_config(0, 0xE7)) return 1;
    loader_protocol_mode = 1;
    loader_stage = 4;
    if (ready()) return 1;
    if (function == 3) xip();
    loader_stage = 5;
    loader_trace[0] = loader_protocol_mode;
    loader_trace[1] = REG(0xE000ED94UL);
    loader_trace[2] = static_base_register();
    loader_trace[3] = SPI->spi_ctrlr0;
    return 0;
}

/* The CMSIS default AC6 linker options request Reset_Handler. This is an
 * algorithm ELF without a vector table or application startup; keep the
 * default entry as an alias of the mandatory FlashOS Init entry point.
 */
#ifndef MODEL_LOADER_DIAGNOSTIC
int Reset_Handler(unsigned long, unsigned long, unsigned long)
    __attribute__((alias("Init")));
#endif

int UnInit(unsigned long function)
{
    (void)function;
    xip(); /* Expose programmed data for debugger reads and verification. */
    return 0;
}

int EraseSector(unsigned long address)
{
    if (address < BASE || address >= BASE + LIMIT ||
        (address & (ERASE_SECTOR_BYTES - 1U))) return error(3);
    loader_stage = 6;
    loader_erase_mode = loader_protocol_mode;
    loader_trace[4] = loader_protocol_mode;
    loader_trace[5] = REG(0xE000ED94UL);
    loader_trace[6] = static_base_register();
    loader_trace[7] = SPI->spi_ctrlr0;
    /* FlashOS entry points describe their own transfer phase. Do not rely on
     * an Init-time software flag surviving debugger operation transitions.
     */
    if (command(0x06, 1)) return 1;
    configure(0, 8, 0, 0, 0, 1);
    /* The exact extended-ID check above establishes uniform 128 KiB geometry.
     * Use the documented four-byte-address sector erase, within this dedicated
     * model partition, to avoid thousands of slow 4 KiB erase transactions.
     */
    push(0xDC);
    push(address - BASE);
    select_flash();
    if (finish_tx() || ready()) return 1;
    return 0;
}

/* Keep each device transaction within one physical 256-byte page. The FlashOS
 * host buffer is larger so multi-megabyte models need fewer debugger calls.
 */
static int program_physical_page(unsigned long address, unsigned long size,
                                 const unsigned char *buffer)
{
    if (command(0x06, 1)) return 1;
    configure(0, 8, 0, 0, 0, 1);
    push(0x84);
    push(address - BASE);
    /* A 256-byte page plus command/address occupies 130 of the vendor
     * controller's 256 FIFO words. Queue it before asserting chip select
     * so the transaction cannot end while the CPU is still supplying data.
     */
    for (uint32_t count = 0; count < size; count += 2) {
        uint32_t tries = 0;
        while (!(SPI->sr & SR_TF_NOT_FULL)) if (++tries == POLL_LIMIT) return error(14);
        push((uint32_t)buffer[count] | ((uint32_t)buffer[count + 1] << 8));
    }
    select_flash();
    if (finish_tx() || ready()) return 1;
    return 0;
}

int ProgramPage(unsigned long address, unsigned long size, unsigned char *buffer)
{
    if (address < BASE || address >= BASE + LIMIT || !size ||
        size > PROGRAM_BLOCK_BYTES || (address & 1U) || (size & 1U) ||
        size > BASE + LIMIT - address || buffer == 0) return error(4);
    loader_stage = 7;
    while (size != 0U) {
        unsigned long count = PHYSICAL_PAGE_BYTES - (address & (PHYSICAL_PAGE_BYTES - 1U));
        if (count > size) count = size;
        if (program_physical_page(address, count, buffer)) return 1;
        address += count;
        buffer += count;
        size -= count;
    }
    return 0;
}

unsigned long Verify(unsigned long address, unsigned long size, unsigned char *buffer)
{
    if (address < BASE || address >= BASE + LIMIT || size > BASE + LIMIT - address) return address;
    loader_stage = 8;
    xip();
    for (uint32_t i = 0; i < size; ++i)
        if (*(volatile const uint8_t *)(address + i) != buffer[i]) return address + i;
    return address + size;
}

int BlankCheck(unsigned long address, unsigned long size, unsigned char pattern)
{
    if (address < BASE || address >= BASE + LIMIT || size > BASE + LIMIT - address) return 1;
    xip();
    for (uint32_t i = 0; i < size; ++i)
        if (*(volatile const uint8_t *)(address + i) != pattern) return 1;
    return 0;
}
