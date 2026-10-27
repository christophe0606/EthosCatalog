// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
/*

Generated with CMSIS-Stream python scripts.
The generated code is not covered by CMSIS-Stream license.

The support classes and code are covered by CMSIS-Stream license.

*/


#include <cstdint>
#include "app_config.hpp"
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
static uint8_t schedule[7]=
{
5,1,3,2,0,4,6,
};

/*

Internal ID identification for the nodes

*/
#define DUP0_INTERNAL_ID 0
#define EDITOR_INTERNAL_ID 1
#define PCM_INTERNAL_ID 2
#define SEND_INTERNAL_ID 3
#define SPEAKER_INTERNAL_ID 4
#define UART_INTERNAL_ID 5
#define WAVE_INTERNAL_ID 6
#define ACOUSTIC_INTERNAL_ID 7
#define FRONTEND_INTERNAL_ID 8
#define GRU_INTERNAL_ID 9
#define HEAD_INTERNAL_ID 10
#define LCD_INTERNAL_ID 11
#define PITCH_INTERNAL_ID 12
#define PRE_INTERNAL_ID 13
#define VOCODER_INTERNAL_ID 14

/* Initialize the selectors global IDs in each class */
template<>
std::array<uint16_t,1> LineInput<UartBlock,1,TextLine,1>::selectors = {SEL_ADVANCE_ID};
template<>
std::array<uint16_t,2> PlaybackSource<uint32_t,1,AudioBlock,1>::selectors = {SEL_ADVANCE_ID, SEL_ACK_ID};
template<>
std::array<uint16_t,2> SendToNetwork<TextLine,1,uint32_t,1>::selectors = {SEL_REQUEST_ID, SEL_ACK_ID};
template<>
std::array<uint16_t,1> I2SSink<AudioBlock,1>::selectors = {SEL_ADVANCE_ID};
template<>
std::array<uint16_t,1> UartSource<UartBlock,1>::selectors = {SEL_ADVANCE_ID};
template<>
std::array<uint16_t,1> WaveformSink<AudioBlock,1>::selectors = {SEL_WAVE_ID};
std::array<uint16_t,1> RvttsNetwork::selectors = {SEL_ADVANCE_ID};
std::array<uint16_t,3> TextFrontend::selectors = {SEL_REQUEST_ID, SEL_ADVANCE_ID, SEL_ACK_ID};
std::array<uint16_t,1> WaveformDisplay::selectors = {SEL_WAVE_ID};
std::array<uint16_t,1> PitchCorrect::selectors = {SEL_ADVANCE_ID};
std::array<uint16_t,1> FarganVocoder::selectors = {SEL_ADVANCE_ID};


/* No identified nodes found so array is forced to length 1*/
static CStreamNode identifiedNodes[1]={0};

CG_BEFORE_FIFO_BUFFERS
/***********

FIFO buffers

************/
#define FIFOSIZE0 1
#define FIFOSIZE1 1
#define FIFOSIZE2 1
#define FIFOSIZE3 1
#define FIFOSIZE4 1
#define FIFOSIZE5 1

#define BUFFERSIZE0 332
CG_BEFORE_BUFFER
uint8_t stream_app_buf0[BUFFERSIZE0]={0};

#define BUFFERSIZE1 36
CG_BEFORE_BUFFER
uint8_t stream_app_buf1[BUFFERSIZE1]={0};


typedef struct {
FIFO<UartBlock,FIFOSIZE0,1,0> *fifo0;
FIFO<TextLine,FIFOSIZE1,1,0> *fifo1;
FIFO<uint32_t,FIFOSIZE2,1,0> *fifo2;
FIFO<AudioBlock,FIFOSIZE3,1,0> *fifo3;
FIFO<AudioBlock,FIFOSIZE4,1,0> *fifo4;
FIFO<AudioBlock,FIFOSIZE5,1,0> *fifo5;
} fifos_t;

