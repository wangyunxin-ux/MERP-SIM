//==========================================================================
//  CPARALLELSCHEDULER.H - part of
//                     OMNeT++/OMNEST
//            Discrete System Simulation in C++
//
//        Parallel Scheduler for Concurrent Event Execution
//
//==========================================================================

/*--------------------------------------------------------------*
  Copyright (C) 2003-2017 Andras Varga
  Copyright (C) 2006-2017 OpenSim Ltd.
  Copyright (C) 2025 [Your Name/Institution]

  This file is distributed WITHOUT ANY WARRANTY. See the file
  `license' for details on this and other legal matters.
*--------------------------------------------------------------*/

#ifndef __OMNETPP_CPARALLELSCHEDULER_H
#define __OMNETPP_CPARALLELSCHEDULER_H

#include "cscheduler.h"
#include "cparalleleventset.h"
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace omnetpp {

// 前向声明
class cParallelSimulation;

/**
 * @brief Parallel scheduler for concurrent event execution.
 *
 * cParallelScheduler extends cScheduler to support concurrent event execution
 * in a multi-threaded environment. It works in conjunction with cParallelEventSet
 * to determine which events can be executed in parallel.
 *
 * @ingroup SimCore
 */
class SIM_API cParallelScheduler : public cScheduler
{
  private:
    // 并行事件集
    cParallelEventSet *parallelFES;
    
    // 同步原语
    mutable std::mutex schedulerMutex;
    std::condition_variable eventAvailable;
    
    // 状态标志
    std::atomic<bool> simulationRunning;
    std::atomic<bool> waitingForEvents;
    
    // 统计信息
    std::atomic<long> eventsProcessed;
    
  public:
    /** @name Constructors, destructor */
    //@{
    /**
     * Constructor.
     */
    cParallelScheduler();
    
    /**
     * Destructor.
     */
    virtual ~cParallelScheduler();
    //@}
    
    /** @name Redefined cScheduler methods */
    //@{
    /**
     * Returns a description of the parallel scheduler.
     */
    virtual std::string str() const override;
    
    /**
     * Pass cSimulation object to scheduler and initialize parallel components.
     */
    virtual void setSimulation(cSimulation *_sim) override;
    
    /**
     * Called at the beginning of a simulation run.
     */
    virtual void startRun() override;
    
    /**
     * Called at the end of a simulation run.
     */
    virtual void endRun() override;
    
    /**
     * Returns the likely next event in the simulation.
     * For parallel simulation, this is an approximation.
     */
    virtual cEvent *guessNextEvent() override;
    
    /**
     * Returns the next event that can be executed concurrently.
     * This method may block if no events are currently available.
     */
    virtual cEvent *takeNextEvent() override;
    
    /**
     * Puts back an event into the event set.
     */
    virtual void putBackEvent(cEvent *event) override;
    //@}
    
    /** @name Parallel-specific methods */
    //@{
    /**
     * Returns the number of events processed so far.
     */
    virtual long getEventsProcessedCount() const;
    
    /**
     * Returns the number of events that can be executed concurrently.
     */
    virtual int getConcurrentEventCount() const;
    
    /**
     * Notifies the scheduler that new events may be available.
     */
    virtual void notifyEventAvailable();
    //@}
};

}  // namespace omnetpp

#endif