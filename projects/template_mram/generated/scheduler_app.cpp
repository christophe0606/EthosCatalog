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
static uint8_t schedule[9]=
{ 
4,5,2,7,0,8,1,3,6,
};

/*

Internal ID identification for the nodes

*/
#define DC_INTERNAL_ID 0
#define FFT_INTERNAL_ID 1
#define GAIN_INTERNAL_ID 2
#define MAGNITUDE_INTERNAL_ID 3
#define MICROPHONE_INTERNAL_ID 4
#define PCM_INTERNAL_ID 5
#define SEND_INTERNAL_ID 6
#define SLIDING_INTERNAL_ID 7
#define WINDOW_INTERNAL_ID 8
#define LCD_INTERNAL_ID 9

/* Initialize the selectors global IDs in each class */
template<>
std::array<uint16_t,1> SendToLCD<SpectrumBlock,1>::selectors = {SEL_SPECTRUM_ID};
std::array<uint16_t,1> LCD::selectors = {SEL_SPECTRUM_ID};


/***********

Node identification

************/
static CStreamNode identifiedNodes[STREAM_APP_NB_IDENTIFIED_NODES]={0};

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

#define BUFFERSIZE0 8208
CG_BEFORE_BUFFER
uint8_t stream_app_buf0[BUFFERSIZE0]={0};

#define BUFFERSIZE1 8208
CG_BEFORE_BUFFER
uint8_t stream_app_buf1[BUFFERSIZE1]={0};


typedef struct {
FIFO<PcmBlock,FIFOSIZE0,1,0> *fifo0;
FIFO<AudioBlock,FIFOSIZE1,1,0> *fifo1;
FIFO<AudioBlock,FIFOSIZE2,1,0> *fifo2;
FIFO<FFTBlock,FIFOSIZE3,1,0> *fifo3;
FIFO<FFTBlock,FIFOSIZE4,1,0> *fifo4;
FIFO<FFTBlock,FIFOSIZE5,1,0> *fifo5;
FIFO<FFTBlock,FIFOSIZE6,1,0> *fifo6;
FIFO<SpectrumBlock,FIFOSIZE7,1,0> *fifo7;
} fifos_t;