typedef struct {
    Duplicate<AudioBlock,1,AudioBlock,1> *dup0;
    LineInput<UartBlock,1,TextLine,1> *editor;
    PlaybackSource<uint32_t,1,AudioBlock,1> *pcm;
    SendToNetwork<TextLine,1,uint32_t,1> *send;
    I2SSink<AudioBlock,1> *speaker;
    UartSource<UartBlock,1> *uart;
    WaveformSink<AudioBlock,1> *wave;
    RvttsNetwork *acoustic;
    TextFrontend *frontend;
    RvttsNetwork *gru;
    RvttsNetwork *head;
    WaveformDisplay *lcd;
    PitchCorrect *pitch;
    RvttsNetwork *pre;
    FarganVocoder *vocoder;
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
    fifos.fifo0 = new (std::nothrow) FIFO<UartBlock,FIFOSIZE0,1,0>(stream_app_buf1);
    if (fifos.fifo0==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo1 = new (std::nothrow) FIFO<TextLine,FIFOSIZE1,1,0>(stream_app_buf0);
    if (fifos.fifo1==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo2 = new (std::nothrow) FIFO<uint32_t,FIFOSIZE2,1,0>(stream_app_buf1);
    if (fifos.fifo2==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo3 = new (std::nothrow) FIFO<AudioBlock,FIFOSIZE3,1,0>(stream_app_buf0);
    if (fifos.fifo3==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo4 = new (std::nothrow) FIFO<AudioBlock,FIFOSIZE4,1,0>(stream_app_buf0);
    if (fifos.fifo4==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo5 = new (std::nothrow) FIFO<AudioBlock,FIFOSIZE5,1,0>(stream_app_buf0);
    if (fifos.fifo5==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    CG_BEFORE_NODE_INIT;
    cg_status initError;

    nodes.dup0 = new (std::nothrow) Duplicate<AudioBlock,1,AudioBlock,1>(*(fifos.fifo3),{});
    if (nodes.dup0==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.editor = new (std::nothrow) LineInput<UartBlock,1,TextLine,1>(*(fifos.fifo0),*(fifos.fifo1),params->editor);
    if (nodes.editor==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.pcm = new (std::nothrow) PlaybackSource<uint32_t,1,AudioBlock,1>(*(fifos.fifo2),*(fifos.fifo3),evtQueue,params->pcm);
    if (nodes.pcm==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.send = new (std::nothrow) SendToNetwork<TextLine,1,uint32_t,1>(*(fifos.fifo1),*(fifos.fifo2),evtQueue,params->send);
    if (nodes.send==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.speaker = new (std::nothrow) I2SSink<AudioBlock,1>(*(fifos.fifo4),params->speaker);
    if (nodes.speaker==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.uart = new (std::nothrow) UartSource<UartBlock,1>(*(fifos.fifo0),params->uart);
    if (nodes.uart==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.wave = new (std::nothrow) WaveformSink<AudioBlock,1>(*(fifos.fifo5),evtQueue,params->wave);
    if (nodes.wave==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.acoustic = new (std::nothrow) RvttsNetwork(evtQueue,params->acoustic);
    if (nodes.acoustic==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.frontend = new (std::nothrow) TextFrontend(evtQueue,params->frontend);
    if (nodes.frontend==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.gru = new (std::nothrow) RvttsNetwork(evtQueue,params->gru);
    if (nodes.gru==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.head = new (std::nothrow) RvttsNetwork(evtQueue,params->head);
    if (nodes.head==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.lcd = new (std::nothrow) WaveformDisplay(params->lcd);
    if (nodes.lcd==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.pitch = new (std::nothrow) PitchCorrect(evtQueue,params->pitch);
    if (nodes.pitch==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.pre = new (std::nothrow) RvttsNetwork(evtQueue,params->pre);
    if (nodes.pre==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.vocoder = new (std::nothrow) FarganVocoder(evtQueue,params->vocoder);
    if (nodes.vocoder==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }


/* Subscribe nodes for the event system*/
    nodes.send->subscribe(0,*nodes.frontend,0);
    nodes.frontend->subscribe(1,*nodes.send,0);
    nodes.frontend->subscribe(0,*nodes.pre,0);
    nodes.pre->subscribe(0,*nodes.gru,0);
    nodes.gru->subscribe(0,*nodes.head,0);
    nodes.head->subscribe(0,*nodes.acoustic,0);
    nodes.acoustic->subscribe(0,*nodes.pitch,0);
    nodes.pitch->subscribe(0,*nodes.vocoder,0);
    nodes.vocoder->subscribe(0,*nodes.pcm,0);
    nodes.pcm->subscribe(0,*nodes.send,0);
    nodes.wave->subscribe(0,*nodes.lcd,0);

    initError = CG_SUCCESS;
    initError = nodes.dup0->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.editor->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.pcm->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.send->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.speaker->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.uart->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.wave->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.acoustic->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.frontend->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.gru->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.head->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.lcd->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.pitch->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.pre->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.vocoder->init();
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

    if (nodes.dup0!=NULL)
    {
        delete nodes.dup0;
    }
    if (nodes.editor!=NULL)
    {
        delete nodes.editor;
    }
    if (nodes.pcm!=NULL)
    {
        delete nodes.pcm;
    }
    if (nodes.send!=NULL)
    {
        delete nodes.send;
    }
    if (nodes.speaker!=NULL)
    {
        delete nodes.speaker;
    }
    if (nodes.uart!=NULL)
    {
        delete nodes.uart;
    }
    if (nodes.wave!=NULL)
    {
        delete nodes.wave;
    }
    if (nodes.acoustic!=NULL)
    {
        delete nodes.acoustic;
    }
    if (nodes.frontend!=NULL)
    {
        delete nodes.frontend;
    }
    if (nodes.gru!=NULL)
    {
        delete nodes.gru;
    }
    if (nodes.head!=NULL)
    {
        delete nodes.head;
    }
    if (nodes.lcd!=NULL)
    {
        delete nodes.lcd;
    }
    if (nodes.pitch!=NULL)
    {
        delete nodes.pitch;
    }
    if (nodes.pre!=NULL)
    {
        delete nodes.pre;
    }
    if (nodes.vocoder!=NULL)
    {
        delete nodes.vocoder;
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
        for(; id < 7; id++)
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

                   cgStaticError = nodes.editor->run();
                }
                break;

                case 2:
                {

                   cgStaticError = nodes.pcm->run();
                }
                break;

                case 3:
                {

                   cgStaticError = nodes.send->run();
                }
                break;

                case 4:
                {

                   cgStaticError = nodes.speaker->run();
                }
                break;

                case 5:
                {

                   cgStaticError = nodes.uart->run();
                }
                break;

                case 6:
                {

                   cgStaticError = nodes.wave->run();
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
