# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Exercise the real board queue with a deterministic CMSIS/codec mock."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MOCK = r'''
#pragma once
#include <stdint.h>
#define RTE_I2S3_DMA_ENABLE 0
#define RTE_I2S3_BLOCKING_MODE_ENABLE 0
#define CMSIS_device_header "audio_mock.h"
#define ARM_DRIVER_OK 0
#define ARM_POWER_OFF 0
#define ARM_POWER_FULL 1
#define ARM_SAI_EVENT_SEND_COMPLETE 1
#define ARM_SAI_EVENT_TX_UNDERFLOW 4
#define ARM_SAI_EVENT_FRAME_ERROR 16
#define ARM_SAI_CONTROL_TX 1
#define ARM_SAI_ABORT_SEND 2
#define ARM_SAI_CONFIGURE_TX 8
#define ARM_SAI_MODE_MASTER 16
#define ARM_SAI_ASYNCHRONOUS 32
#define ARM_SAI_PROTOCOL_I2S 64
#define ARM_SAI_DATA_SIZE(x) ((x)<<16)
#define osOK 0
#define SERVICES_REQ_SUCCESS 0
#define CLKEN_HFOSCx2 0
#define PADCTRL_READ_ENABLE 1
#define PADCTRL_DRIVER_DISABLED_PULL_UP 2
#define PORT_(x) (x)
#define BOARD_DAC_OUTPUT_SDO_GPIO_PORT 0
#define BOARD_DAC_OUTPUT_SDO_GPIO_PIN 0
#define BOARD_DAC_OUTPUT_SDO_ALTERNATE_FUNCTION 0
#define BOARD_DAC_OUTPUT_WS_GPIO_PORT 0
#define BOARD_DAC_OUTPUT_WS_GPIO_PIN 0
#define BOARD_DAC_OUTPUT_WS_ALTERNATE_FUNCTION 0
#define BOARD_DAC_OUTPUT_SCLK_GPIO_PORT 0
#define BOARD_DAC_OUTPUT_SCLK_GPIO_PIN 0
#define BOARD_DAC_OUTPUT_SCLK_ALTERNATE_FUNCTION 0
#define BOARD_WM8904_CODEC_I2C_SDA_GPIO_PORT 0
#define BOARD_WM8904_CODEC_I2C_SDA_GPIO_PIN 0
#define BOARD_WM8904_CODEC_I2C_SDA_ALTERNATE_FUNCTION 0
#define BOARD_WM8904_CODEC_I2C_SCL_GPIO_PORT 0
#define BOARD_WM8904_CODEC_I2C_SCL_GPIO_PIN 0
#define BOARD_WM8904_CODEC_I2C_SCL_ALTERNATE_FUNCTION 0
using osSemaphoreId_t=void*;
struct ARM_DRIVER_SAI {
 int32_t (*Initialize)(void(*)(uint32_t)); int32_t (*Uninitialize)();
 int32_t (*PowerControl)(int); int32_t (*Control)(uint32_t,uint32_t,uint32_t);
 int32_t (*Send)(const void*,uint32_t);
};
struct ARM_DRIVER_WM8904 {
 int32_t (*Initialize)(); int32_t (*Uninitialize)(); int32_t (*PowerControl)(int);
 int32_t (*Mute)(); int32_t (*UnMute)(); int32_t (*SetVolume)(uint8_t);
};
struct WM8904_CODEC_SLAVE_I2C_CONFIG { int unused; };
extern "C" unsigned se_services_s_handle;
osSemaphoreId_t osSemaphoreNew(uint32_t,uint32_t,const void*);
int osSemaphoreDelete(osSemaphoreId_t);
int osSemaphoreAcquire(osSemaphoreId_t,uint32_t);
int osSemaphoreRelease(osSemaphoreId_t);
uint32_t osKernelGetTickFreq(); uint32_t osKernelGetSysTimerFreq();
uint32_t osKernelGetTickCount(); uint32_t osKernelGetSysTimerCount(); int osDelay(uint32_t);
int SERVICES_clocks_enable_clock(unsigned,unsigned,bool,uint32_t*);
int pinconf_set(unsigned,unsigned,unsigned,unsigned);
int WM8904_codec_i2c_write(WM8904_CODEC_SLAVE_I2C_CONFIG*,uint16_t,uint16_t);
int WM8904_codec_i2c_read(WM8904_CODEC_SLAVE_I2C_CONFIG*,uint16_t,uint16_t*);
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default='clang++')
    parser.add_argument('--vcvars', type=Path)
    args = parser.parse_args()
    output = ROOT/'out/midi-ddsp-tests'
    output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix='audio-', dir=output))
    (run/'audio_mock.h').write_text(MOCK)
    for name in ['cmsis_os2.h', 'Driver_SAI.h', 'WM8904_driver.h', 'WM8904_codec_i2c.h',
                 'board_defs.h', 'pinconf.h', 'se_services_port.h', 'RTE_Components.h', 'RTE_Device.h']:
        (run/name).write_text('#include "audio_mock.h"\n')
    board = ROOT/'board/DevKit-E8'
    sources = [ROOT/'tests/test_midi_ddsp_audio_queue.cpp', board/'audio_output.cpp']
    binary = run/'audio_tests.exe'
    if args.vcvars:
        batch = run/'compile.cmd'
        batch.write_text('@echo off\ncall "'+str(args.vcvars.resolve())+'" >nul\n'
                         'cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /I"'+str(run)+'" /I"'+str(board)+'" '
                         +' '.join('"'+str(p)+'"' for p in sources)+' /Fe:"'+str(binary)+'"\n')
        subprocess.run(['cmd', '/d', '/c', str(batch)], cwd=run, check=True)
    else:
        subprocess.run([args.cxx, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I'+str(run), '-I'+str(board), *map(str,sources), '-o', str(binary)], cwd=run, check=True)
    subprocess.run([str(binary)], check=True)

if __name__ == '__main__':
    main()
