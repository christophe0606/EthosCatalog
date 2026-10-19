# Goal

Goal of this project is to port and benchmark new networks
on Ethos NPU.

This project is current only targeting Alif E8 DevKit.


## Mandatory Repository guidelines

* This repository uses executorch. Network saved in .pt2 / .safetensor format are loaded from pytorch and use the executorch flow to be compiled by vela.

* Only one project should be built at a given time because there is only one ai_layer that is generated for a specific project.
So building all projects is wrong since the same ai_layer would be used by all of them.

Only one project must be enabled when building the solution.

* Format of the solution must be respected : folder organization, purpose of each folder. The constraimts are detailed in the agents that need them.

* Nothing covered by GPL license must be included in this project. GPL use is forbidden.

* For open source reuse, copyright and licenses must be preserved. License must be compatible with Apache.

* Don't record any reference (in documentation, comments) to a local project outside of this one. Only use references to public project available on the web

* When user uses a local ML Eval Kit project, 
document the public references using https://gitlab.arm.com/artificial-intelligence/ethos-u/ml-embedded-evaluation-kit
or https://github.com/Arm-Examples/mlek-cmsis-pack-examples