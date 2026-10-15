---
name: cmsis-stream-graph
description: This skill must be used to create new graphs for CMSIS Stream.
---

# CMSIS Stream graph

A CMSIS Stream graph is a collection of interconnected nodes that define the flow of data and events within a system. It allows users to visually design and manage the relationships between different components, facilitating the creation of complex processing pipelines.

The graph is defined in Python and the python script when run will generate C++ source files and headers that must be built into the project.

## Generated C++ Source Files and Headers

C++ and C files rely on the files provided by the CMSIS Stream pack.
The CMSIS Stream pack provides the runtime for CMSIS RTOS and some files, in RTE folder for the project, that must be customized by an application developer to fit the specific needs of their project.

Each project, will include those customizable files in the RTE folder for the project.

If all the projects of a solution share the same CMSIS Stream pack, the customizable files in the RTE folder should be consistent across all projects to ensure proper integration and functionality.
In that case, RTE CMSIS Stream files should include files defined in a common folder and shared by all project to ensure consistency.

Each project would still provide the required customization but
not in the RTE folder but in the project-specific configuration files or directories.

There is a lot of flexibility in how CMSIS Stream can be configured so in general some guidelines and constraint are to be defined by the user.

This skill is defining reasonable consraints and organization that will apply to all projects of the solution.

## Python description

The python descriptino is a file in the project folder.
It may be called either from the project folder or the root folder.
It may references python packages defined in common/nodes

The CMSIS Stream configuration in the file must be:

```python
conf = Configuration()
    conf.CMSISDSP = False
    conf.asynchronous = False
    conf.horizontal = True
    conf.nodeIdentification = True
    conf.schedName = "scheduler_app"
    conf.schedulerCFileName = "scheduler_app"
    conf.memoryOptimization = True
    conf.appConfigCName = "app_config.hpp"
    conf.appNodesCName = "AppNodes.hpp"
    conf.cOptionalInitArgs = [f"AppParams *params"]
    conf.prefix = "stream_app_"
```
Since it is common to all project, this part could be factored in a common python package and the project would just define the
project specific parts.

The graph creation part could pack all of the creation in an `mp_app` python function that could also take as additional argument global defines for the project:

```python
mk_app(
    the_graph,
    globals={
        "MIC_BLOCK_SIZE": NB_AUDIO_SAMPLES * Q15_SCALAR.bytes * mic_channels,
        "MIC_SAMPLE_RATE": mic_sample_rate,
        "MIC_CHANNELS": mic_channels,
        "MIC_FRAMES_PER_BUFFER": mic_frames_per_buffer,
        "MIC_SAMPLE_SIZE" :  Q15_SCALAR.bytes * 8,
        "CONFIG_NB_BINS" : 80,
    },
    config=config
)
```

## AppNodes.hpp

The files `AppNodes.hpp` is generated in the project from the list of nodes in the graph after the `computeSchedule` step.

As a convention, we assume that if a node object as a python property `folder` then it is a node provided by this solution and such should be included in `AppNodes.hpp`
If the node does not have this property, then it is a default
CMSIS Stream node and does not need to be included in `AppNodes.h`

## app_config.hpp

Defines some customizations of the generated stream scheduler. It is included by the generated C++ scheduler.

This is not a configuration of the project but of the stream scheduler.
Configuration of the project should be kept in the generated `app_params.h` file
and controlled by the Python describing the graph.

It defines the buffer alignment required by FIFO in memory optimization mode

```c
#define CG_BEFORE_BUFFER __ALIGNED(16) __attribute__((section(".bss.stream_fifo")))

```
It is MANDATORY to align when memory optimization has been activated in python when generatign the graph.

The file also defines events and a way to catch in the scheduler pause / stop events:

