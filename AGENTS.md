# Goal

Goal of this project is to port and benchmark new networks
on Ethos NPU.

This project is current only targeting Alif E8 DevKit.

## Mandatory Repository guidelines

* This repository uses executorch. Network saved in .pt2 / .safetensor format are loaded from pytorch and use the executorch flow to be compiled by vela.

* Each application owns its generated `projects/<project>/ai_layer/`, referenced by its cproject.
Generate the layer when its model, quantization or Vela/storage settings change.
Switching projects does not require regeneration. Select the intended target set
when building or loading firmware for the board.

* Format of the solution must be respected : folder organization, purpose of each folder. The constraimts are detailed in the agents that need them.

* Nothing covered by GPL license must be included in this project. GPL use is forbidden.
  Network dual licensed can be used if the other license i not GPL.

* For open source reuse, copyright and licenses must be preserved. License must be compatible with Apache.

* Don't record any reference (in documentation, comments or scripts) to a local project outside of this one. Only use references to public project available on the web, local path pointing inside this project. If there is a the need to refer to an external project, the scripts should use a command line argument to receive the path to this project. A script must never hardcode any absolute path or use local paths that point outside of this project

* If you need to reference an external project for test or validation or quantization, check if it is possible to copy some files from the external project:
  * Only a few files is acceptable. We must not copy the full external project
  * The license of those files must be compatible with Apache
  * If it is not possible to copy the files because of size (too many files, or too big files)
    ask the user if you can download and install the external project somewhere outside of this project and then use command line arguments to give the path to this project where it is needed

* Demos using LCD uses the LCD in landscape mode

* Real-time audio demos must deliver generated audio to the audio output as soon
  as it is available, with generation and playback overlapping. Do not accumulate
  the entire utterance in a large intermediate audio buffer before playback.
  A small, bounded buffer is acceptable to absorb processing and scheduling
  variability; keep its duration reasonable and document the buffering latency.

* You are not allowed to modify files from imported libraries and packs. You can only modify files from this project.

* Don't use inline asm since it will make a port to Cortex-A more difficult
