// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// Hardware feasibility benchmark; UART tokenizer and audio playback follow later.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include "embedded_runtime.hpp"
#include "model_pte.h"
#include "model/benchmark_fixtures.h"
#include "model/first_token_reference.h"
#include "phonon_psram.h"
#include "cmsis_os2.h"
#include "main.h"
#include "se_services_port.h"
#include "RTE_Components.h"
#include CMSIS_device_header
using namespace executorch::runtime;
using namespace executorch::aten;
#ifndef PHONON_FLASH_HZ
#define PHONON_FLASH_HZ 25000000UL
#endif
#ifndef PHONON_DEDICATED_SRAM
#define PHONON_DEDICATED_SRAM 0
#endif
#if PHONON_DEDICATED_SRAM
alignas(32) static unsigned char phonon_fast_scratch_buffer[393216]
    __attribute__((section(".bss.ai_pool")));
#endif
extern "C" {
int e8_flash_set_read_clock(uint32_t hz);
#if PHONON_DEDICATED_SRAM
// Strong definitions override the backend's weak null/zero defaults. Base2
// uses a disjoint SRAM allocation; the slow temporary pool remains required.
unsigned char *ethosu_fast_scratch=phonon_fast_scratch_buffer;
size_t ethosu_fast_scratch_size=sizeof(phonon_fast_scratch_buffer);
#endif
volatile uint32_t phonon_stage, phonon_complete, phonon_case_index, phonon_frame_index;
volatile int32_t phonon_error;
volatile uint32_t phonon_model_crc, phonon_voice_crc, phonon_bos_crc;
volatile uint32_t phonon_cpu_hz, phonon_axi_hz, phonon_timer_hz, phonon_ospi1_baud;
volatile uint32_t phonon_prefix_ticks[2], phonon_prefill_ticks[2];
volatile uint32_t phonon_frame_ticks[2][64], phonon_frame_invocations[2][64];
volatile uint32_t phonon_frame_delegate_ticks[2][64], phonon_frame_cpu_ticks[2][64];
volatile uint32_t phonon_delegate_ticks,phonon_delegate_calls,phonon_delegate_start;
volatile uint32_t phonon_delegate_active,phonon_delegate_pairing_error;
volatile uint64_t phonon_npu_cycles[2][64];
volatile uint32_t phonon_frames[2], phonon_eos_frame[2];
volatile float phonon_latent_max_error, phonon_pcm_max_error, phonon_eos_max_error;
volatile float phonon_hidden_max_error;
volatile float phonon_first_embedding_error, phonon_first_hidden_error, phonon_first_cache_error;
volatile float phonon_first_cache_error_by_layer[6][2];
volatile uint32_t phonon_first_token_checked;
volatile uint32_t phonon_method_peak, phonon_temp_peak, phonon_external_bytes;
// These backend hooks include Vela parsing, CPU I/O copies, NPU wait, and
// output copies. With 61 calls, tick quantization contributes at most 61 ms.
void EthosUBackend_execute_begin(void)
{
    if (phonon_delegate_active) phonon_delegate_pairing_error=1;
    phonon_delegate_active=1;
    phonon_delegate_start=osKernelGetTickCount();
}
void EthosUBackend_execute_end(void)
{
    if (!phonon_delegate_active) phonon_delegate_pairing_error=1;
    phonon_delegate_ticks+=osKernelGetTickCount()-phonon_delegate_start;
    ++phonon_delegate_calls;
    phonon_delegate_active=0;
}
}
namespace {
alignas(32) uint8_t method_pool[APP_METHOD_POOL_SIZE] __attribute__((section(".bss.ai_pool")));
alignas(32) uint8_t temp_pool[APP_TEMP_POOL_SIZE] __attribute__((section(".bss.ai_pool")));
constexpr size_t kArenaBytes = 38801424;
constexpr uintptr_t kExternalBase = PHONON_PSRAM_BASE + ((kArenaBytes + 31) & ~31UL);
constexpr size_t kExternalCapacity = PHONON_PSRAM_BYTES - (kExternalBase - PHONON_PSRAM_BASE);
arm::embedded::EmbeddedModule *module;
size_t external_used;
bool finite_float(float value)
{
    uint32_t bits;
    std::memcpy(&bits,&value,sizeof(bits));
    return (bits&0x7f800000U)!=0x7f800000U;
}
bool finite_floats(const float *values,size_t count)
{
    for (size_t i=0;i<count;++i) if (!finite_float(values[i])) return false;
    return true;
}
uint32_t crc32(const void *data, size_t count)
{
    static constexpr uint32_t table[16] = {0,0x1db71064,0x3b6e20c8,0x26d930ac,
        0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,0xedb88320,0xf00f9344,
        0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c};
    uint32_t crc = ~0U;
    auto bytes = static_cast<const uint8_t *>(data);
    auto consume = [&](uint8_t byte) {
        crc ^= byte;
        crc = (crc >> 4) ^ table[crc & 15];
        crc = (crc >> 4) ^ table[crc & 15];
    };
    while (count && (reinterpret_cast<uintptr_t>(bytes) & 3)) {
        consume(*bytes++); --count;
    }
    while (count >= 4) {
        // One uncached OSPI transaction supplies four little-endian bytes.
        uint32_t word = *reinterpret_cast<const volatile uint32_t *>(bytes);
        for (unsigned shift=0;shift<32;shift+=8) consume(uint8_t(word >> shift));
        bytes+=4; count-=4;
    }
    while (count--) consume(*bytes++);
    return ~crc;
}
struct Input {
    std::array<TensorImpl::SizesType, 8> sizes{};
    std::array<TensorImpl::DimOrderType, 8> order{};
    std::unique_ptr<TensorImpl> impl;
    void *data = nullptr;
    size_t bytes = 0;
    int initialize(const TensorInfo &info) {
        if (info.sizes().size() > sizes.size()) return -513;
        bytes = info.nbytes(); external_used = (external_used + 31) & ~31UL;
        if (external_used + std::max<size_t>(bytes,32) > kExternalCapacity) return -514;
        data = reinterpret_cast<void *>(kExternalBase + external_used);
        external_used += std::max<size_t>(bytes,32);
        std::copy(info.sizes().begin(), info.sizes().end(), sizes.begin());
        std::copy(info.dim_order().begin(), info.dim_order().end(), order.begin());
        impl = std::make_unique<TensorImpl>(info.scalar_type(), info.sizes().size(),
            sizes.data(), data, order.data());
        return 0;
    }
};
struct NeuralMethod {
    const char *name;
    std::array<Input,20> inputs;
    std::vector<EValue> values;
    std::array<Span<uint8_t>,1> buffers{{
        {reinterpret_cast<uint8_t *>(PHONON_PSRAM_BASE),kArenaBytes}}};
    HierarchicalAllocator planned{Span<Span<uint8_t>>(buffers.data(),buffers.size())};
    explicit NeuralMethod(const char *method_name) : name(method_name) {}
    int initialize() {
        auto meta = module->method_meta(name);
        if (!meta.ok()) return -520 - static_cast<int>(meta.error());
        if (meta->num_inputs() > inputs.size() || meta->num_memory_planned_buffers() != 1) return -515;
        auto arena = meta->memory_planned_buffer_size(0);
        if (!arena.ok() || *arena > kArenaBytes) return -516;
        for (size_t i=0; i<meta->num_inputs(); ++i) {
            auto info = meta->input_tensor_meta(i);
            if (!info.ok()) return -517;
            int error = inputs[i].initialize(*info);
            if (error) return error;
            values.emplace_back(Tensor(inputs[i].impl.get()));
        }
        auto result = module->load_method(name,&planned);
        return result == Error::Ok ? 0 : -600 - static_cast<int>(result);
    }
    Result<std::vector<EValue>> execute() { return module->execute(name,values); }
    int copy_output(const std::vector<EValue> &output,size_t index,Input &input) {
        if (index>=output.size() || !output[index].isTensor()) return -518;
        auto tensor=output[index].toTensor();
        if (tensor.nbytes()!=input.bytes) return -519;
        if (input.bytes) std::memcpy(input.data,tensor.const_data_ptr(),input.bytes);
        return 0;
    }
};
NeuralMethod backbone("backbone"), synthesis("synthesize"), embedding("text_embedding");
int initialize()
{
    phonon_stage=2;
    // No NPU or audio DMA is active. The routine executes from internal memory.
    int error=e8_flash_set_read_clock(PHONON_FLASH_HZ);
    if (error) return -530+error;
    SCB_CleanInvalidateDCache();
    phonon_model_crc=crc32(model_pte,model_pte_size);
    phonon_voice_crc=crc32(reinterpret_cast<const void *>(MODEL_ASSET_VOICE_PREFIX_ADDRESS),MODEL_ASSET_VOICE_PREFIX_SIZE_BYTES);
    phonon_bos_crc=crc32(reinterpret_cast<const void *>(MODEL_ASSET_BOS_EMBEDDING_ADDRESS),MODEL_ASSET_BOS_EMBEDDING_SIZE_BYTES);
    printf("Phonon CRC: PTE=%08lx voice=%08lx BOS=%08lx\n",(unsigned long)phonon_model_crc,(unsigned long)phonon_voice_crc,(unsigned long)phonon_bos_crc);
    const bool crc_ok = phonon_model_crc==MODEL_PTE_CRC32 &&
        phonon_voice_crc==MODEL_ASSET_VOICE_PREFIX_CRC32 &&
        phonon_bos_crc==MODEL_ASSET_BOS_EMBEDDING_CRC32;
    phonon_stage=1;
    error=phonon_psram_init_and_test();
    if (error) return error;
    if (!crc_ok) return -503;
    phonon_stage=3;
    module=arm::embedded::initialize_runtime(model_pte,model_pte_size,method_pool,sizeof(method_pool),temp_pool,sizeof(temp_pool));
    if (!module) return -504;
    error=backbone.initialize();
    if (!error) error=synthesis.initialize();
    if (!error) error=embedding.initialize();
    if (error) return error;
    phonon_external_bytes=external_used;
    uint32_t service_error=0,clock=0;
    if (SERVICES_clocks_setting_get(se_services_s_handle,CLOCK_SETTING_EXTSYS0_FREQ,&clock,&service_error)!=SERVICES_REQ_SUCCESS || service_error || !clock) return -505;
    phonon_cpu_hz=clock;
    if (SERVICES_clocks_setting_get(se_services_s_handle,CLOCK_SETTING_AXI_FREQ,&clock,&service_error)!=SERVICES_REQ_SUCCESS || service_error || !clock) return -505;
    phonon_axi_hz=clock;
    phonon_timer_hz=osKernelGetTickFreq();
    phonon_ospi1_baud=((OSPI_Type *)OSPI1_BASE)->OSPI_BAUDR;
    printf("Phonon clocks: CPU=%lu AXI=%lu timer=%lu OSPI0baud=%lu OSPI1baud=%lu\n",(unsigned long)phonon_cpu_hz,(unsigned long)phonon_axi_hz,(unsigned long)phonon_timer_hz,(unsigned long)phonon_psram_baud,(unsigned long)phonon_ospi1_baud);
    return 0;
}
int prefill(const phonon_fixture::Case &fixture,unsigned case_index)
{
    uint32_t start=osKernelGetTickCount();
    if (backbone.inputs[1].bytes!=MODEL_ASSET_VOICE_PREFIX_SIZE_BYTES || backbone.inputs[0].bytes!=MODEL_ASSET_BOS_EMBEDDING_SIZE_BYTES) return -506;
    std::memcpy(backbone.inputs[1].data,reinterpret_cast<const void *>(MODEL_ASSET_VOICE_PREFIX_ADDRESS),backbone.inputs[1].bytes);
    for (size_t i=2;i<synthesis.values.size();++i) std::memset(synthesis.inputs[i].data,0,synthesis.inputs[i].bytes);
    *static_cast<float *>(backbone.inputs[2].data)=125;
    phonon_prefix_ticks[case_index]=osKernelGetTickCount()-start;
    start=osKernelGetTickCount();
    // Pinned host tokenizer IDs: tokenizer/UART latency is excluded here.
    for (unsigned i=0;i<fixture.token_count;++i) {
        if (125+i>=512) return -507;
        *static_cast<int32_t *>(embedding.inputs[0].data)=fixture.tokens[i];
        auto text=embedding.execute();
        if (!text.ok()) return -700-static_cast<int>(text.error());
        int error=embedding.copy_output(*text,0,backbone.inputs[0]);
        if (error) return error;
        auto hidden=backbone.execute();
        if (!hidden.ok()) return -800-static_cast<int>(hidden.error());
        if (!finite_floats((*hidden)[0].toTensor().const_data_ptr<float>(),512)) return -510;
        error=backbone.copy_output(*hidden,1,backbone.inputs[1]);
        if (error) return error;
        *static_cast<float *>(backbone.inputs[2].data)+=1;
    }
    std::memcpy(backbone.inputs[0].data,reinterpret_cast<const void *>(MODEL_ASSET_BOS_EMBEDDING_ADDRESS),backbone.inputs[0].bytes);
    phonon_prefill_ticks[case_index]=osKernelGetTickCount()-start;
    return 0;
}
int check_first_token()
{
    phonon_stage=5;
    std::memcpy(backbone.inputs[1].data,reinterpret_cast<const void *>(MODEL_ASSET_VOICE_PREFIX_ADDRESS),backbone.inputs[1].bytes);
    *static_cast<float *>(backbone.inputs[2].data)=phonon_first_token::position_0;
    *static_cast<int32_t *>(embedding.inputs[0].data)=phonon_first_token::token_0;
    auto text=embedding.execute();
    if (!text.ok()) return -700-static_cast<int>(text.error());
    const float *embedded=(*text)[0].toTensor().const_data_ptr<float>();
    if (!finite_floats(embedded,512)) return -510;
    for (unsigned i=0;i<512;++i)
        phonon_first_embedding_error=std::max(float(phonon_first_embedding_error),std::fabs(embedded[i]-phonon_first_token::embedding_0[i]));
    int error=embedding.copy_output(*text,0,backbone.inputs[0]);
    if (error) return error;
    auto output=backbone.execute();
    if (!output.ok()) return -800-static_cast<int>(output.error());
    const float *hidden=(*output)[0].toTensor().const_data_ptr<float>();
    const float *cache=(*output)[1].toTensor().const_data_ptr<float>();
    if (!finite_floats(hidden,512)) return -510;
    for (unsigned i=0;i<512;++i)
        phonon_first_hidden_error=std::max(float(phonon_first_hidden_error),std::fabs(hidden[i]-phonon_first_token::hidden_0[i]));
    for (unsigned block=0;block<6*2*8;++block) for (unsigned i=0;i<64;++i) {
        float value=cache[(block*512+511)*64+i];
        if (!finite_float(value)) return -510;
        float difference=std::fabs(value-phonon_first_token::cache_last_row_0[block*64+i]);
        phonon_first_cache_error=std::max(float(phonon_first_cache_error),difference);
        unsigned layer=block/(2*8),kv=(block/8)%2;
        phonon_first_cache_error_by_layer[layer][kv]=std::max(float(phonon_first_cache_error_by_layer[layer][kv]),difference);
    }
    phonon_first_token_checked=1;
    auto usage=arm::embedded::runtime_memory_usage();
    phonon_method_peak=usage.method_peak;
    phonon_temp_peak=usage.temp_peak;
    return 0;
}
int run_case(unsigned case_index)
{
    const auto &fixture=phonon_fixture::cases[case_index];
    phonon_case_index=case_index; phonon_stage=10;
    int error=prefill(fixture,case_index);
    if (error) return error;
    int eos_frame=-1;
    for (unsigned frame=0;frame<phonon_fixture::kFrames;++frame) {
        if (125+fixture.token_count+frame>=512) return -507;
        phonon_stage=20; phonon_frame_index=frame;
        ethos_benchmark_reset();
        phonon_delegate_ticks=0; phonon_delegate_calls=0;
        phonon_delegate_pairing_error=0;
        uint32_t start=osKernelGetTickCount();
        auto hidden=backbone.execute();
        if (!hidden.ok()) return -800-static_cast<int>(hidden.error());
        const float *hidden_values=(*hidden)[0].toTensor().const_data_ptr<float>();
        if (!finite_floats(hidden_values,512)) return -510;
        if (frame<2) {
            const float *reference=case_index?phonon_benchmark_backbone_output_1:phonon_benchmark_backbone_output_0;
            for (unsigned i=0;i<512;++i) phonon_hidden_max_error=std::max(float(phonon_hidden_max_error),std::fabs(hidden_values[i]-reference[frame*512+i]));
        }
        error=backbone.copy_output(*hidden,0,synthesis.inputs[0]);
        if (!error) error=backbone.copy_output(*hidden,1,backbone.inputs[1]);
        if (error) return error;
        *static_cast<float *>(backbone.inputs[2].data)+=1;
        std::memcpy(synthesis.inputs[1].data,fixture.noise+frame*32,32*sizeof(float));
        *static_cast<float *>(synthesis.inputs[3].data)=16.0f*frame;
        phonon_stage=21;
        auto decoded=synthesis.execute();
        if (!decoded.ok()) return -900-static_cast<int>(decoded.error());
        if (decoded->size()!=17) return -508;
        float eos=(*decoded)[1].toTensor().const_data_ptr<float>()[0];
        const float *latent=(*decoded)[0].toTensor().const_data_ptr<float>();
        if (!finite_float(eos) || !finite_floats(latent,32)) return -510;
        error=synthesis.copy_output(*decoded,3,backbone.inputs[0]);
        if (!error) error=synthesis.copy_output(*decoded,4,synthesis.inputs[2]);
        for (size_t i=4;!error && i<synthesis.values.size();++i) error=synthesis.copy_output(*decoded,i+1,synthesis.inputs[i]);
        if (error) return error;
        // Include PCM conversion plus handoff storage. No physical playback.
        static volatile int16_t pcm16[1920];
        auto pcm=(*decoded)[2].toTensor();
        if (pcm.numel()!=1920) return -509;
        const float *audio=pcm.const_data_ptr<float>();
        for (unsigned i=0;i<1920;++i) {
            if (!finite_float(audio[i])) return -510;
            pcm16[i]=static_cast<int16_t>(std::max(-1.0f,std::min(1.0f,audio[i]))*32767);
        }
        phonon_frame_ticks[case_index][frame]=osKernelGetTickCount()-start;
        phonon_frame_delegate_ticks[case_index][frame]=phonon_delegate_ticks;
        if (phonon_delegate_active || phonon_delegate_pairing_error || phonon_delegate_calls!=61 ||
            phonon_delegate_ticks>phonon_frame_ticks[case_index][frame]) return -531;
        phonon_frame_cpu_ticks[case_index][frame]=phonon_frame_ticks[case_index][frame]-phonon_delegate_ticks;
        phonon_npu_cycles[case_index][frame]=ethos_benchmark.cycles;
        phonon_frame_invocations[case_index][frame]=ethos_benchmark.invocations;
        if (ethos_benchmark.invocations!=61 || !ethos_benchmark.cycles || ethos_benchmark.overflow) return -511;
        if (frame<2) {
            for (unsigned i=0;i<32;++i) phonon_latent_max_error=std::max(float(phonon_latent_max_error),std::fabs(latent[i]-fixture.expected_latent[frame*32+i]));
            phonon_eos_max_error=std::max(float(phonon_eos_max_error),std::fabs(eos-fixture.expected_eos[frame]));
            for (unsigned i=0;i<1920;++i) phonon_pcm_max_error=std::max(float(phonon_pcm_max_error),std::fabs(audio[i]-fixture.expected_pcm[frame*1920+i]));
        }
        phonon_frames[case_index]=frame+1;
        auto usage=arm::embedded::runtime_memory_usage();
        phonon_method_peak=usage.method_peak;
        phonon_temp_peak=usage.temp_peak;
        printf("Phonon case=%u frame=%u ticks=%lu NPU=%llu calls=%lu EOS=%.6f\n",case_index,frame,(unsigned long)phonon_frame_ticks[case_index][frame],(unsigned long long)phonon_npu_cycles[case_index][frame],(unsigned long)phonon_frame_invocations[case_index][frame],eos);
        if (eos>4 && eos_frame<0) eos_frame=frame;
        if (eos_frame>=0 && frame>=unsigned(eos_frame)+(fixture.word_count<=4?3:1)) break;
    }
    phonon_eos_frame[case_index]=eos_frame<0?~0U:unsigned(eos_frame);
    return eos_frame<0?-512:0;
}
}
extern "C" int app_main(void)
{
    int error=initialize();
    if (!error) error=check_first_token();
    for (unsigned i=0;!error && i<phonon_fixture::kTextCount;++i) error=run_case(i);
    auto usage=arm::embedded::runtime_memory_usage();
    phonon_method_peak=usage.method_peak; phonon_temp_peak=usage.temp_peak;
    phonon_error=error; phonon_stage=error?0xFFFFFFFF:100; phonon_complete=error?0:1;
    printf("Phonon benchmark: error=%d method_peak=%lu temp_peak=%lu external=%lu\n",error,(unsigned long)phonon_method_peak,(unsigned long)phonon_temp_peak,(unsigned long)phonon_external_bytes);
    printf("First-two-frame PTQ max abs error: hidden=%.8f latent=%.8f EOS=%.8f PCM=%.8f\n",float(phonon_hidden_max_error),float(phonon_latent_max_error),float(phonon_eos_max_error),float(phonon_pcm_max_error));
    fflush(stdout);
    return error;
}
