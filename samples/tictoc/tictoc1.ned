//
// This file is part of an OMNeT++/OMNEST simulation example.
//
// Copyright (C) 2003 Ahmet Sekercioglu
// Copyright (C) 2003-2015 Andras Varga
//
// This file is distributed WITHOUT ANY WARRANTY. See the file
// `license' for details on this and other legal matters.
//

import Psimulation;
simple Txc1
{
    gates:
        input in;
        output out;
}



//
// Two instances (tic and toc) of Txc1 connected both ways.
// Tic and toc will pass messages to one another.
//
network Tictoc1
{
    submodules:
        tic: Txc1;
        toc: Txc1;
        psimulation:Psimulation;
    connections:
        tic.out --> {  delay = 10ms; } --> toc.in;
        tic.in <-- {  delay = 10ms; } <-- toc.out;
}

