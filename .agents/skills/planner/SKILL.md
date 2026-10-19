---
name: planner
description: Use this skill when the user wants to port a new network
---

The skill describes what to do when the user ask for the porting of a new network.

# Plan of action 


When the user asks to port a new network and gives some URLs as source you should go:
1 - Launch the analyze agent. If the agents confirm that the network can be ported (or it is likely to be ported) you can proceed to the next step
2 - Create a .flow folder in the root project folder if none exists. 
In the .flow folder, create a new subfolder that will be used for the work and used to save markdown reflecting the state of the work, remaining work to do, problems found. The path to this folder will be communicated to each agent at each step
After each step, save a CHECKPOINT.md in this folder that describes where we are, what remains to be done, what is the long term goal.
3 - Create a project folder based on one of the template `remplate_mram` or `template_ospi` depending on the memory analysis done by the analyze agent.
4 - Launch the converter agent
5 - Once the conversion is successful, launch the benchmarking agent
6 - Once the network has been benchmarked, give the inference time to the coding agent to start the coding. Inference time is needed to make some coding choices.
The coding agent may demonstrate that we do not have enough memory to run both the network and the software on the given board. Then the coding process should stop and the problem be documented clearly.
The project will not be added to the catalog if it cannot be successfully ported due to memory constraints. But we will keep the code and add a CHECKPOINT.md also in the project folder to be able to resume the coding process on another board with more memory.
7 - Launch the review agent with the last checkpoint file
If the review agent finds no problems, consider the goal is reached. Otherwise, according to the problems found, you may have to restart the coding step with the feedback from the review.
Run the final release version of the project on the board and let it run.

If the step converter or the step benchmarks are failing then we consider that the network cannot be ported to Ethos and the reason should be clearly documented in the project folder.


