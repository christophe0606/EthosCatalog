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
static uint8_t schedule[3]=
{
2,0,1,
};

/*

Internal ID identification for the nodes

*/
#define EDITOR_INTERNAL_ID 0
#define SEND_INTERNAL_ID 1
#define UART_INTERNAL_ID 2
#define DISPLAY_INTERNAL_ID 3
#define NETWORK_INTERNAL_ID 4

/* Initialize the selectors global IDs in each class */
template<>
std::array<uint16_t,2> SendToNetwork<SentenceSnapshot,1>::selectors = {SEL_REQUEST_ID, SEL_ACK_ID};
std::array<uint16_t,1> HandwritingDisplay::selectors = {SEL_STROKE_ID};
std::array<uint16_t,3> HandwritingNetwork::selectors = {SEL_REQUEST_ID, SEL_ACK_ID, SEL_STROKE_ID};


/* No identified nodes found so array is forced to length 1*/
static CStreamNode identifiedNodes[1]={0};

CG_BEFORE_FIFO_BUFFERS
/***********

FIFO buffers

************/
#define FIFOSIZE0 1
#define FIFOSIZE1 1

#define BUFFERSIZE0 36
CG_BEFORE_BUFFER
uint8_t stream_app_buf0[BUFFERSIZE0]={0};

#define BUFFERSIZE1 84
CG_BEFORE_BUFFER
uint8_t stream_app_buf1[BUFFERSIZE1]={0};


typedef struct {
FIFO<UartBlock,FIFOSIZE0,1,0> *fifo0;
FIFO<SentenceSnapshot,FIFOSIZE1,1,0> *fifo1;
} fifos_t;

typedef struct {
    SentenceInput<UartBlock,1,SentenceSnapshot,1> *editor;
    SendToNetwork<SentenceSnapshot,1> *send;
    UartSource<UartBlock,1> *uart;
    HandwritingDisplay *display;
    HandwritingNetwork *network;
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
    fifos.fifo0 = new (std::nothrow) FIFO<UartBlock,FIFOSIZE0,1,0>(stream_app_buf0);
    if (fifos.fifo0==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo1 = new (std::nothrow) FIFO<SentenceSnapshot,FIFOSIZE1,1,0>(stream_app_buf1);
    if (fifos.fifo1==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    CG_BEFORE_NODE_INIT;
    cg_status initError;

    nodes.editor = new (std::nothrow) SentenceInput<UartBlock,1,SentenceSnapshot,1>(*(fifos.fifo0),*(fifos.fifo1),params->editor);
    if (nodes.editor==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.send = new (std::nothrow) SendToNetwork<SentenceSnapshot,1>(*(fifos.fifo1),evtQueue,params->send);
    if (nodes.send==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.uart = new (std::nothrow) UartSource<UartBlock,1>(*(fifos.fifo0),params->uart);
    if (nodes.uart==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.display = new (std::nothrow) HandwritingDisplay(params->display);
    if (nodes.display==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.network = new (std::nothrow) HandwritingNetwork(evtQueue,params->network);
    if (nodes.network==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }


/* Subscribe nodes for the event system*/
    nodes.send->subscribe(0,*nodes.network,0);
    nodes.network->subscribe(0,*nodes.send,0);
    nodes.network->subscribe(1,*nodes.display,0);

    initError = CG_SUCCESS;
    initError = nodes.editor->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.send->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.uart->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.display->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.network->init();
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

    if (nodes.editor!=NULL)
    {
        delete nodes.editor;
    }
    if (nodes.send!=NULL)
    {
        delete nodes.send;
    }
    if (nodes.uart!=NULL)
    {
        delete nodes.uart;
    }
    if (nodes.display!=NULL)
    {
        delete nodes.display;
    }
    if (nodes.network!=NULL)
    {
        delete nodes.network;
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
        for(; id < 3; id++)
        {
            CG_BEFORE_NODE_EXECUTION(schedule[id]);
            switch(schedule[id])
            {
                case 0:
                {

                   cgStaticError = nodes.editor->run();
                }
                break;

                case 1:
                {

                   cgStaticError = nodes.send->run();
                }
                break;

                case 2:
                {

                   cgStaticError = nodes.uart->run();
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
