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
static uint8_t schedule[4]=
{
2,3,1,0,
};

/*

Internal ID identification for the nodes

*/
#define DISPLAY_CLOCK_INTERNAL_ID 0
#define INFERENCE_INTERNAL_ID 1
#define MICROPHONE_INTERNAL_ID 2
#define PCM_INTERNAL_ID 3
#define LCD_INTERNAL_ID 4

/* Initialize the selectors global IDs in each class */
template<>
std::array<uint16_t,1> VADDisplayClock<float,1>::selectors = {SEL_VAD_SNAPSHOT_ID};
std::array<uint16_t,1> VADLCD::selectors = {SEL_VAD_SNAPSHOT_ID};


/***********

Node identification

************/
static CStreamNode identifiedNodes[STREAM_APP_NB_IDENTIFIED_NODES]={0};

CG_BEFORE_FIFO_BUFFERS
/***********

FIFO buffers

************/
#define FIFOSIZE0 512
#define FIFOSIZE1 512
#define FIFOSIZE2 1

#define BUFFERSIZE0 2048
CG_BEFORE_BUFFER
uint8_t stream_app_buf0[BUFFERSIZE0]={0};

#define BUFFERSIZE1 1024
CG_BEFORE_BUFFER
uint8_t stream_app_buf1[BUFFERSIZE1]={0};


typedef struct {
FIFO<int16_t,FIFOSIZE0,1,0> *fifo0;
FIFO<float,FIFOSIZE1,1,0> *fifo1;
FIFO<float,FIFOSIZE2,1,0> *fifo2;
} fifos_t;

typedef struct {
    VADDisplayClock<float,1> *display_clock;
    SileroInference<float,512,float,1> *inference;
    MicrophoneSource<int16_t,512> *microphone;
    PCMToFloat<int16_t,512,float,512> *pcm;
    VADLCD *lcd;
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

    CG_BEFORE_NODE_INIT;
    cg_status initError;

    nodes.display_clock = new (std::nothrow) VADDisplayClock<float,1>(*(fifos.fifo2),evtQueue,params->display_clock);
    if (nodes.display_clock==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.inference = new (std::nothrow) SileroInference<float,512,float,1>(*(fifos.fifo1),*(fifos.fifo2),params->inference);
    if (nodes.inference==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.microphone = new (std::nothrow) MicrophoneSource<int16_t,512>(*(fifos.fifo0),params->microphone);
    if (nodes.microphone==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.pcm = new (std::nothrow) PCMToFloat<int16_t,512,float,512>(*(fifos.fifo0),*(fifos.fifo1));
    if (nodes.pcm==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.lcd = new (std::nothrow) VADLCD(params->lcd);
    if (nodes.lcd==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    identifiedNodes[STREAM_APP_LCD_ID]=createStreamNode(*nodes.lcd);
    nodes.lcd->setID(STREAM_APP_LCD_ID);


/* Subscribe nodes for the event system*/
    nodes.display_clock->subscribe(0,*nodes.lcd,0);

    initError = CG_SUCCESS;
    initError = nodes.display_clock->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.inference->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.microphone->init();
    if (initError != CG_SUCCESS)
        return(initError);

    initError = nodes.pcm->init();
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

    if (nodes.display_clock!=NULL)
    {
        delete nodes.display_clock;
    }
    if (nodes.inference!=NULL)
    {
        delete nodes.inference;
    }
    if (nodes.microphone!=NULL)
    {
        delete nodes.microphone;
    }
    if (nodes.pcm!=NULL)
    {
        delete nodes.pcm;
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
        for(; id < 4; id++)
        {
            CG_BEFORE_NODE_EXECUTION(schedule[id]);
            switch(schedule[id])
            {
                case 0:
                {

                   cgStaticError = nodes.display_clock->run();
                }
                break;

                case 1:
                {

                   cgStaticError = nodes.inference->run();
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