typedef struct {
    RemoveDC<FFTBlock,1,FFTBlock,1> *dc;
    FFT<FFTBlock,1,FFTBlock,1> *fft;
    EmbeddedGain<AudioBlock,1,AudioBlock,1> *gain;
    Magnitude<FFTBlock,1,SpectrumBlock,1> *magnitude;
    MicrophoneSource<PcmBlock,1> *microphone;
    PCMToFloat<PcmBlock,1,AudioBlock,1> *pcm;
    SendToLCD<SpectrumBlock,1> *send;
    SlidingBuffer<AudioBlock,1,FFTBlock,1> *sliding;
    HannWindow<FFTBlock,1,FFTBlock,1> *window;
    LCD *lcd;
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
    fifos.fifo0 = new (std::nothrow) FIFO<PcmBlock,FIFOSIZE0,1,0>(stream_app_buf1);
    if (fifos.fifo0==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo1 = new (std::nothrow) FIFO<AudioBlock,FIFOSIZE1,1,0>(stream_app_buf0);
    if (fifos.fifo1==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo2 = new (std::nothrow) FIFO<AudioBlock,FIFOSIZE2,1,0>(stream_app_buf1);
    if (fifos.fifo2==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo3 = new (std::nothrow) FIFO<FFTBlock,FIFOSIZE3,1,0>(stream_app_buf0);
    if (fifos.fifo3==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo4 = new (std::nothrow) FIFO<FFTBlock,FIFOSIZE4,1,0>(stream_app_buf1);
    if (fifos.fifo4==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo5 = new (std::nothrow) FIFO<FFTBlock,FIFOSIZE5,1,0>(stream_app_buf0);
    if (fifos.fifo5==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo6 = new (std::nothrow) FIFO<FFTBlock,FIFOSIZE6,1,0>(stream_app_buf1);
    if (fifos.fifo6==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    fifos.fifo7 = new (std::nothrow) FIFO<SpectrumBlock,FIFOSIZE7,1,0>(stream_app_buf0);
    if (fifos.fifo7==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    CG_BEFORE_NODE_INIT;
    cg_status initError;

    nodes.dc = new (std::nothrow) RemoveDC<FFTBlock,1,FFTBlock,1>(*(fifos.fifo3),*(fifos.fifo4));
    if (nodes.dc==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.fft = new (std::nothrow) FFT<FFTBlock,1,FFTBlock,1>(*(fifos.fifo5),*(fifos.fifo6));
    if (nodes.fft==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.gain = new (std::nothrow) EmbeddedGain<AudioBlock,1,AudioBlock,1>(*(fifos.fifo1),*(fifos.fifo2),params->gain);
    if (nodes.gain==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.magnitude = new (std::nothrow) Magnitude<FFTBlock,1,SpectrumBlock,1>(*(fifos.fifo6),*(fifos.fifo7));
    if (nodes.magnitude==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.microphone = new (std::nothrow) MicrophoneSource<PcmBlock,1>(*(fifos.fifo0),params->microphone);
    if (nodes.microphone==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.pcm = new (std::nothrow) PCMToFloat<PcmBlock,1,AudioBlock,1>(*(fifos.fifo0),*(fifos.fifo1));
    if (nodes.pcm==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.send = new (std::nothrow) SendToLCD<SpectrumBlock,1>(*(fifos.fifo7),evtQueue,params->send);
    if (nodes.send==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.sliding = new (std::nothrow) SlidingBuffer<AudioBlock,1,FFTBlock,1>(*(fifos.fifo2),*(fifos.fifo3));
    if (nodes.sliding==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.window = new (std::nothrow) HannWindow<FFTBlock,1,FFTBlock,1>(*(fifos.fifo4),*(fifos.fifo5));
    if (nodes.window==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }

    nodes.lcd = new (std::nothrow) LCD(params->lcd);
    if (nodes.lcd==NULL)
    {
        return(CG_MEMORY_ALLOCATION_FAILURE);
    }
    identifiedNodes[STREAM_APP_LCD_ID]=createStreamNode(*nodes.lcd);
    nodes.lcd->setID(STREAM_APP_LCD_ID);


/* Subscribe nodes for the event system*/
    nodes.send->subscribe(0,*nodes.lcd,0);

    initError = CG_SUCCESS;
    initError = nodes.dc->init();
    if (initError != CG_SUCCESS)
        return(initError);
    
    initError = nodes.fft->init();
    if (initError != CG_SUCCESS)
        return(initError);
    
    initError = nodes.gain->init();
    if (initError != CG_SUCCESS)
        return(initError);
    
    initError = nodes.magnitude->init();
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
    
    initError = nodes.sliding->init();
    if (initError != CG_SUCCESS)
        return(initError);
    
    initError = nodes.window->init();
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

    if (nodes.dc!=NULL)
    {
        delete nodes.dc;
    }
    if (nodes.fft!=NULL)
    {
        delete nodes.fft;
    }
    if (nodes.gain!=NULL)
    {
        delete nodes.gain;
    }
    if (nodes.magnitude!=NULL)
    {
        delete nodes.magnitude;
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
    if (nodes.sliding!=NULL)
    {
        delete nodes.sliding;
    }
    if (nodes.window!=NULL)
    {
        delete nodes.window;
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
        for(; id < 9; id++)
        {
            CG_BEFORE_NODE_EXECUTION(schedule[id]);
            switch(schedule[id])
            {
                case 0:
                {
                    
                   cgStaticError = nodes.dc->run();
                }
                break;

                case 1:
                {
                    
                   cgStaticError = nodes.fft->run();
                }
                break;

                case 2:
                {
                    
                   cgStaticError = nodes.gain->run();
                }
                break;

                case 3:
                {
                    
                   cgStaticError = nodes.magnitude->run();
                }
                break;

                case 4:
                {
                    
                   cgStaticError = nodes.microphone->run();
                }
                break;

                case 5:
                {
                    
                   cgStaticError = nodes.pcm->run();
                }
                break;

                case 6:
                {
                    
                   cgStaticError = nodes.send->run();
                }
                break;

                case 7:
                {
                    
                   cgStaticError = nodes.sliding->run();
                }
                break;

                case 8:
                {
                    
                   cgStaticError = nodes.window->run();
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