```c
#pragma once 

/*
 * Application configuration for a CMSIS-Stream scheduler.
 *
 * Generated schedulers include this copied configuration header before
 * stream_platform_config.hpp. Use it for scheduler-specific application hooks.
 * Put shared CMSIS-Stream runtime overrides in stream_runtime_config.hpp; those
 * overrides are included by stream_platform_config.hpp.
 *
 * CG_BEFORE_BUFFER is used when the generated scheduler uses memory optimization
 * settings with memory sharing between different FIFOs. In this case, the buffers 
 * must be aligned.
 *
 * CG_BEFORE_NODE_EXECUTION is called by the generated scheduler before each
 * node execution. The default hook connects the scheduler to the CMSIS-RTOS
 * runtime so pause and stop requests can interrupt the graph cleanly. The hook
 * can also be extended with application tracing or diagnostics.
 */

extern "C"
{
#include "RTE_Components.h"
#include CMSIS_device_header

#include "cmsis_os2.h" /* CMSIS-RTOS2 API */


}

#include "cg_enums.h"
#include "stream_rtos_events.h"

#include "arm_math_types.h"

extern "C"
{
    #include "datatypes.h"
    #include "app_params.h"
}

extern osEventFlagsId_t cg_streamEvent;

// FIFO buffers are placed in a dedicated section so the linker can apply the
// memory layout selected for the generated graph. 
// When memory optimization setting is used in the Python generator, the memory
// for FIFOs is shared and the buffers must be aligned to avoid unaligned accesses.
#define CG_BEFORE_BUFFER __ALIGNED(16) __attribute__((section(".bss.stream_fifo")))

#define CG_BEFORE_NODE_EXECUTION(id)                                                 \
    {                                                                                \
        uint32_t res =                                                               \
            osEventFlagsWait(cg_streamEvent, STREAM_PAUSE_EVENT | STREAM_DONE_EVENT, \
                             osFlagsWaitAny, 0);                                     \
        if (!(res & 0x80000000))                                                     \
        {                                                                            \
            if ((res & STREAM_DONE_EVENT) != 0)                                      \
            {                                                                        \
                cgStaticError = CG_STOP_SCHEDULER;                                   \
                goto errorHandling;                                                  \
            }                                                                        \
            if ((res & STREAM_PAUSE_EVENT) != 0)                                     \
            {                                                                        \
                cgStaticError = CG_PAUSED_SCHEDULER;                                 \
                goto errorHandling;                                                  \
            }                                                                        \
        }                                                                            \
    }

```


## app_params.hpp
The `app_params.hpp` must define the application-specific configuration parameters and settings required by the CMSIS Stream application. 

It is generally automatically generated when the CMSIS Stream graph is processed in python.The creation script must create this file so that it is always consistent with the network.

It generally starts with 

```cpp

extern "C"
{
#include "RTE_Components.h"
#include CMSIS_device_header

#include "cmsis_os2.h" /* CMSIS-RTOS2 API */


}

#include "cg_enums.h"
#include "stream_rtos_events.h"

#include "arm_math_types.h"

extern "C"
{
    #include "datatypes.h"
    #include "app_params.h"
}
```

Where `datatype.h` define datatypes used in the graph and application.



We assume that each python node defines in the constructor something like

```python
self.params = {"gain": float(gain)}
```

Then a corresponding C struct is generated in `app_params.h`
```c
typedef struct {
    float gain;
} GainParams;
```
If the python nodes has a needs hardware property:

```python
@property
    def needsHardware(self):
        return True
```

Then the struct would include a field indicating the hardware requirement, for example:

```c
typedef struct {
    HardwareParams hw_;
    int32_t num_channels;
} MicrophoneSourceParams;
```

The hardware param field is always named `hw_`

So, an example could be:
```c
// This file is automatically generated by app.py. Do not edit it manually.
#ifndef APP_PARAMS_H
#define APP_PARAMS_H

#include <stdint.h>
#include "hardware_params.h"

#define MIC_BLOCK_SIZE 1280
#define MIC_SAMPLE_RATE 16000
#define MIC_CHANNELS 2
#define MIC_FRAMES_PER_BUFFER 0
#define MIC_SAMPLE_SIZE 16
#define CONFIG_NB_BINS 80

typedef struct {
    float gain;
} GainParams;

typedef struct {
    HardwareParams hw_;
    int32_t num_channels;
} MicrophoneSourceParams;

typedef struct {
    GainParams gain;
    MicrophoneSourceParams src;
} AppParams;

extern AppParams appParams;
void app_params_set_hardware(const HardwareParams *params);

#endif

```
Hardware includes peripheral like micro, LCD, speaker etc ...
But also consider that use of a network is HW.
So this structure should contain a pointer to the embedded module
and the graph node should only use this embedded module.

