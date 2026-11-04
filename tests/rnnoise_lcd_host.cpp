// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <vector>
#include <cstdio>
extern "C" void rnnoise_render(uint16_t*,float,float,float,float,float,uint32_t,uint32_t);
int main(int argc,char** argv) {
    if(argc!=2)return 1;
    std::vector<uint16_t> memory(800*480+2,0x1234);
    rnnoise_render(memory.data()+1,.87f,-34.2f,12.4f,.47f,7.81f,56789,0);
    if(memory.front()!=0x1234||memory.back()!=0x1234)return 2;
    FILE* out=fopen(argv[1],"wb");if(!out)return 3;
    fprintf(out,"P6\n800 480\n255\n");
    for(unsigned y=0;y<480;++y)for(unsigned x=0;x<800;++x){
        uint16_t c=memory[1+x*480+479-y];
        uint8_t rgb[]{uint8_t(((c>>11)&31)*255/31),uint8_t(((c>>5)&63)*255/63),uint8_t((c&31)*255/31)};
        fwrite(rgb,1,3,out);
    }
    fclose(out);
    rnnoise_render(memory.data()+1,1.f,-120.f,120.f,10737.f,10737.f,0xffffffff,0xffffffff);
    return memory.front()!=0x1234||memory.back()!=0x1234?4:0;
}
