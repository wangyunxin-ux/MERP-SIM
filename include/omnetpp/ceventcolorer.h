//==========================================================================
//  CEVENTCOLORER.H - part of
//                     OMNeT++/OMNEST
//            Discrete System Simulation in C++
//
//        Simplified Event Colorer for Parallel Simulation
//
//==========================================================================

/*--------------------------------------------------------------*
  Copyright (C) 1992-2017 Andras Varga
  Copyright (C) 2006-2017 OpenSim Ltd.
  Copyright (C) 2025 [Your Name/Institution]

  This file is distributed WITHOUT ANY WARRANTY. See the file
  `license' for details on this and other legal matters.
*--------------------------------------------------------------*/

#ifndef __OMNETPP_CEVENTCOLORER_H
#define __OMNETPP_CEVENTCOLORER_H

#include "omnetpp/cobject.h"
#include "omnetpp/csimulation.h"
#include "omnetpp/cmodule.h"
#include "omnetpp/cevent.h"
#include "omnetpp/cmessage.h"
#include "omnetpp/cparalleleventset.h"

namespace omnetpp {



/**
 * @brief Event color types for parallel simulation.
 */


/**
 * @brief Simplified event colorer for parallel simulation.
 *
 * This is a simplified implementation that assumes a fixed 0.1s delay
 * between all modules for initial testing.
 *
 * @ingroup SimCore
 */
class SIM_API cEventColorer : public cNamedObject
{
  private:
    // 配置
    simtime_t fixedDelay;  // 固定延迟值
    
    // 相关组件
    cSimulation *simulation;
    
  public:
    /** @name Constructors, destructor */
    //@{
    /**
     * Constructor.
     */
    cEventColorer(const char *name=nullptr, cSimulation *sim=nullptr);
    
    /**
     * Destructor.
     */
    virtual ~cEventColorer();
    //@}
    
    /** @name Configuration methods */
    //@{
    /**
     * Sets the fixed delay between modules.
     */
    void setFixedDelay(simtime_t delay);
    
    /**
     * Returns the fixed delay between modules.
     */
    simtime_t getFixedDelay() const;
    //@}
    
    /** @name Event coloring methods */
    //@{
    /**
     * Determines the color of an event based on dependencies.
     */
    virtual EventColor determineEventColor(cEvent *event);
    
    /**
     * Calculates the earliest time this event can affect other events.
     */
    virtual simtime_t calculateEarliestEffectTime(cEvent *event);
    
    /**
     * Recalculates all event colors in the given event set.
     */
    virtual void recolorAllEvents(cParallelEventSet *eventSet);
    
    /**
     * Recalculates the color of a specific event.
     */
    virtual void recolorEvent(cEvent *event, cParallelEventSet *eventSet);
    //@}
    
    /** @name Module analysis methods */
    //@{
    /**
     * Returns the shortest delay between two modules.
     * Simplified: always returns fixed delay.
     */
    virtual simtime_t getShortestDelay(cModule *from, cModule *to) const;
    
    /**
     * Returns the minimum delay from any module to the given module.
     */
    virtual simtime_t getMinDelayToModule(cModule *to) const;
    
    /**
     * Returns the minimum delay from the given module to any module.
     */
    virtual simtime_t getMinDelayFromModule(cModule *from) const;
    //@}
    
    /** @name Redefined cObject methods */
    //@{
    /**
     * Creates and returns an exact copy of this object.
     */
    virtual cEventColorer *dup() const override;
    
    /**
     * Produces a one-line description of the object's contents.
     */
    virtual std::string str() const override;
    //@}
};

}  // namespace omnetpp

#endif