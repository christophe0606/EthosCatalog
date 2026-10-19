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
static uint8_t schedule[8]=
{
0,1,4,5,6,2,7,3,
};

/*

Internal ID identification for the nodes

*/
#define CAMERA_INTERNAL_ID 0
#define CROP_INTERNAL_ID 1
#define DECODE_INTERNAL_ID 2
#define DISPLAY_INTERNAL_ID 3
#define DUP0_INTERNAL_ID 4
#define GRAY_INTERNAL_ID 5
#define NETWORK_INTERNAL_ID 6
#define NMS_INTERNAL_ID 7



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
#define FIFOSIZE6 1
#define FIFOSIZE7 1
#define FIFOSIZE8 1

#define BUFFERSIZE0 24
CG_BEFORE_BUFFER
uint8_t stream_app_buf0[BUFFERSIZE0]={0};

#define BUFFERSIZE1 24
CG_BEFORE_BUFFER
uint8_t stream_app_buf1[BUFFERSIZE1]={0};

#define BUFFERSIZE2 24
CG_BEFORE_BUFFER
uint8_t stream_app_buf2[BUFFERSIZE2]={0};


typedef struct {
FIFO<Rgb565Image,FIFOSIZE0,1,0> *fifo0;
FIFO<GrayInt8Image,FIFOSIZE1,1,0> *fifo1;
FIFO<InferredFrame,FIFOSIZE2,1,0> *fifo2;
FIFO<ScoredBoxList,FIFOSIZE3,1,0> *fifo3;
FIFO<PreparedFrame,FIFOSIZE4,1,0> *fifo4;
FIFO<ScoredBoxList,FIFOSIZE5,1,0> *fifo5;
FIFO<Rgb888Image,FIFOSIZE6,1,0> *fifo6;
FIFO<Rgb888Image,FIFOSIZE7,1,0> *fifo7;
FIFO<Rgb888Image,FIFOSIZE8,1,0> *fifo8;
} fifos_t;