## app_params.c

Create a nested struct to initialize all parameters of the demo except
hardware ones that are initialized separately from code.
Example:

```c
// This file is automatically generated by app.py. Do not edit it manually.
#include "app_params.h"
#include <math.h>

AppParams appParams = {
    .gain = {
        .gain = 2.0f,
    },
    .src = {
        .num_channels = 2,
    }
};

void app_params_set_hardware(const HardwareParams *params)
{
    if (params == 0) {
        return;
    }
    appParams.src.hw_ = *params;
}

```
## hardware_params.h

Define the interface to initialize the hardware on a platform.
It is project specific because we do not want to enable all hardware peripherals.

The api is
```c
#ifndef HARDWARE_PARAMS_H
#define HARDWARE_PARAMS_H

#include <stdint.h>
#include <stddef.h>

#define HARDWARE_MICROPHONE_FRAME_EVENT (1U << 0)
#define HARDWARE_MICROPHONE_OVERFLOW_EVENT (1U << 1)

typedef struct {
    void *microphone_stream;
    void *microphone_event;
    int32_t microphone_sample_rate;
    int32_t microphone_num_channels;
    const uint8_t *model_weights;
    size_t model_size;
} HardwareParams;

int hardware_params_init(HardwareParams *params);
void hardware_params_uninit(HardwareParams *params);
int hardware_audio_pause(HardwareParams *params);
int hardware_audio_resume(HardwareParams *params);

#endif /* HARDWARE_PARAMS_H */
```

The `HardwareParams` is project specific

The API to init and uninit are always present.

Other API may be node specific. For instance, with an audio node implementing the context swwitch protocol to switch between two graph, there may be the need to pause / resume the audio.


## hardware_params.c

It implements the interface and initialize the hardware.
It can also (by relying on a cpp file_) get a pointer to an embedded module to initialize a field of
this hardware structure. The reason why the embedded module is in the hw_ field is because the module mauy contain several network used by different nodes.
So it is an initialization that cannot be done in a specific node.

# RTE files

RTE includes files that provides the customization of the CMSIS Stream runtime

Those files are

## stream_runtime_config.hpp

This can be copied from the RTE. It contain configuration values (pool size, priorities etc ...)
Default values are generally ok.

## cstream_node.h

Interface provided by the node to the external world.
RTE default is generally ok and can be copied as it is.
It provides `StreamNodeInterface` and `ContextSwitchInterface`

```c
struct CStreamNode
{
    void *obj;
    const struct StreamNodeInterface *stream_intf;
    const struct ContextSwitchInterface *context_switch_intf;
};

Note that if a project needs more API, then the project will need a custom implementation.
We want to share this file as much as possible between all projects to avoid duplicating when implementation is the same but for some project we may need new API.

Note that StreamNodeInterface and ContextSwitchInterface are always available in all nodes that expose an API to outside world.
```

## IdentifiedNode.hpp

It implements the interfaces defined in `cstream_node.h`

## stream_init.hpp

Provides API to initialize the stream runtime and set up the necessary resources for the CMSIS Stream graph.

## stream_init.cpp

Implement the stream runtime initialization.
This generally requires customizations because:
* It implements the pause_scheduler_app and resume_scheduler_app that rely on the CStreamNode class that can be redefined in different projects.
* It initializes the stream_execution_context_t that contain pointer to the scheduler API and this API may change between different project
* It needs to cast some function pointers to match the expected types in the stream_execution_context_t structure and this is project dependent

To summarize, the main issue is that some API of the generated scheduler and soem datatype are project specific. So the initialization code cannot be generic because it must works with different type because the API is strongly typed.

If the API of CMSIS Stream had been based on void and void*, the file could have been genereic.

The CMSIS scheduler receives the pointer to a nested struct to configure all settings of the node so they have different type in different project so they need a different stream_init.cpp implementation.