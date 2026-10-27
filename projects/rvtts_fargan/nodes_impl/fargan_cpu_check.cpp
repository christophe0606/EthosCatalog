// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "fargan_cpu_check.hpp"
#include "fargan_cpu.h"
#include "../model/cpu_test_data.h"
#include <cmath>
#include <cstdio>
#include <cstring>
extern "C" {
volatile uint32_t fargan_cpu_selftest_pass;
volatile float fargan_cpu_source_max_error,fargan_cpu_source_rmse,fargan_cpu_source_snr_db,fargan_cpu_host_max_error;
}
int rvtts_fargan_cpu_selftest(){
    RvttsFarganCpuState state{};
    rvtts_fargan_cpu_prime(&state,rvtts_cpu_fixture_features);
    double error=0,power=0;float maximum=0,host_maximum=0;
    for(unsigned t=0;t<RVTTS_CPU_FIXTURE_FRAMES;++t){
        float pcm[160];rvtts_fargan_cpu_frame(&state,rvtts_cpu_fixture_features+t*20,pcm);
        for(unsigned i=0;i<160;++i){
            uint32_t bits;std::memcpy(&bits,pcm+i,4);if((bits&0x7f800000u)==0x7f800000u)return -472;
            const float source=rvtts_cpu_fixture_source_pcm[t*160+i],d=pcm[i]-source;
            const float host=std::fabs(pcm[i]-rvtts_cpu_fixture_quantized_pcm[t*160+i]);
            error+=double(d)*d;power+=double(source)*source;
            if(std::fabs(d)>maximum)maximum=std::fabs(d);if(host>host_maximum)host_maximum=host;
        }
    }
    const double rmse=std::sqrt(error/(RVTTS_CPU_FIXTURE_FRAMES*160));
    const double snr=10*std::log10(power/(error>1e-30?error:1e-30));
    fargan_cpu_source_max_error=maximum;fargan_cpu_source_rmse=float(rmse);fargan_cpu_source_snr_db=float(snr);fargan_cpu_host_max_error=host_maximum;
    printf("FARGAN_CPU selftest frames=%u mve=%lu rows=%lu packed=%lu dtcm=%lu source_max=%g source_rmse=%g source_snr_db=%g host_max=%g\r\n",
        RVTTS_CPU_FIXTURE_FRAMES,(unsigned long)fargan_cpu_kernel_mve,(unsigned long)fargan_cpu_kernel_rows,
        (unsigned long)fargan_cpu_kernel_packed,(unsigned long)fargan_cpu_dtcm_bytes,double(maximum),rmse,snr,double(host_maximum));
    // Declared host/board gates preserve the float source reference separately.
    if(maximum>.01f||rmse>.001||snr<30||host_maximum>.001f)return -473;
    fargan_cpu_selftest_pass=1;return 0;
}