typedef struct {
    CameraCapture<Rgb565Image,1> *camera;
    Rgb565CenterCrop<Rgb565Image,1,Rgb888Image,1> *crop;
    FaceDecode<InferredFrame,1,ScoredBoxList,1,PreparedFrame,1> *decode;
    FaceDisplay<ScoredBoxList,1,PreparedFrame,1> *display;
    Duplicate<Rgb888Image,1,Rgb888Image,1> *dup0;
    Rgb888ToGrayInt8<Rgb888Image,1,GrayInt8Image,1> *gray;
    FaceInference<GrayInt8Image,1,Rgb888Image,1,InferredFrame,1> *network;
    NonMaximumSuppression<ScoredBoxList,1,ScoredBoxList,1> *nms;
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
    fifos.fifo0 = new (std::nothrow) FIFO<Rgb565Image,FIFOSIZE0,1,0>(stream_app_buf0);
    if (fifos.fifo0==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo1 = new (std::nothrow) FIFO<GrayInt8Image,FIFOSIZE1,1,0>(stream_app_buf1);
    if (fifos.fifo1==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo2 = new (std::nothrow) FIFO<InferredFrame,FIFOSIZE2,1,0>(stream_app_buf0);
    if (fifos.fifo2==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo3 = new (std::nothrow) FIFO<ScoredBoxList,FIFOSIZE3,1,0>(stream_app_buf1);
    if (fifos.fifo3==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo4 = new (std::nothrow) FIFO<PreparedFrame,FIFOSIZE4,1,0>(stream_app_buf2);
    if (fifos.fifo4==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo5 = new (std::nothrow) FIFO<ScoredBoxList,FIFOSIZE5,1,0>(stream_app_buf0);
    if (fifos.fifo5==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo6 = new (std::nothrow) FIFO<Rgb888Image,FIFOSIZE6,1,0>(stream_app_buf1);
    if (fifos.fifo6==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo7 = new (std::nothrow) FIFO<Rgb888Image,FIFOSIZE7,1,0>(stream_app_buf0);
    if (fifos.fifo7==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo8 = new (std::nothrow) FIFO<Rgb888Image,FIFOSIZE8,1,0>(stream_app_buf2);
    if (fifos.fifo8==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    CG_BEFORE_NODE_INIT;
    cg_status initError;

    nodes.camera = new (std::nothrow) CameraCapture<Rgb565Image,1>(*(fifos.fifo0),params->camera);
    if (nodes.camera==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.crop = new (std::nothrow) Rgb565CenterCrop<Rgb565Image,1,Rgb888Image,1>(*(fifos.fifo0),*(fifos.fifo6),params->crop);
    if (nodes.crop==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.decode = new (std::nothrow) FaceDecode<InferredFrame,1,ScoredBoxList,1,PreparedFrame,1>(*(fifos.fifo2),*(fifos.fifo3),*(fifos.fifo4),params->decode);
    if (nodes.decode==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.display = new (std::nothrow) FaceDisplay<ScoredBoxList,1,PreparedFrame,1>(*(fifos.fifo5),*(fifos.fifo4),params->display);
    if (nodes.display==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.dup0 = new (std::nothrow) Duplicate<Rgb888Image,1,Rgb888Image,1>(*(fifos.fifo6),{fifos.fifo7,fifos.fifo8});
    if (nodes.dup0==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.gray = new (std::nothrow) Rgb888ToGrayInt8<Rgb888Image,1,GrayInt8Image,1>(*(fifos.fifo7),*(fifos.fifo1),params->gray);
    if (nodes.gray==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.network = new (std::nothrow) FaceInference<GrayInt8Image,1,Rgb888Image,1,InferredFrame,1>(*(fifos.fifo1),*(fifos.fifo8),*(fifos.fifo2),params->network);
    if (nodes.network==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.nms = new (std::nothrow) NonMaximumSuppression<ScoredBoxList,1,ScoredBoxList,1>(*(fifos.fifo3),*(fifos.fifo5),params->nms);
    if (nodes.nms==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }


/* Subscribe nodes for the event system*/

    initError = CG_SUCCESS;
    initError = nodes.camera->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.crop->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.decode->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.display->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.dup0->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.gray->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.network->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.nms->init();
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

    if (nodes.camera!=NULL)
    {
        delete nodes.camera;
    }
    if (nodes.crop!=NULL)
    {
        delete nodes.crop;
    }
    if (nodes.decode!=NULL)
    {
        delete nodes.decode;
    }
    if (nodes.display!=NULL)
    {
        delete nodes.display;
    }
    if (nodes.dup0!=NULL)
    {
        delete nodes.dup0;
    }
    if (nodes.gray!=NULL)
    {
        delete nodes.gray;
    }
    if (nodes.network!=NULL)
    {
        delete nodes.network;
    }
    if (nodes.nms!=NULL)
    {
        delete nodes.nms;
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
   // Buffers are set to zero too
   if (all)
   {
       std::fill_n(stream_app_buf0, BUFFERSIZE0, (uint8_t)0);
       std::fill_n(stream_app_buf1, BUFFERSIZE1, (uint8_t)0);
       std::fill_n(stream_app_buf2, BUFFERSIZE2, (uint8_t)0);
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
        for(; id < 8; id++)
        {
            CG_BEFORE_NODE_EXECUTION(schedule[id]);
            switch(schedule[id])
            {
                case 0:
                {

                   cgStaticError = nodes.camera->run();
                }
                break;

                case 1:
                {

                   cgStaticError = nodes.crop->run();
                }
                break;

                case 2:
                {

                   cgStaticError = nodes.decode->run();
                }
                break;

                case 3:
                {

                   cgStaticError = nodes.display->run();
                }
                break;

                case 4:
                {

                   cgStaticError = nodes.dup0->run();
                }
                break;

                case 5:
                {

                   cgStaticError = nodes.gray->run();
                }
                break;

                case 6:
                {

                   cgStaticError = nodes.network->run();
                }
                break;

                case 7:
                {

                   cgStaticError = nodes.nms->run();
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
