// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
/*

Generated with CMSIS-Stream python scripts.
The generated code is not covered by CMSIS-Stream license.

The support classes and code are covered by CMSIS-Stream license.

*/


#include <cstdint>
#include "dtln_app_config.hpp"
#include "stream_platform_config.hpp"
#include "cg_enums.h"
#include "StreamNode.hpp"
#include "cstream_node.h"
#include "IdentifiedNode.hpp"
#include "EventQueue.hpp"
#include "GenericNodes.hpp"
#include "AppNodes.hpp"
#include "scheduler_app.h"

#if !defined(CHECKERROR)
#define CHECKERROR       if (cgStaticError < 0) \
       {\
         goto errorHandling;\
       }

#endif


#if !defined(CG_BEFORE_ITERATION)
#define CG_BEFORE_ITERATION
#endif

#if !defined(CG_AFTER_ITERATION)
#define CG_AFTER_ITERATION
#endif

#if !defined(CG_BEFORE_SCHEDULE)
#define CG_BEFORE_SCHEDULE
#endif

#if !defined(CG_AFTER_SCHEDULE)
#define CG_AFTER_SCHEDULE
#endif

#if !defined(CG_BEFORE_BUFFER)
#define CG_BEFORE_BUFFER
#endif

#if !defined(CG_BEFORE_FIFO_BUFFERS)
#define CG_BEFORE_FIFO_BUFFERS
#endif

#if !defined(CG_BEFORE_FIFO_INIT)
#define CG_BEFORE_FIFO_INIT
#endif

#if !defined(CG_BEFORE_NODE_INIT)
#define CG_BEFORE_NODE_INIT
#endif

#if !defined(CG_AFTER_INCLUDES)
#define CG_AFTER_INCLUDES
#endif

#if !defined(CG_BEFORE_SCHEDULER_FUNCTION)
#define CG_BEFORE_SCHEDULER_FUNCTION
#endif

#if !defined(CG_BEFORE_NODE_EXECUTION)
#define CG_BEFORE_NODE_EXECUTION(ID)
#endif

#if !defined(CG_AFTER_NODE_EXECUTION)
#define CG_AFTER_NODE_EXECUTION(ID)
#endif





CG_AFTER_INCLUDES


using namespace arm_cmsis_stream;

/*

Description of the scheduling.

*/
static uint8_t schedule[15]=
{
11,12,5,3,1,4,9,0,7,8,13,2,14,6,10,
};

/*

Internal ID identification for the nodes

*/
#define DUP0_INTERNAL_ID 0
#define DUP1_INTERNAL_ID 1
#define MASK_INVERSE_INTERNAL_ID 2
#define MIC_FFT_INTERNAL_ID 3
#define MIC_MAGNITUDE_INTERNAL_ID 4
#define MIC_WINDOW_INTERNAL_ID 5
#define OVERLAP_ADD_INTERNAL_ID 6
#define REFERENCE_FFT_INTERNAL_ID 7
#define REFERENCE_MAGNITUDE_INTERNAL_ID 8
#define REFERENCE_WINDOW_INTERNAL_ID 9
#define SDS_SINK_INTERNAL_ID 10
#define SDS_SOURCE_INTERNAL_ID 11
#define SPLIT_INPUT_INTERNAL_ID 12
#define STAGE1_INTERNAL_ID 13
#define STAGE2_INTERNAL_ID 14



/* No identified nodes found so array is forced to length 1*/
static CStreamNode identifiedNodes[1]={0};

CG_BEFORE_FIFO_BUFFERS
/***********

FIFO buffers

************/
#define FIFOSIZE0 256
#define FIFOSIZE1 128
#define FIFOSIZE2 128
#define FIFOSIZE3 512
#define FIFOSIZE4 512
#define FIFOSIZE5 257
#define FIFOSIZE6 257
#define FIFOSIZE7 257
#define FIFOSIZE8 512
#define FIFOSIZE9 512
#define FIFOSIZE10 128
#define FIFOSIZE11 512
#define FIFOSIZE12 512
#define FIFOSIZE13 512
#define FIFOSIZE14 512
#define FIFOSIZE15 512
#define FIFOSIZE16 512

#define BUFFERSIZE0 2048
CG_BEFORE_BUFFER
uint8_t stream_app_buf0[BUFFERSIZE0]={0};

