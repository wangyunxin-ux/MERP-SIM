MERP-SIM

MERP-SIM is an experimental parallel discrete-event simulation extension based on OMNeT++ 6.0. Its goal is to explore multi-threaded event scheduling and concurrent event execution while preserving the existing modeling framework and runtime mechanisms of OMNeT++.

Main Modifications and Contributions

This project implements and integrates the following features into OMNeT++ 6.0:

Added cParallelSimulation, which uses a thread pool to dispatch and execute events that can be processed in parallel.

Added cParallelScheduler and cParallelEventSet to support concurrent event scheduling, event queue access, and synchronization of task completion.

Added cEventColorer, which combines module connectivity information and propagation-delay analysis to assist in determining whether events can be executed concurrently.

Introduced atomic counters, thread-local simulation contexts, and necessary mutex protection into the simulation kernel to improve the thread safety of shared states, including event numbering, object ownership, signal handling, parameter configuration, and string pools.

Integrated the runtime environment with the parallel simulation implementation, and updated the simulation kernel build configuration and public header interfaces accordingly.

These modifications are currently intended for research on multi-threaded and parallel simulation mechanisms and should be regarded as an experimental implementation. Before using MERP-SIM with a specific model, it is recommended to carefully validate event causality, determinism, and thread safety.

Upstream Project

MERP-SIM is based on OMNeT++ 6.0, developed by OpenSim Ltd.

Original project: https://github.com/omnetpp/omnetpp
