Put node implementations that require Helium intrinsics here. The current
portable CMSIS-DSP wrappers in `../c/` use the selected library implementation,
including its MVE acceleration on Cortex-M55, without requiring an alternative
node API or an implementation that cannot run on other supported cores.