#define BUFFERSIZE1 2048
CG_BEFORE_BUFFER
uint8_t stream_app_buf1[BUFFERSIZE1]={0};

#define BUFFERSIZE2 2048
CG_BEFORE_BUFFER
uint8_t stream_app_buf2[BUFFERSIZE2]={0};

#define BUFFERSIZE3 2048
CG_BEFORE_BUFFER
uint8_t stream_app_buf3[BUFFERSIZE3]={0};

#define BUFFERSIZE4 1028
CG_BEFORE_BUFFER
uint8_t stream_app_buf4[BUFFERSIZE4]={0};


typedef struct {
FIFO<float,FIFOSIZE0,1,0> *fifo0;
FIFO<float,FIFOSIZE1,1,0> *fifo1;
FIFO<float,FIFOSIZE2,1,0> *fifo2;
FIFO<float,FIFOSIZE3,1,0> *fifo3;
FIFO<float,FIFOSIZE4,1,0> *fifo4;
FIFO<float,FIFOSIZE5,1,0> *fifo5;
FIFO<float,FIFOSIZE6,1,0> *fifo6;
FIFO<float,FIFOSIZE7,1,0> *fifo7;
FIFO<float,FIFOSIZE8,1,0> *fifo8;
FIFO<float,FIFOSIZE9,1,0> *fifo9;
FIFO<float,FIFOSIZE10,1,0> *fifo10;
FIFO<float,FIFOSIZE11,1,0> *fifo11;
FIFO<float,FIFOSIZE12,1,0> *fifo12;
FIFO<float,FIFOSIZE13,1,0> *fifo13;
FIFO<float,FIFOSIZE14,1,0> *fifo14;
FIFO<float,FIFOSIZE15,1,0> *fifo15;
FIFO<float,FIFOSIZE16,1,0> *fifo16;
} fifos_t;

typedef struct {
    Duplicate<float,512,float,512> *dup0;
    Duplicate<float,512,float,512> *dup1;
    RFFTMaskInverse<float,512,float,257,float,512> *mask_inverse;
    FFT<float,512,float,512> *mic_fft;
    RawRFFTMagnitude<float,512,float,257> *mic_magnitude;
    SlidingBuffer<float,128,float,512> *mic_window;
    OverlapAdd<float,512,float,128> *overlap_add;
    PreservingFFT<float,512,float,512> *reference_fft;
    RawRFFTMagnitude<float,512,float,257> *reference_magnitude;
    SlidingBuffer<float,128,float,512> *reference_window;
    SdsSink<float,128> *sds_sink;
    SdsSource<float,256> *sds_source;
    DtlnSplitInput<float,256,float,128,float,128> *split_input;
    DtlnMethod<float,257,float,257,float,257> *stage1;
    DtlnMethod<float,512,float,512,float,512> *stage2;
} nodes_t;


static fifos_t fifos={0};

static nodes_t nodes={0};

CStreamNode* get_scheduler_app_node(int32_t nodeID)
{
    if (nodeID >= STREAM_APP_NB_IDENTIFIED_NODES)
    {
        return(nullptr);
    }
    if (nodeID < 0)
    {
        return(nullptr);
    }
    return(&identifiedNodes[nodeID]);
}

