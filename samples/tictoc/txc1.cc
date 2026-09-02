//
// This file is part of an OMNeT++/OMNEST simulation example.
//
// Copyright (C) 2003 Ahmet Sekercioglu
// Copyright (C) 2003-2015 Andras Varga
//
// This file is distributed WITHOUT ANY WARRANTY. See the file
// `license' for details on this and other legal matters.
//

#include <string.h>
#include <omnetpp.h>
#include <atomic>
#include <mutex>
#include <bitset>

using namespace omnetpp;

/**
 * Derive the Txc1 class from cSimpleModule. In the Tictoc1 network,
 * both the `tic' and `toc' modules are Txc1 objects, created by OMNeT++
 * at the beginning of the simulation.
 */
class Txc1 : public cSimpleModule
{
  int i=0;
  int j=0;
  protected:
    // The following redefined virtual function holds the algorithm.
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
};

// The module class needs to be registered with OMNeT++
Define_Module(Txc1);

void Txc1::initialize()
{
    // Initialize is called at the beginning of the simulation.
    // To bootstrap the tic-toc-tic-toc process, one of the modules needs
    // to send the first message. Let this be `tic'.

    // Am I Tic or Toc?
    if (strcmp("tic", getName()) == 0) {
        // create and send first message on gate "out". "tictocMsg" is an
        // arbitrary string which will be the name of the message object.
        cMessage *msg = new cMessage("ticMsg");
//        cSimulation*sim=this->getSimulation();
        send(msg, "out");
//        cFutureEventSet*fes=this->getSimulation()->getFES();
//        std::cout<<fes->getLength()<<endl;
    }
    else{
        cMessage *msg = new cMessage("tocMsg");
        send(msg, "out");
    }
}

void Txc1::handleMessage(cMessage *msg)
{
//    cFutureEventSet*fes=this->getSimulation()->getFES();
    // The handleMessage() method is called whenever a message arrives
    // at the module. Here, we just send it to the other module, through
    // gate `out'. Because both `tic' and `toc' does the same, the message
    // will bounce between the two.
//    std::cout<<fes->getLength()<<endl;
//    double result = 0;
//        for (int i = 0; i < 10000; i++) {
//            result += sqrt(i) * sin(i) * cos(i);
//        }
//    std::cout<<std::bitset<32>(flags)<<endl;
//    static std::mutex sendMutex;  // 静态全局锁
//    {
//    std::lock_guard<std::mutex> lock(sendMutex);
    send(msg, "out"); // send out the message
//    }
//    if (strcmp("tic", getName()) == 0) {
//        i++;
//        std::cout<<"i="<<i<<endl;
//        }
//        else{
//            cMessage *msg = new cMessage("tocMsg");
//            j++;
//            std::cout<<"j="<<j<<endl;
//        }
//    std::cout<<fes->getLength()<<endl;
//    i++;
//    std::cout<<i<<endl;
}

