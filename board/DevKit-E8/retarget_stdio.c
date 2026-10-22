/* Local modifications for this demo (2026-10-07): Game-only interrupt-driven UART4 support.
 * Copyright (c) 2026 Arm Limited or its affiliates. All rights reserved.
 * New Arm contributions were generated with AI assistance, under Apache-2.0.
 * Reused vendor code retains the original licenses/notices below;
 * this notice does not relicense it. See THIRD_PARTY_NOTICES.md.
 */

/*---------------------------------------------------------------------------
 * Copyright (c) 2025 Arm Limited (or its affiliates).
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *      Name:    retarget_stdio.c
 *      Purpose: Retarget stdio to CMSIS UART
 *
 *---------------------------------------------------------------------------*/

#include "RTE_Components.h"
#include CMSIS_target_header
#include "retarget_stdout.h"

/* Compile-time configuration */
#ifndef UART_BAUDRATE
#define UART_BAUDRATE 115200
#endif

/* Exported function */
extern int stdio_init(void);

/* Arm C library diagnostic output uses the board's UART stdout transport.
 * Implement the library hook directly: the CMSIS TTY component is AC6-only.
 */
#if defined(__ARMCOMPILER_VERSION)
void _ttywrch(int ch)
{
    (void)stdout_putchar(ch);
}
#endif

/* Reference to the underlying USART driver */
#define ptrUSART (&ARM_Driver_USART_(RETARGET_STDIO_UART))

#if defined(BOARD_UART_ASYNC_RX)
/* Optional interrupt reception. The board owns its transport ring; application
 * nodes own line editing and protocol policy. Installed on the FIRST Initialize:
 * Ensemble's driver deliberately ignores later Initialize callbacks. */
#include "board_nodes.h"
#include CMSIS_device_header
static uint8_t uart_rx_byte, uart_rx_ring[512];
static volatile uint32_t uart_rx_head, uart_rx_tail;
volatile uint32_t board_uart_rx_drops, board_uart_rx_errors;
static void uart_rx_callback(uint32_t events)
{
    if (events & ARM_USART_EVENT_RECEIVE_COMPLETE) {
        uint32_t head = uart_rx_head;
        if (head - uart_rx_tail < sizeof(uart_rx_ring)) {
            uart_rx_ring[head & (sizeof(uart_rx_ring)-1)] = uart_rx_byte;
            __DMB(); uart_rx_head = head + 1;
        } else ++board_uart_rx_drops;
        if (ptrUSART->Receive(&uart_rx_byte,1) != ARM_DRIVER_OK) ++board_uart_rx_errors;
    }
    if (events & (ARM_USART_EVENT_RX_OVERFLOW | ARM_USART_EVENT_RX_FRAMING_ERROR |
                  ARM_USART_EVENT_RX_PARITY_ERROR | ARM_USART_EVENT_RX_BREAK)) ++board_uart_rx_errors;
}
int board_uart_rx_start(void) { return ptrUSART->Receive(&uart_rx_byte,1); }
int board_uart_rx_read(void)
{
    uint32_t tail=uart_rx_tail;
    if (tail==uart_rx_head) return -1;
    __DMB(); int value=uart_rx_ring[tail & (sizeof(uart_rx_ring)-1)];
    __DMB(); uart_rx_tail=tail+1; return value;
}
/* The vendor polling adapter tests GetTxCount()==1, which can still describe
 * the PREVIOUS byte when interrupt Send returns. Wait for tx_busy to clear so
 * the stack-backed byte remains valid until this transfer really completes. */
int stdout_init(void) { return 0; }
int stderr_init(void) { return 0; }
int stdout_putchar(int ch)
{
    uint8_t byte=(uint8_t)ch;
    while(ptrUSART->GetStatus().tx_busy) {}
    if(ptrUSART->Send(&byte,1)!=ARM_DRIVER_OK) return -1;
    while(ptrUSART->GetStatus().tx_busy) {}
    return ch;
}
int stderr_putchar(int ch) { return stdout_putchar(ch); }
#endif

/**
  Initialize stdio

  \return          0 on success, or -1 on error.
*/
int stdio_init(void)
{

    if (ptrUSART->Initialize(
#if defined(BOARD_UART_ASYNC_RX)
        uart_rx_callback
#else
        NULL
#endif
        ) != ARM_DRIVER_OK) {
        return -1;
    }

    if (ptrUSART->PowerControl(ARM_POWER_FULL) != ARM_DRIVER_OK) {
        return -1;
    }

    if (ptrUSART->Control(ARM_USART_MODE_ASYNCHRONOUS | ARM_USART_DATA_BITS_8 |
                              ARM_USART_PARITY_NONE | ARM_USART_STOP_BITS_1 |
                              ARM_USART_FLOW_CONTROL_NONE,
                          UART_BAUDRATE) != ARM_DRIVER_OK) {
        return -1;
    }

#if defined(RTE_CMSIS_Compiler_STDIN_Custom)
    if (ptrUSART->Control(ARM_USART_CONTROL_RX, 1U) != ARM_DRIVER_OK) {
        return -1;
    }
#endif

#if defined(RTE_CMSIS_Compiler_STDERR_Custom) || defined(RTE_CMSIS_Compiler_STDOUT_Custom)
    if (ptrUSART->Control(ARM_USART_CONTROL_TX, 1U) != ARM_DRIVER_OK) {
        return -1;
    }
#endif

    return 0;
}
