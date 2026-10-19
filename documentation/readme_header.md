# Ethos Catalog

A collection of neural networks, benchmarks and demos for Arm Ethos NPUs.

The goal of the project is to identify networks that can run on a board using Ethos. 

As consequences:

* This project does not deliver networks quantized with state of the art methods
  * Networks are not made to be reused as they are in a product
  * The user interested in a network should check the accuracy on the full set of test patterns
  * The network may have to be reconverted and quantized with better methods
* This project does not try to optimize pre / post processing when what's available is enough to run the demo on board
* This project does not try to optimize the final CMSIS Stream graph that may contain more data copies than required in a better graph

Once a user has identified a network from this list, we expect that more time 
will be spent by the user on validating the conversion and quantization 
step before using the network in a product.

The demos use CMSIS Stream, whose POSIX and Zephyr runtimes provide a path to
porting them to Linux or Zephyr, including on Cortex-A processors. These ports
have not yet been implemented in this repository.

See the [build and usage guide](documentation/HOWTO.md) for setup instructions
and details on building, running and extending the projects.
