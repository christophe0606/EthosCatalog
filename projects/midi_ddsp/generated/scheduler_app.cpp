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
static uint8_t schedule[18]=
{
3,4,1,5,3,4,1,5,3,4,1,5,3,4,1,5,2,0,
};

/*

Internal ID identification for the nodes

*/
#define AUDIO_INTERNAL_ID 0
#define CONTROLS_INTERNAL_ID 1
#define REVERB_INTERNAL_ID 2
#define SCORE_INTERNAL_ID 3
#define SEND_INTERNAL_ID 4
#define SYNTH_INTERNAL_ID 5
#define CONTEXT_BACKWARD_INTERNAL_ID 6
#define CONTEXT_FORWARD_INTERNAL_ID 7
#define EXPRESSION_BACKWARD_INTERNAL_ID 8
#define EXPRESSION_FORWARD_INTERNAL_ID 9
#define EXPRESSION_STEP_INTERNAL_ID 10
#define HARMONIC_INTERNAL_ID 11
#define PITCH_STEP_INTERNAL_ID 12
#define PRECONDITION_INTERNAL_ID 13

/* Initialize the selectors global IDs in each class */
template<>
std::array<uint16_t,3> SendToNetwork<uint32_t,1,uint32_t,1>::selectors = {SEL_EXECUTE_ID, SEL_RESULT_ID, SEL_ACK_ID};
std::array<uint16_t,3> MidiMethod::selectors = {SEL_EXECUTE_ID, SEL_RESULT_ID, SEL_ACK_ID};


/* No identified nodes found so array is forced to length 1*/
static CStreamNode identifiedNodes[1]={0};

CG_BEFORE_FIFO_BUFFERS
/***********

FIFO buffers

************/
#define FIFOSIZE0 1
#define FIFOSIZE1 1
#define FIFOSIZE2 256
#define FIFOSIZE3 256
#define FIFOSIZE4 256

#define BUFFERSIZE0 1024
CG_BEFORE_BUFFER
uint8_t stream_app_buf0[BUFFERSIZE0]={0};

#define BUFFERSIZE1 4
CG_BEFORE_BUFFER
uint8_t stream_app_buf1[BUFFERSIZE1]={0};

#define BUFFERSIZE2 1024
CG_BEFORE_BUFFER
uint8_t stream_app_buf2[BUFFERSIZE2]={0};

#define BUFFERSIZE3 256
CG_BEFORE_BUFFER
float stream_app_buf3[BUFFERSIZE3]={0};


typedef struct {
FIFO<uint32_t,FIFOSIZE0,1,0> *fifo0;
FIFO<uint32_t,FIFOSIZE1,1,0> *fifo1;
FIFO<float,FIFOSIZE2,1,0> *fifo2;
FIFO<float,FIFOSIZE3,0,0> *fifo3;
FIFO<float,FIFOSIZE4,1,0> *fifo4;
} fifos_t;

