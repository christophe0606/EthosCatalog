// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
/*

Generated with CMSIS-Stream python scripts.
The generated code is not covered by CMSIS-Stream license.

The support classes and code are covered by CMSIS-Stream license.

*/


#include <cstdint>
#include "encodec_app_config.hpp"
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
static uint8_t schedule[6]=
{
4,1,5,2,0,3,
};

/*

Internal ID identification for the nodes

*/
#define DECODER_INTERNAL_ID 0
#define DUP0_INTERNAL_ID 1
#define ENCODER_INTERNAL_ID 2
#define SDS_SINK_INTERNAL_ID 3
#define SDS_SOURCE_INTERNAL_ID 4
#define WAVEFORM_INTERNAL_ID 5
#define LCD_INTERNAL_ID 6

/* Initialize the selectors global IDs in each class */
template<>
std::array<uint16_t,1> WaveformCapture<float,2880>::selectors = {SEL_WAVEFORM_ID};
std::array<uint16_t,1> WaveformDisplay::selectors = {SEL_WAVEFORM_ID};


/***********

Node identification

************/
static CStreamNode identifiedNodes[STREAM_APP_NB_IDENTIFIED_NODES]={0};

CG_BEFORE_FIFO_BUFFERS
/***********

FIFO buffers

************/
#define FIFOSIZE0 72
#define FIFOSIZE1 2880
#define FIFOSIZE2 2880
#define FIFOSIZE3 2880
#define FIFOSIZE4 2880

#define BUFFERSIZE0 11520
CG_BEFORE_BUFFER
uint8_t stream_app_buf0[BUFFERSIZE0]={0};

#define BUFFERSIZE1 11520
CG_BEFORE_BUFFER
uint8_t stream_app_buf1[BUFFERSIZE1]={0};


typedef struct {
FIFO<int32_t,FIFOSIZE0,1,0> *fifo0;
FIFO<float,FIFOSIZE1,1,0> *fifo1;
FIFO<float,FIFOSIZE2,1,0> *fifo2;
FIFO<float,FIFOSIZE3,1,0> *fifo3;
FIFO<float,FIFOSIZE4,1,0> *fifo4;
} fifos_t;

typedef struct {
    EnCodecDecoder<int32_t,72,float,2880> *decoder;
    Duplicate<float,2880,float,2880> *dup0;
    EnCodecEncoder<float,2880,int32_t,72> *encoder;
    SdsSink<float,2880> *sds_sink;
    SdsSource<float,2880> *sds_source;
    WaveformCapture<float,2880> *waveform;
    WaveformDisplay *lcd;
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
    fifos.fifo0 = new (std::nothrow) FIFO<int32_t,FIFOSIZE0,1,0>(stream_app_buf0);
    if (fifos.fifo0==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo1 = new (std::nothrow) FIFO<float,FIFOSIZE1,1,0>(stream_app_buf1);
    if (fifos.fifo1==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo2 = new (std::nothrow) FIFO<float,FIFOSIZE2,1,0>(stream_app_buf0);
    if (fifos.fifo2==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo3 = new (std::nothrow) FIFO<float,FIFOSIZE3,1,0>(stream_app_buf1);
    if (fifos.fifo3==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo4 = new (std::nothrow) FIFO<float,FIFOSIZE4,1,0>(stream_app_buf0);
    if (fifos.fifo4==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    CG_BEFORE_NODE_INIT;
    cg_status initError;

    nodes.decoder = new (std::nothrow) EnCodecDecoder<int32_t,72,float,2880>(*(fifos.fifo0),*(fifos.fifo1));
    if (nodes.decoder==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    identifiedNodes[STREAM_APP_DECODER_ID]=createStreamNode(*nodes.decoder);
    nodes.decoder->setID(STREAM_APP_DECODER_ID);

    nodes.dup0 = new (std::nothrow) Duplicate<float,2880,float,2880>(*(fifos.fifo2),{fifos.fifo3});
    if (nodes.dup0==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.encoder = new (std::nothrow) EnCodecEncoder<float,2880,int32_t,72>(*(fifos.fifo3),*(fifos.fifo0));
    if (nodes.encoder==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    identifiedNodes[STREAM_APP_ENCODER_ID]=createStreamNode(*nodes.encoder);
    nodes.encoder->setID(STREAM_APP_ENCODER_ID);

    nodes.sds_sink = new (std::nothrow) SdsSink<float,2880>(*(fifos.fifo1),encodecRecordParams);
    if (nodes.sds_sink==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    identifiedNodes[STREAM_APP_SDS_SINK_ID]=createStreamNode(*nodes.sds_sink);
    nodes.sds_sink->setID(STREAM_APP_SDS_SINK_ID);

    nodes.sds_source = new (std::nothrow) SdsSource<float,2880>(*(fifos.fifo2),encodecReplayParams);
    if (nodes.sds_source==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    identifiedNodes[STREAM_APP_SDS_SOURCE_ID]=createStreamNode(*nodes.sds_source);
    nodes.sds_source->setID(STREAM_APP_SDS_SOURCE_ID);

    nodes.waveform = new (std::nothrow) WaveformCapture<float,2880>(*(fifos.fifo4),evtQueue);
    if (nodes.waveform==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    identifiedNodes[STREAM_APP_WAVEFORM_ID]=createStreamNode(*nodes.waveform);
    nodes.waveform->setID(STREAM_APP_WAVEFORM_ID);

    nodes.lcd = new (std::nothrow) WaveformDisplay(params->lcd);
    if (nodes.lcd==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    identifiedNodes[STREAM_APP_LCD_ID]=createStreamNode(*nodes.lcd);
    nodes.lcd->setID(STREAM_APP_LCD_ID);


/* Subscribe nodes for the event system*/
    nodes.waveform->subscribe(0,*nodes.lcd,0);

    initError = CG_SUCCESS;
    initError = nodes.decoder->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.dup0->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.encoder->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.sds_sink->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.sds_source->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.waveform->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.lcd->init();
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

    if (nodes.decoder!=NULL)
    {
        delete nodes.decoder;
    }
    if (nodes.dup0!=NULL)
    {
        delete nodes.dup0;
    }
    if (nodes.encoder!=NULL)
    {
        delete nodes.encoder;
    }
    if (nodes.sds_sink!=NULL)
    {
        delete nodes.sds_sink;
    }
    if (nodes.sds_source!=NULL)
    {
        delete nodes.sds_source;
    }
    if (nodes.waveform!=NULL)
    {
        delete nodes.waveform;
    }
    if (nodes.lcd!=NULL)
    {
        delete nodes.lcd;
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
   // Buffers are set to zero too
   if (all)
   {
       std::fill_n(stream_app_buf0, BUFFERSIZE0, (uint8_t)0);
       std::fill_n(stream_app_buf1, BUFFERSIZE1, (uint8_t)0);
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
        for(; id < 6; id++)
        {
            CG_BEFORE_NODE_EXECUTION(schedule[id]);
            switch(schedule[id])
            {
                case 0:
                {

                   cgStaticError = nodes.decoder->run();
                }
                break;

                case 1:
                {

                   cgStaticError = nodes.dup0->run();
                }
                break;

                case 2:
                {

                   cgStaticError = nodes.encoder->run();
                }
                break;

                case 3:
                {

                   cgStaticError = nodes.sds_sink->run();
                }
                break;

                case 4:
                {

                   cgStaticError = nodes.sds_source->run();
                }
                break;

                case 5:
                {

                   cgStaticError = nodes.waveform->run();
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
