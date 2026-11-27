// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "rl_usb.h"
#include "USBD_Config_CustomClass_0.h"
#include "sds.h"
#include "sds_config.h"
#include "sdsio_client.h"
#include "sdsio_client_usb_mdk_config.h"

static uint8_t tx_buffer[SDS_BUF_SIZE]
    __attribute__((aligned(32), section(".bss.encodec_usb_tx")));

extern int32_t $Super$$sdsioClientSend(const uint8_t *buf, uint32_t size);

// SDS serializes sends across its application and worker threads. Its USB
// transport passes the caller's buffer directly to DMA, including stack
// headers. Keep one DMA-visible staging buffer for the blocking send.
int32_t $Sub$$sdsioClientSend(const uint8_t *buf, uint32_t size)
{
    static bool transfer_may_be_active;

    if (size == 0U) {
        return $Super$$sdsioClientSend(buf, size);
    }
    if (buf == NULL || size > sizeof(tx_buffer)) {
        return SDS_ERROR_PARAMETER;
    }
    if (transfer_may_be_active &&
        USBD_EndpointAbort(SDSIO_USB_DEVICE_INDEX,
                           USBD_CUSTOM_CLASS0_IF0_EP1_BENDPOINTADDRESS) != usbOK) {
        return SDS_ERROR_IO;
    }

    memcpy(tx_buffer, buf, size);
    const int32_t result = $Super$$sdsioClientSend(tx_buffer, size);
    // A timed-out send can leave the DMA owning this buffer.
    transfer_may_be_active = result != (int32_t)size;
    return result;
}
