/* Local modifications for this demo (2026-10-07): Alif Ensemble 2.2.1 XIP driver; corrected legacy configuration framing.
 * Copyright (c) 2026 Arm Limited or its affiliates. All rights reserved.
 * New Arm contributions were generated with AI assistance, under Apache-2.0.
 * Reused vendor code retains the original licenses/notices below;
 * this notice does not relicense it. See THIRD_PARTY_NOTICES.md.
 */

/* Copyright (C) 2022 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification of this code is permitted under the
 * terms stated in the Alif Semiconductor Software License Agreement
 *
 * You should have received a copy of the Alif Semiconductor Software
 * License Agreement with this file. If not, please write to:
 * contact@alifsemi.com, or visit: https://alifsemi.com/license
 *
 */

/*******************************************************************************
 * @file     issi_flash.c
 * @version  V1.0.0
 * @brief    Flash driver to set up flash in XIP mode
 * @bug      None.
 * @Note     None
 ******************************************************************************/
/* Local adapter based on Ensemble 2.2.1 issi_flash.c. Vendor board and
 * ospi_drv.c remain intact; only legacy configuration framing is corrected.
 */
#include "RTE_Components.h"
#include CMSIS_device_header
#include "issi_flash_private.h"
#include "ospi_drv.h"
#include "ospi_xip_user.h"

static ospi_flash_cfg_t ospi_flash_config;

/**
  \fn         static void issi_write_enable(ospi_flash_cfg_t *ospi_cfg)
  \brief      Write enable for ISSI Flash before any write / program / erase
  \param[in]  ospi_cfg : OSPI configuration structure
  \return     none
 */
static void issi_write_enable(ospi_flash_cfg_t *ospi_cfg)
{
    /* Write WEL bit in OctalSPI mode */
    ospi_setup_write(ospi_cfg, ADDR_LENGTH_0_BITS);
    ospi_send_blocking(ospi_cfg, ISSI_WRITE_ENABLE);
}

/**
  \fn         static uint8_t issi_decode_id(ospi_flash_cfg_t *ospi_cfg, uint8_t *buffer)
  \brief      Decode the Device ID sent by the flash in single mode but read in octal mode.
  \param[in]  ospi_cfg : OSPI configuration structure
  \param[in]  buffer : ID read in SDR mode
  \return     ID of NOR flash
 */
static uint8_t issi_decode_id(ospi_flash_cfg_t *ospi_cfg, uint8_t *buffer)
{
    uint8_t iter, id = 0;

    for (iter = 0; iter < 8; iter++) {
        if (*buffer & 0x2) {
            id |= 1;
        }
        if (iter < 7) {
            id <<= 1;
        }
        buffer++;
    }

    ospi_cfg->device_id = id;

    return id;
}

/**
  \fn        uint8_t ospi_flash_ReadID(ospi_flash_cfg_t *ospi_cfg)
  \brief     This function reads the Device ID , if the ISSI NOR Flash boots up in SDR mode
  \param[in] ospi_cfg : OSPI configuration structure
  \return    Device ID of NOR Flash
 */
static uint8_t issi_flash_ReadID(ospi_flash_cfg_t *ospi_cfg)
{
    uint8_t buffer[8];

    ospi_setup_read(ospi_cfg, ADDR_LENGTH_0_BITS, 8, 0);
    ospi_recv_blocking(ospi_cfg, ISSI_READ_ID, buffer);

    return issi_decode_id(ospi_cfg, buffer);
}

/**
  \fn      static int issi_flash_set_configuration_register_SDR(ospi_flash_cfg_t *ospi_cfg, uint8_t
  cmd, uint8_t address, uint8_t value) \brief   This function sets the configuration register of the
  ISSI NOR Flash in SDR mode \param[in] ospi_cfg : OSPI configuration structure \param[in] cmd :
  Command \param[in] address : Address of register \param[in] value : Value to be set \return none
 */
static int issi_flash_set_configuration_register_SDR(ospi_flash_cfg_t *ospi_cfg, uint8_t cmd,
                                                      uint8_t address, uint8_t value)
{
    issi_write_enable(ospi_cfg);
    /* E8 legacy 0x81 configuration is plain 1-1-1 SPI. Enhanced
     * address packing is only used for octal DDR transactions.
     */
    spi_disable(ospi_cfg);
    ospi_writel(ospi_cfg, ser, 0);
    ospi_writel(ospi_cfg, ctrlr0, CTRLR0_IS_MST |
                (TMOD_TO << CTRLR0_TMOD_OFFSET) | CTRLR0_DFS_8bit);
    ospi_writel(ospi_cfg, ctrlr1, 0);
    ospi_writel(ospi_cfg, spi_ctrlr0, 0);
    spi_enable(ospi_cfg);
    const uint8_t bytes[] = {cmd, 0, 0, address, value};
    for (unsigned i = 0; i < sizeof(bytes); ++i) {
        ospi_push(ospi_cfg, bytes[i]);
        __DSB();
    }
    ospi_writel(ospi_cfg, ser, ospi_cfg->ser);
    __DSB();
    for (unsigned i = 0; i < 2000000U; ++i) {
        if ((ospi_readl(ospi_cfg, sr) & (SR_TF_EMPTY | SR_BUSY)) == SR_TF_EMPTY) {
            ospi_writel(ospi_cfg, ser, 0);
            return 0;
        }
    }
    ospi_writel(ospi_cfg, ser, 0);
    return -1;
}

