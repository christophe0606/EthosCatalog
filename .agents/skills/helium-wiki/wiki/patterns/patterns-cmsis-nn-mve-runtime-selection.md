---
id: patterns-cmsis-nn-mve-runtime-selection
title: Select CMSIS-NN Helium kernels through the application runtime
type: patterns
architectures:
  - cortex-m55
tags:
  - helium
  - mve
  - compiler-target-configuration
  - c
confidence: source-reported
reproducibility: concept
related:
  - techniques-arm-compiler-mve-target-configuration
  - techniques-helium-auto-vectorization
  - techniques-profile-guided-helium-optimization
sources:
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/General_optimizations/basic_optimization_topics.md#L80-L107
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/ml-dsp-front-end-optimization-with-helium.md#L296-L352
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/ml-dsp-front-end-optimization-with-helium.md#L519-L544
  - https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf
aliases:
  - CMSIS-NN MVE runtime integration
  - TFLM CMSIS-NN optimized kernels
prerequisites:
  - Build the runtime and CMSIS-NN for the processor's implemented MVE and floating-point features.
  - Confirm that the runtime version supports the operators and kernel path required by the model.
---

Use this pattern when an ML application runs through a runtime such as TensorFlow Lite Micro and should select optimized CMSIS-NN kernels for a Helium-capable target. The [Helium optimization topics #1 - the basics article](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/General_optimizations/basic_optimization_topics.md) describes runtime integration as the normal path for NN operators; it does not imply that every custom DSP or frontend loop becomes vectorized when the library is enabled.

Build the runtime and CMSIS-NN with a target configuration that matches the fitted processor and the enabled MVE/FPU features. Review the runtime's operator resolver and library release to confirm that the model's required operators have an optimized implementation. The [Helium optimization topics #1 - the basics article](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/General_optimizations/basic_optimization_topics.md) gives TFLM's OPTIMIZED_KERNEL_DIR=cmsis_nn setting as an example; check the selected runtime version's build instructions before using that spelling as a build option.

After enabling the optimized kernel path, inspect the build configuration or generated call path to confirm that the runtime selected the intended implementation. Profile the full application again: library selection can change which custom frontend or post-processing loops are now worth optimizing. Keep those loops subject to compiler vectorization diagnostics and numerical validation.

## Match operators and deployment resources to the selected release

For each model operator, match its dimensions, element type, quantization, and tensor layout to an optimized CMSIS-NN implementation available in the selected library and runtime release. Include only the operators needed by the model in the resolver, inspect the generated or linked call path, and provide an explicit supported route for any operator without a matching implementation. Target flags alone do not establish that every operator is accelerated. The Arm Helium reference book describes a historical release snapshot, so verify current support from the selected release's documentation. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 223–224, 227](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Budget the complete deployment from actual configured allocations: model weights, input and output tensors, persistent recurrent state, intermediate activations, interpreter and resolver state, temporary kernel scratch, and sensor-front-end or output-postprocessing buffers. Determine peak use from the selected runtime and kernel requirements; do not size runtime overhead from a fixed allowance in the Arm Helium reference book. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 219, 224–225, 229–230](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Keep model tensor layout and channel order consistent with the selected kernel's documented dimensions and strides, and reorder weights when that API requires it. Verify the converted representation produces the reference model's outputs. The Arm Helium reference book's HWC convolution example keeps each pixel's channels contiguous for that access pattern; it is a case-specific layout choice, not a requirement for every NN operator or API. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 221, 227–228](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Distinguish legacy power-of-two Q-format paths from TFLM-compatible quantization. For the selected operator, account for activation zero points or input offsets, filter offsets, per-channel scales, bias representation, output offsets, fused activation bounds, and its requantization contract. Calibrate activation ranges from representative data, then compare intermediate and final outputs and task accuracy with the reference model; quantization does not guarantee unchanged accuracy. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 220, 223–224, 228–229](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Prefer an optimized activation or softmax helper only when its supported data type, approximation domain, scaling, rounding, and output range match the operator contract. Vector clamping can implement ReLU; lookup-table or polynomial approximations for nonlinear functions need a defined input domain and validated error. Check the implementation in the selected release rather than relying on historical helper names or incomplete examples. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 221–222](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

For pooling, preserve the selected window, stride, padding, and rounding semantics. A width-direction intermediate reduction can be reused by the height-direction pass when the kernel is structured that way. Permit in-place operation only when the selected API allows input destruction and no later consumer still needs those values; the Arm Helium reference book's example is not a universal aliasing guarantee. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 221, 229](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Keep sensor preprocessing consistent with the input tensor's layout and quantization, and convert model outputs according to the downstream interface. When conversion or kernel selection changes, compare intermediate layer outputs as well as final results against the reference model using representative inputs. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 224, 229](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)
