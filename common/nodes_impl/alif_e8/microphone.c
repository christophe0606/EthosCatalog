/* Adapted from the Spectrogram example.
 * Copyright (C) 2023 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification permitted under the Alif Semiconductor
 * Software License Agreement: https://alifsemi.com/license
 */
#include "board_nodes.h"
#include "RTE_Components.h"
#include CMSIS_device_header
#include "Driver_PDM.h"
#include "pinconf.h"
#include "board_config.h"
#include "cmsis_os2.h"
#include <string.h>
#define AUDIO_CHUNK MICROPHONE_BLOCK_SAMPLES
#define AUDIO_CHANNELS 1U
#define AUDIO_SLOTS 8U
#define MICROPHONE_GAIN_Q8_4 0xF00U
#define CHECK(call) do { int32_t status = (call); if (status != ARM_DRIVER_OK) return status; } while (0)
extern ARM_DRIVER_PDM Driver_PDM;
static int16_t audio[AUDIO_SLOTS][AUDIO_CHUNK];
static uint32_t sequence[AUDIO_SLOTS], capture_sequence, last_sequence;
static volatile uint32_t audio_produced, audio_consumed;
static volatile bool audio_armed, audio_restart;
static volatile int32_t audio_error;
static osThreadId_t owner;
volatile uint32_t microphone_drops, microphone_overflows;
static void audio_callback(uint32_t events)
{
    if (!audio_armed) return;
    if (events & ARM_PDM_EVENT_ERROR) {
        ++microphone_overflows;
        audio_restart = true;
        audio_armed = false;
    } else if (events & ARM_PDM_EVENT_CAPTURE_COMPLETE) {
        ++capture_sequence;
        // Keep one slot for the driver. Never overwrite an unread/leased block.
        if (audio_produced - audio_consumed < AUDIO_SLOTS - 1U) {
            sequence[audio_produced % AUDIO_SLOTS] = capture_sequence;
            __DMB();
            ++audio_produced;
        } else ++microphone_drops;
        audio_error = Driver_PDM.Receive(audio[audio_produced % AUDIO_SLOTS], AUDIO_CHUNK);
    }
    osThreadFlagsSet(owner, 1U);
}
int microphone_start(void)
{
    CHECK(pinconf_set(PORT_(BOARD_PDM_D2_B_GPIO_PORT), BOARD_PDM_D2_B_GPIO_PIN,
                             PINMUX_ALTERNATE_FUNCTION_3,
                             PADCTRL_READ_ENABLE | PADCTRL_DRIVER_DISABLED_PULL_UP |
                             PADCTRL_OUTPUT_DRIVE_STRENGTH_4MA));
    CHECK(pinconf_set(PORT_(BOARD_PDM_C2_A_GPIO_PORT), BOARD_PDM_C2_A_GPIO_PIN,
                             PINMUX_ALTERNATE_FUNCTION_3, PADCTRL_OUTPUT_DRIVE_STRENGTH_4MA));
    owner = osThreadGetId();
    CHECK(Driver_PDM.Initialize(audio_callback));
    CHECK(Driver_PDM.PowerControl(ARM_POWER_FULL));
    CHECK(Driver_PDM.Control(ARM_PDM_MODE, ARM_PDM_MODE_MICROPHONE_SLEEP, 0));
    /* Filter from the pack's bare-metal PDM example.
     * Keep the hardware FIR decimator and DC-blocking IIR enabled. */
    PDM_CH_CONFIG config = { .ch_num = 4, .ch_fir_coef = {
        0x001,0x003,0x003,0x7F4,0x004,0x7ED,0x7F5,0x7F4,0x7D3,
        0x7FE,0x7BC,0x7E5,0x7D9,0x793,0x029,0x72C,0x072,0x2FD
    }, .ch_iir_coef = 9 };
    CHECK(Driver_PDM.Config(&config));
    CHECK(Driver_PDM.Control(ARM_PDM_CHANNEL_PHASE, 4, 0x1F));
    CHECK(Driver_PDM.Control(ARM_PDM_CHANNEL_GAIN, 4, MICROPHONE_GAIN_Q8_4));
    CHECK(Driver_PDM.Control(ARM_PDM_BYPASS_FIR_FILTER, 0, 0));
    CHECK(Driver_PDM.Control(ARM_PDM_BYPASS_IIR_FILTER, 0, 0));
    CHECK(Driver_PDM.Control(ARM_PDM_SELECT_CHANNEL,
                 ARM_PDM_MASK_CHANNEL_4, 0));
    CHECK(Driver_PDM.Control(ARM_PDM_MODE, ARM_PDM_MODE_AUDIOFREQ_48K_DECM_64, 0));
    CHECK(Driver_PDM.Receive(audio[0], AUDIO_CHUNK * AUDIO_CHANNELS));
    audio_armed = true;
    return 0;
}


int microphone_read(int16_t *block, bool *discontinuity)
{
    *discontinuity = false;
    for (;;) {
        if (audio_restart) {
            audio_armed = false;
            CHECK(Driver_PDM.PowerControl(ARM_POWER_OFF));
            audio_produced = audio_consumed = 0;
            capture_sequence = last_sequence = 0;
            audio_restart = false;
            audio_error = 0;
            CHECK(microphone_start());
            *discontinuity = true;
        }
        if (audio_error) return audio_error;
        uint32_t available = audio_produced;
        __DMB();
        if (available != audio_consumed) {
            uint32_t slot = audio_consumed % AUDIO_SLOTS;
            memcpy(block, audio[slot], AUDIO_CHUNK * sizeof(int16_t));
            if (sequence[slot] != last_sequence + 1U) *discontinuity = true;
            last_sequence = sequence[slot];
            __DMB();
            ++audio_consumed;
            return 0;
        }
        uint32_t flags = osThreadFlagsWait(1U, osFlagsWaitAny, osKernelGetTickFreq() / 4U);
        if (flags & osFlagsError) return ARM_DRIVER_ERROR_TIMEOUT;
    }
}