/**
  \fn      static int issi_flash_probe (ospi_flash_cfg_t * ospi_cfg)
  \brief   Probe for an ISSI Flash device connected on the specified OSPI instance
  \param[in]  ospi_cfg : OSPI Flash Configuration
  \return     Success or Fail
 */
static int issi_flash_probe(ospi_flash_cfg_t *ospi_cfg)
{
    /* Initialize SPI in Single mode 1-1-1 and read Flash ID */
    if (issi_flash_ReadID(ospi_cfg) == DEVICE_ID_ISSI_FLASH_IS25WX256) {
        /* Set wrap configuration to 32 bytes */
        if (issi_flash_set_configuration_register_SDR(ospi_cfg,
                                                  ISSI_WRITE_VOLATILE_CONFIG_REG,
                                                  0x07,
                                                  WRAP_32_BYTE)) return -1;

        /* Set the wait cycles needed for read operations */
        if (issi_flash_set_configuration_register_SDR(ospi_cfg,
                                                  ISSI_WRITE_VOLATILE_CONFIG_REG,
                                                  0x01,
                                                  ospi_cfg->wait_cycles)) return -1;

        /* Switch the flash to Octal DDR mode */
        if (issi_flash_set_configuration_register_SDR(ospi_cfg,
                                                  ISSI_WRITE_VOLATILE_CONFIG_REG,
                                                  0x00,
                                                  OCTAL_DDR_DQS)) return -1;

        return 0;
    }

    return -1;
}

/**
  \fn         static int flash_xip_init(ospi_flash_cfg_t *ospi_cfg)
  \brief      This function initializes the flash in XIP mode
  \param[in]  ospi_cfg : OSPI configuration structure
  \return     Success or Fail
 */
static int flash_xip_init(ospi_flash_cfg_t *ospi_cfg)
{
    ospi_xip_enter(ospi_cfg, ISSI_DDR_OCTAL_IO_FAST_READ, ISSI_DDR_OCTAL_IO_FAST_READ);
    return 0;
}

/**
  \fn         bool e8_flash_xip_enabled(void)
  \brief      Return the status of xip initialization
  \param[in]  none
  \return     true or false
 */
bool e8_flash_xip_enabled(void)
{
    ospi_flash_cfg_t *ospi_cfg = &ospi_flash_config;
#if (OSPI_XIP_INSTANCE == 0)
    ospi_cfg->aes_regs = (aes_regs_t *) AES0_BASE;
#else
    ospi_cfg->aes_regs = (aes_regs_t *) AES1_BASE;
#endif
    return ospi_xip_enabled(ospi_cfg);
}

/**
  \fn         int e8_setup_flash_xip(void)
  \brief      This function initializes the NOR Flash and OSPI and enters the XIP mode
  \param[in]  none
  \return     Success or Fail
 */
int e8_setup_flash_xip(void)
{
    ospi_flash_cfg_t *ospi_cfg = &ospi_flash_config;
    OSPI_INSTANCE     drv_instance;

#if (OSPI_XIP_INSTANCE == 0)
    ospi_cfg->regs     = (ssi_regs_t *) OSPI0_BASE;
    ospi_cfg->aes_regs = (aes_regs_t *) AES0_BASE;
    ospi_cfg->xip_base = (volatile void *) OSPI0_XIP_BASE;
    drv_instance       = OSPI_INSTANCE_0;
#else
    ospi_cfg->regs     = (ssi_regs_t *) OSPI1_BASE;
    ospi_cfg->aes_regs = (aes_regs_t *) AES1_BASE;
    ospi_cfg->xip_base = (volatile void *) OSPI1_XIP_BASE;
    drv_instance       = OSPI_INSTANCE_1;
#endif

    ospi_cfg->ser         = 1;
    ospi_cfg->addrlen     = ADDR_LENGTH_32_BITS;
    ospi_cfg->ospi_clock  = OSPI_CLOCK;
    ospi_cfg->ddr_en      = 0;
    ospi_cfg->wait_cycles = OSPI_XIP_FLASH_WAIT_CYCLES;

    ospi_init(ospi_cfg, drv_instance);
    /* Both E8 RXDS delay fields match the verified 25 MHz loader path. */
    ospi_cfg->aes_regs->aes_rxds_delay = 0x0b0bU;
    __DSB();

    if (issi_flash_probe(ospi_cfg)) {
        return -1;
    }

    ospi_cfg->ddr_en = 1;

    if (flash_xip_init(ospi_cfg)) {
        return -1;
    }

    return 0;
}