int init_scheduler_app(void *evtQueue_,AppParams *params)
{
    EventQueue *evtQueue = reinterpret_cast<EventQueue *>(evtQueue_);
    (void)evtQueue;

    CG_BEFORE_FIFO_INIT;
    fifos.fifo0 = new (std::nothrow) FIFO<float,FIFOSIZE0,1,0>(stream_app_buf1);
    if (fifos.fifo0==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo1 = new (std::nothrow) FIFO<float,FIFOSIZE1,1,0>(stream_app_buf0);
    if (fifos.fifo1==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo2 = new (std::nothrow) FIFO<float,FIFOSIZE2,1,0>(stream_app_buf2);
    if (fifos.fifo2==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo3 = new (std::nothrow) FIFO<float,FIFOSIZE3,1,0>(stream_app_buf1);
    if (fifos.fifo3==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo4 = new (std::nothrow) FIFO<float,FIFOSIZE4,1,0>(stream_app_buf3);
    if (fifos.fifo4==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo5 = new (std::nothrow) FIFO<float,FIFOSIZE5,1,0>(stream_app_buf0);
    if (fifos.fifo5==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo6 = new (std::nothrow) FIFO<float,FIFOSIZE6,1,0>(stream_app_buf4);
    if (fifos.fifo6==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo7 = new (std::nothrow) FIFO<float,FIFOSIZE7,1,0>(stream_app_buf3);
    if (fifos.fifo7==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo8 = new (std::nothrow) FIFO<float,FIFOSIZE8,1,0>(stream_app_buf0);
    if (fifos.fifo8==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo9 = new (std::nothrow) FIFO<float,FIFOSIZE9,1,0>(stream_app_buf1);
    if (fifos.fifo9==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo10 = new (std::nothrow) FIFO<float,FIFOSIZE10,1,0>(stream_app_buf0);
    if (fifos.fifo10==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo11 = new (std::nothrow) FIFO<float,FIFOSIZE11,1,0>(stream_app_buf3);
    if (fifos.fifo11==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo12 = new (std::nothrow) FIFO<float,FIFOSIZE12,1,0>(stream_app_buf2);
    if (fifos.fifo12==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo13 = new (std::nothrow) FIFO<float,FIFOSIZE13,1,0>(stream_app_buf2);
    if (fifos.fifo13==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo14 = new (std::nothrow) FIFO<float,FIFOSIZE14,1,0>(stream_app_buf0);
    if (fifos.fifo14==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo15 = new (std::nothrow) FIFO<float,FIFOSIZE15,1,0>(stream_app_buf1);
    if (fifos.fifo15==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo16 = new (std::nothrow) FIFO<float,FIFOSIZE16,1,0>(stream_app_buf1);
    if (fifos.fifo16==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    CG_BEFORE_NODE_INIT;
    cg_status initError;

    nodes.dup0 = new (std::nothrow) Duplicate<float,512,float,512>(*(fifos.fifo11),{fifos.fifo12});
    if (nodes.dup0==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.dup1 = new (std::nothrow) Duplicate<float,512,float,512>(*(fifos.fifo14),{fifos.fifo15});
    if (nodes.dup1==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.mask_inverse = new (std::nothrow) RFFTMaskInverse<float,512,float,257,float,512>(*(fifos.fifo16),*(fifos.fifo7),*(fifos.fifo8));
    if (nodes.mask_inverse==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.mic_fft = new (std::nothrow) FFT<float,512,float,512>(*(fifos.fifo3),*(fifos.fifo14));
    if (nodes.mic_fft==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.mic_magnitude = new (std::nothrow) RawRFFTMagnitude<float,512,float,257>(*(fifos.fifo15),*(fifos.fifo5));
    if (nodes.mic_magnitude==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.mic_window = new (std::nothrow) SlidingBuffer<float,128,float,512>(*(fifos.fifo1),*(fifos.fifo3));
    if (nodes.mic_window==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.overlap_add = new (std::nothrow) OverlapAdd<float,512,float,128>(*(fifos.fifo9),*(fifos.fifo10));
    if (nodes.overlap_add==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.reference_fft = new (std::nothrow) PreservingFFT<float,512,float,512>(*(fifos.fifo12),*(fifos.fifo4));
    if (nodes.reference_fft==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.reference_magnitude = new (std::nothrow) RawRFFTMagnitude<float,512,float,257>(*(fifos.fifo4),*(fifos.fifo6));
    if (nodes.reference_magnitude==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.reference_window = new (std::nothrow) SlidingBuffer<float,128,float,512>(*(fifos.fifo2),*(fifos.fifo11));
    if (nodes.reference_window==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.sds_sink = new (std::nothrow) SdsSink<float,128>(*(fifos.fifo10),dtlnRecordParams);
    if (nodes.sds_sink==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.sds_source = new (std::nothrow) SdsSource<float,256>(*(fifos.fifo0),dtlnReplayParams);
    if (nodes.sds_source==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.split_input = new (std::nothrow) DtlnSplitInput<float,256,float,128,float,128>(*(fifos.fifo0),*(fifos.fifo1),*(fifos.fifo2));
    if (nodes.split_input==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.stage1 = new (std::nothrow) DtlnMethod<float,257,float,257,float,257>(*(fifos.fifo5),*(fifos.fifo6),*(fifos.fifo7),params->stage1);
    if (nodes.stage1==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.stage2 = new (std::nothrow) DtlnMethod<float,512,float,512,float,512>(*(fifos.fifo8),*(fifos.fifo13),*(fifos.fifo9),params->stage2);
    if (nodes.stage2==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }


/* Subscribe nodes for the event system*/

    initError = CG_SUCCESS;
    initError = nodes.dup0->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.dup1->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.mask_inverse->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.mic_fft->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.mic_magnitude->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.mic_window->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.overlap_add->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.reference_fft->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.reference_magnitude->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.reference_window->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.sds_sink->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.sds_source->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.split_input->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.stage1->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.stage2->init();
    if (initError != CG_SUCCESS)
        return(initError);




    return(CG_SUCCESS);

}

void free_scheduler_app()
{
    if (fifos.fifo0!=NULL)
    {
       delete fifos.fifo0;
    }
    if (fifos.fifo1!=NULL)
    {
       delete fifos.fifo1;
    }
    if (fifos.fifo2!=NULL)
    {
       delete fifos.fifo2;
    }
    if (fifos.fifo3!=NULL)
    {
       delete fifos.fifo3;
    }
    if (fifos.fifo4!=NULL)
    {
       delete fifos.fifo4;
    }
    if (fifos.fifo5!=NULL)
    {
       delete fifos.fifo5;
    }
    if (fifos.fifo6!=NULL)
    {
       delete fifos.fifo6;
    }
    if (fifos.fifo7!=NULL)
    {
       delete fifos.fifo7;
    }
    if (fifos.fifo8!=NULL)
    {
       delete fifos.fifo8;
    }
    if (fifos.fifo9!=NULL)
    {
       delete fifos.fifo9;
    }
    if (fifos.fifo10!=NULL)
    {
       delete fifos.fifo10;
    }
    if (fifos.fifo11!=NULL)
    {
       delete fifos.fifo11;
    }
    if (fifos.fifo12!=NULL)
    {
       delete fifos.fifo12;
    }
    if (fifos.fifo13!=NULL)
    {
       delete fifos.fifo13;
    }
    if (fifos.fifo14!=NULL)
    {
       delete fifos.fifo14;
    }
    if (fifos.fifo15!=NULL)
    {
       delete fifos.fifo15;
    }
    if (fifos.fifo16!=NULL)
    {
       delete fifos.fifo16;
    }

    if (nodes.dup0!=NULL)
    {
        delete nodes.dup0;
    }
    if (nodes.dup1!=NULL)
    {
        delete nodes.dup1;
    }
    if (nodes.mask_inverse!=NULL)
    {
        delete nodes.mask_inverse;
    }
    if (nodes.mic_fft!=NULL)
    {
        delete nodes.mic_fft;
    }
    if (nodes.mic_magnitude!=NULL)
    {
        delete nodes.mic_magnitude;
    }
    if (nodes.mic_window!=NULL)
    {
        delete nodes.mic_window;
    }
    if (nodes.overlap_add!=NULL)
    {
        delete nodes.overlap_add;
    }
    if (nodes.reference_fft!=NULL)
    {
        delete nodes.reference_fft;
    }
    if (nodes.reference_magnitude!=NULL)
    {
        delete nodes.reference_magnitude;
    }
    if (nodes.reference_window!=NULL)
    {
        delete nodes.reference_window;
    }
    if (nodes.sds_sink!=NULL)
    {
        delete nodes.sds_sink;
    }
    if (nodes.sds_source!=NULL)
    {
        delete nodes.sds_source;
    }
    if (nodes.split_input!=NULL)
    {
        delete nodes.split_input;
    }
    if (nodes.stage1!=NULL)
    {
        delete nodes.stage1;
    }
    if (nodes.stage2!=NULL)
    {
        delete nodes.stage2;
    }
}

void reset_fifos_scheduler_app(int all)
{
    if (fifos.fifo0!=NULL)
    {
       fifos.fifo0->reset();
    }
    if (fifos.fifo1!=NULL)
    {
       fifos.fifo1->reset();
    }
    if (fifos.fifo2!=NULL)
    {
       fifos.fifo2->reset();
    }
    if (fifos.fifo3!=NULL)
    {
       fifos.fifo3->reset();
    }
    if (fifos.fifo4!=NULL)
    {
       fifos.fifo4->reset();
    }
    if (fifos.fifo5!=NULL)
    {
       fifos.fifo5->reset();
    }
    if (fifos.fifo6!=NULL)
    {
       fifos.fifo6->reset();
    }
    if (fifos.fifo7!=NULL)
    {
       fifos.fifo7->reset();
    }
    if (fifos.fifo8!=NULL)
    {
       fifos.fifo8->reset();
    }
    if (fifos.fifo9!=NULL)
    {
       fifos.fifo9->reset();
    }
    if (fifos.fifo10!=NULL)
    {
       fifos.fifo10->reset();
    }
    if (fifos.fifo11!=NULL)
    {
       fifos.fifo11->reset();
    }
    if (fifos.fifo12!=NULL)
    {
       fifos.fifo12->reset();
    }
    if (fifos.fifo13!=NULL)
    {
       fifos.fifo13->reset();
    }
    if (fifos.fifo14!=NULL)
    {
       fifos.fifo14->reset();
    }
    if (fifos.fifo15!=NULL)
    {
       fifos.fifo15->reset();
    }
    if (fifos.fifo16!=NULL)
    {
       fifos.fifo16->reset();
    }
   // Buffers are set to zero too
   if (all)
   {
       std::fill_n(stream_app_buf0, BUFFERSIZE0, (uint8_t)0);
       std::fill_n(stream_app_buf1, BUFFERSIZE1, (uint8_t)0);
       std::fill_n(stream_app_buf2, BUFFERSIZE2, (uint8_t)0);
       std::fill_n(stream_app_buf3, BUFFERSIZE3, (uint8_t)0);
       std::fill_n(stream_app_buf4, BUFFERSIZE4, (uint8_t)0);
   }
}


CG_BEFORE_SCHEDULER_FUNCTION
uint32_t scheduler_app(int *error)
{
    int cgStaticError=0;
    uint32_t nbSchedule=0;






    /* Run several schedule iterations */
    CG_BEFORE_SCHEDULE;
    while(cgStaticError==0)
    {
        /* Run a schedule iteration */
        CG_BEFORE_ITERATION;
        unsigned long id=0;
        for(; id < 15; id++)
        {
            CG_BEFORE_NODE_EXECUTION(schedule[id]);
            switch(schedule[id])
            {
                case 0:
                {

                   cgStaticError = nodes.dup0->run();
                }
                break;

                case 1:
                {

                   cgStaticError = nodes.dup1->run();
                }
                break;

                case 2:
                {

                   cgStaticError = nodes.mask_inverse->run();
                }
                break;

                case 3:
                {

                   cgStaticError = nodes.mic_fft->run();
                }
                break;

                case 4:
                {

                   cgStaticError = nodes.mic_magnitude->run();
                }
                break;

                case 5:
                {

                   cgStaticError = nodes.mic_window->run();
                }
                break;

                case 6:
                {

                   cgStaticError = nodes.overlap_add->run();
                }
                break;

                case 7:
                {

                   cgStaticError = nodes.reference_fft->run();
                }
                break;

                case 8:
                {

                   cgStaticError = nodes.reference_magnitude->run();
                }
                break;

                case 9:
                {

                   cgStaticError = nodes.reference_window->run();
                }
                break;

                case 10:
                {

                   cgStaticError = nodes.sds_sink->run();
                }
                break;

                case 11:
                {

                   cgStaticError = nodes.sds_source->run();
                }
                break;

                case 12:
                {

                   cgStaticError = nodes.split_input->run();
                }
                break;

                case 13:
                {

                   cgStaticError = nodes.stage1->run();
                }
                break;

                case 14:
                {

                   cgStaticError = nodes.stage2->run();
                }
                break;

                default:
                break;
            }
            CG_AFTER_NODE_EXECUTION(schedule[id]);
                        CHECKERROR;
        }
       CG_AFTER_ITERATION;
       nbSchedule++;
    }
errorHandling:
    CG_AFTER_SCHEDULE;
    *error=cgStaticError;
    return(nbSchedule);

}
