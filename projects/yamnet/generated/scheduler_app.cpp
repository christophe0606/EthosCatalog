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
static uint8_t schedule[5]=
{
2,3,0,1,4,
};

/*

Internal ID identification for the nodes

*/
#define AUDIO_WINDOW_INTERNAL_ID 0
#define LOG_MEL_INTERNAL_ID 1
#define MICROPHONE_INTERNAL_ID 2
#define PCM_INTERNAL_ID 3
#define SEND_INTERNAL_ID 4
#define LCD_INTERNAL_ID 5
#define NETWORK_INTERNAL_ID 6
#define POSTPROCESS_INTERNAL_ID 7

/* Initialize the selectors global IDs in each class */
template<>
std::array<uint16_t,3> AudioWindow<float,160,float,400>::selectors = {SEL_REQUEST_ID, SEL_COMPLETE_ID, SEL_RESULT_ID};
template<>
std::array<uint16_t,3> SendToNetwork<float,64>::selectors = {SEL_REQUEST_ID, SEL_COMPLETE_ID, SEL_RESULT_ID};
std::array<uint16_t,3> EventLCD::selectors = {SEL_REQUEST_ID, SEL_COMPLETE_ID, SEL_RESULT_ID};
std::array<uint16_t,3> YAMNet::selectors = {SEL_REQUEST_ID, SEL_COMPLETE_ID, SEL_SCORES_ID};
std::array<uint16_t,3> YAMNetPostprocess::selectors = {SEL_SCORES_ID, SEL_RESULT_ID, SEL_COMPLETE_ID};


/***********

Node identification

************/
static CStreamNode identifiedNodes[STREAM_APP_NB_IDENTIFIED_NODES]={0};

CG_BEFORE_FIFO_BUFFERS
/***********

FIFO buffers

************/
#define FIFOSIZE0 160
#define FIFOSIZE1 160
#define FIFOSIZE2 400
#define FIFOSIZE3 64

#define BUFFERSIZE0 640
CG_BEFORE_BUFFER
uint8_t stream_app_buf0[BUFFERSIZE0]={0};

#define BUFFERSIZE1 1600
CG_BEFORE_BUFFER
uint8_t stream_app_buf1[BUFFERSIZE1]={0};


typedef struct {
FIFO<int16_t,FIFOSIZE0,1,0> *fifo0;
FIFO<float,FIFOSIZE1,1,0> *fifo1;
FIFO<float,FIFOSIZE2,1,0> *fifo2;
FIFO<float,FIFOSIZE3,1,0> *fifo3;
} fifos_t;

typedef struct {
    AudioWindow<float,160,float,400> *audio_window;
    LogMel<float,400,float,64> *log_mel;
    MicrophoneSource<int16_t,160> *microphone;
    PCMToFloat<int16_t,160,float,160> *pcm;
    SendToNetwork<float,64> *send;
    EventLCD *lcd;
    YAMNet *network;
    YAMNetPostprocess *postprocess;
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
    fifos.fifo0 = new (std::nothrow) FIFO<int16_t,FIFOSIZE0,1,0>(stream_app_buf1);
    if (fifos.fifo0==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo1 = new (std::nothrow) FIFO<float,FIFOSIZE1,1,0>(stream_app_buf0);
    if (fifos.fifo1==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo2 = new (std::nothrow) FIFO<float,FIFOSIZE2,1,0>(stream_app_buf1);
    if (fifos.fifo2==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo3 = new (std::nothrow) FIFO<float,FIFOSIZE3,1,0>(stream_app_buf0);
    if (fifos.fifo3==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    CG_BEFORE_NODE_INIT;
    cg_status initError;

    nodes.audio_window = new (std::nothrow) AudioWindow<float,160,float,400>(*(fifos.fifo1),*(fifos.fifo2));
    if (nodes.audio_window==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    identifiedNodes[STREAM_APP_AUDIO_WINDOW_ID]=createStreamNode(*nodes.audio_window);
    nodes.audio_window->setID(STREAM_APP_AUDIO_WINDOW_ID);

    nodes.log_mel = new (std::nothrow) LogMel<float,400,float,64>(*(fifos.fifo2),*(fifos.fifo3),yamnet_log_mel_config);
    if (nodes.log_mel==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.microphone = new (std::nothrow) MicrophoneSource<int16_t,160>(*(fifos.fifo0),params->microphone);
    if (nodes.microphone==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.pcm = new (std::nothrow) PCMToFloat<int16_t,160,float,160>(*(fifos.fifo0),*(fifos.fifo1));
    if (nodes.pcm==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.send = new (std::nothrow) SendToNetwork<float,64>(*(fifos.fifo3),evtQueue,params->send);
    if (nodes.send==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    identifiedNodes[STREAM_APP_SEND_ID]=createStreamNode(*nodes.send);
    nodes.send->setID(STREAM_APP_SEND_ID);

    nodes.lcd = new (std::nothrow) EventLCD(params->lcd);
    if (nodes.lcd==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    identifiedNodes[STREAM_APP_LCD_ID]=createStreamNode(*nodes.lcd);
    nodes.lcd->setID(STREAM_APP_LCD_ID);

    nodes.network = new (std::nothrow) YAMNet(evtQueue,params->network);
    if (nodes.network==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    identifiedNodes[STREAM_APP_NETWORK_ID]=createStreamNode(*nodes.network);
    nodes.network->setID(STREAM_APP_NETWORK_ID);

    nodes.postprocess = new (std::nothrow) YAMNetPostprocess(evtQueue);
    if (nodes.postprocess==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    identifiedNodes[STREAM_APP_POSTPROCESS_ID]=createStreamNode(*nodes.postprocess);
    nodes.postprocess->setID(STREAM_APP_POSTPROCESS_ID);


/* Subscribe nodes for the event system*/
    nodes.send->subscribe(0,*nodes.network,0);
    nodes.network->subscribe(0,*nodes.send,0);
    nodes.network->subscribe(1,*nodes.postprocess,0);
    nodes.postprocess->subscribe(0,*nodes.lcd,0);
    nodes.postprocess->subscribe(1,*nodes.network,1);

    initError = CG_SUCCESS;
    initError = nodes.audio_window->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.log_mel->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.microphone->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.pcm->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.send->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.lcd->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.network->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.postprocess->init();
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

    if (nodes.audio_window!=NULL)
    {
        delete nodes.audio_window;
    }
    if (nodes.log_mel!=NULL)
    {
        delete nodes.log_mel;
    }
    if (nodes.microphone!=NULL)
    {
        delete nodes.microphone;
    }
    if (nodes.pcm!=NULL)
    {
        delete nodes.pcm;
    }
    if (nodes.send!=NULL)
    {
        delete nodes.send;
    }
    if (nodes.lcd!=NULL)
    {
        delete nodes.lcd;
    }
    if (nodes.network!=NULL)
    {
        delete nodes.network;
    }
    if (nodes.postprocess!=NULL)
    {
        delete nodes.postprocess;
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
        for(; id < 5; id++)
        {
            CG_BEFORE_NODE_EXECUTION(schedule[id]);
            switch(schedule[id])
            {
                case 0:
                {

                   cgStaticError = nodes.audio_window->run();
                }
                break;

                case 1:
                {

                   cgStaticError = nodes.log_mel->run();
                }
                break;

                case 2:
                {

                   cgStaticError = nodes.microphone->run();
                }
                break;

                case 3:
                {

                   cgStaticError = nodes.pcm->run();
                }
                break;

                case 4:
                {

                   cgStaticError = nodes.send->run();
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