typedef struct {
    AudioOutput<float,256> *audio;
    ControlSource<uint32_t,1,float,256> *controls;
    DDSPReverb<float,256,float,256> *reverb;
    MidiScore<uint32_t,1> *score;
    SendToNetwork<uint32_t,1,uint32_t,1> *send;
    DDSPSynth<float,256,float,64> *synth;
    MidiMethod *context_backward;
    MidiMethod *context_forward;
    MidiMethod *expression_backward;
    MidiMethod *expression_forward;
    MidiMethod *expression_step;
    MidiMethod *harmonic;
    MidiMethod *pitch_step;
    MidiMethod *precondition;
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
    fifos.fifo0 = new (std::nothrow) FIFO<uint32_t,FIFOSIZE0,1,0>(stream_app_buf0);
    if (fifos.fifo0==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo1 = new (std::nothrow) FIFO<uint32_t,FIFOSIZE1,1,0>(stream_app_buf1);
    if (fifos.fifo1==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo2 = new (std::nothrow) FIFO<float,FIFOSIZE2,1,0>(stream_app_buf2);
    if (fifos.fifo2==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo3 = new (std::nothrow) FIFO<float,FIFOSIZE3,0,0>(stream_app_buf3);
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

    nodes.audio = new (std::nothrow) AudioOutput<float,256>(*(fifos.fifo4),params->audio);
    if (nodes.audio==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.controls = new (std::nothrow) ControlSource<uint32_t,1,float,256>(*(fifos.fifo1),*(fifos.fifo2),params->controls);
    if (nodes.controls==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.reverb = new (std::nothrow) DDSPReverb<float,256,float,256>(*(fifos.fifo3),*(fifos.fifo4),params->reverb);
    if (nodes.reverb==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.score = new (std::nothrow) MidiScore<uint32_t,1>(*(fifos.fifo0),params->score);
    if (nodes.score==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.send = new (std::nothrow) SendToNetwork<uint32_t,1,uint32_t,1>(*(fifos.fifo0),*(fifos.fifo1),evtQueue,params->send);
    if (nodes.send==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.synth = new (std::nothrow) DDSPSynth<float,256,float,64>(*(fifos.fifo2),*(fifos.fifo3));
    if (nodes.synth==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.context_backward = new (std::nothrow) MidiMethod(evtQueue,params->context_backward);
    if (nodes.context_backward==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.context_forward = new (std::nothrow) MidiMethod(evtQueue,params->context_forward);
    if (nodes.context_forward==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.expression_backward = new (std::nothrow) MidiMethod(evtQueue,params->expression_backward);
    if (nodes.expression_backward==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.expression_forward = new (std::nothrow) MidiMethod(evtQueue,params->expression_forward);
    if (nodes.expression_forward==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.expression_step = new (std::nothrow) MidiMethod(evtQueue,params->expression_step);
    if (nodes.expression_step==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.harmonic = new (std::nothrow) MidiMethod(evtQueue,params->harmonic);
    if (nodes.harmonic==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.pitch_step = new (std::nothrow) MidiMethod(evtQueue,params->pitch_step);
    if (nodes.pitch_step==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.precondition = new (std::nothrow) MidiMethod(evtQueue,params->precondition);
    if (nodes.precondition==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }


/* Subscribe nodes for the event system*/
    nodes.send->subscribe(0,*nodes.expression_forward,0);
    nodes.expression_forward->subscribe(0,*nodes.send,0);
    nodes.expression_forward->subscribe(1,*nodes.send,1);
    nodes.send->subscribe(1,*nodes.expression_backward,0);
    nodes.expression_backward->subscribe(0,*nodes.send,2);
    nodes.expression_backward->subscribe(1,*nodes.send,3);
    nodes.send->subscribe(2,*nodes.expression_step,0);
    nodes.expression_step->subscribe(0,*nodes.send,4);
    nodes.expression_step->subscribe(1,*nodes.send,5);
    nodes.send->subscribe(3,*nodes.precondition,0);
    nodes.precondition->subscribe(0,*nodes.send,6);
    nodes.precondition->subscribe(1,*nodes.send,7);
    nodes.send->subscribe(4,*nodes.context_forward,0);
    nodes.context_forward->subscribe(0,*nodes.send,8);
    nodes.context_forward->subscribe(1,*nodes.send,9);
    nodes.send->subscribe(5,*nodes.context_backward,0);
    nodes.context_backward->subscribe(0,*nodes.send,10);
    nodes.context_backward->subscribe(1,*nodes.send,11);
    nodes.send->subscribe(6,*nodes.pitch_step,0);
    nodes.pitch_step->subscribe(0,*nodes.send,12);
    nodes.pitch_step->subscribe(1,*nodes.send,13);
    nodes.send->subscribe(7,*nodes.harmonic,0);
    nodes.harmonic->subscribe(0,*nodes.send,14);
    nodes.harmonic->subscribe(1,*nodes.send,15);

    initError = CG_SUCCESS;
    initError = nodes.audio->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.controls->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.reverb->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.score->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.send->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.synth->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.context_backward->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.context_forward->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.expression_backward->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.expression_forward->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.expression_step->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.harmonic->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.pitch_step->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.precondition->init();
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

    if (nodes.audio!=NULL)
    {
        delete nodes.audio;
    }
    if (nodes.controls!=NULL)
    {
        delete nodes.controls;
    }
    if (nodes.reverb!=NULL)
    {
        delete nodes.reverb;
    }
    if (nodes.score!=NULL)
    {
        delete nodes.score;
    }
    if (nodes.send!=NULL)
    {
        delete nodes.send;
    }
    if (nodes.synth!=NULL)
    {
        delete nodes.synth;
    }
    if (nodes.context_backward!=NULL)
    {
        delete nodes.context_backward;
    }
    if (nodes.context_forward!=NULL)
    {
        delete nodes.context_forward;
    }
    if (nodes.expression_backward!=NULL)
    {
        delete nodes.expression_backward;
    }
    if (nodes.expression_forward!=NULL)
    {
        delete nodes.expression_forward;
    }
    if (nodes.expression_step!=NULL)
    {
        delete nodes.expression_step;
    }
    if (nodes.harmonic!=NULL)
    {
        delete nodes.harmonic;
    }
    if (nodes.pitch_step!=NULL)
    {
        delete nodes.pitch_step;
    }
    if (nodes.precondition!=NULL)
    {
        delete nodes.precondition;
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
       std::fill_n(stream_app_buf2, BUFFERSIZE2, (uint8_t)0);
       std::fill_n(stream_app_buf3, BUFFERSIZE3, (float)0);
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
        for(; id < 18; id++)
        {
            CG_BEFORE_NODE_EXECUTION(schedule[id]);
            switch(schedule[id])
            {
                case 0:
                {

                   cgStaticError = nodes.audio->run();
                }
                break;

                case 1:
                {

                   cgStaticError = nodes.controls->run();
                }
                break;

                case 2:
                {

                   cgStaticError = nodes.reverb->run();
                }
                break;

                case 3:
                {

                   cgStaticError = nodes.score->run();
                }
                break;

                case 4:
                {

                   cgStaticError = nodes.send->run();
                }
                break;

                case 5:
                {

                   cgStaticError = nodes.synth->run();
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
