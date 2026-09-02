//=========================================================================
//  CEVENTCOLORER.CC - part of
//
//                  OMNeT++/OMNEST
//           Discrete System Simulation in C++
//
//   Simplified Event Colorer Implementation for Parallel Simulation
//
//=========================================================================

/*--------------------------------------------------------------*
  Copyright (C) 1992-2017 Andras Varga
  Copyright (C) 2006-2017 OpenSim Ltd.
  Copyright (C) 2025 [Your Name/Institution]

  This file is distributed WITHOUT ANY WARRANTY. See the file
  `license' for details on this and other legal matters.
*--------------------------------------------------------------*/

#include "omnetpp/ceventcolorer.h"
#include "omnetpp/cparalleleventset.h"
#include "omnetpp/csimulation.h"
#include "omnetpp/cmodule.h"
#include "omnetpp/cmessage.h"
#include "omnetpp/cevent.h"
#include "omnetpp/cenvir.h"
#include "omnetpp/cnamedobject.h"

#include <sstream>

namespace omnetpp {

Register_Class(cEventColorer);

cEventColorer::cEventColorer(const char *name, cSimulation *sim) : cNamedObject()
{
    setName(name);  // 使用 setName 方法设置名称
    fixedDelay = 0.1;
    simulation = sim;
    
    if (!simulation) {
        simulation = cSimulation::getActiveSimulation();
    }
}

cEventColorer::~cEventColorer()
{
    // 简化版本，无需清理复杂资源
}

cEventColorer *cEventColorer::dup() const
{
    return new cEventColorer(*this);
}

std::string cEventColorer::str() const
{
    std::stringstream out;
    out << "fixedDelay=" << fixedDelay;
    return out.str();
}

void cEventColorer::setFixedDelay(simtime_t delay)
{
    fixedDelay = delay;
}

simtime_t cEventColorer::getFixedDelay() const
{
    return fixedDelay;
}

EventColor cEventColorer::determineEventColor(cEvent *event)
{
    if (!event) return EventColor::EVENT_RED;
    
    // 简化实现：暂时将所有事件标记为绿色（可并行执行）
    // 在实际实现中，应该根据事件之间的依赖关系来确定颜色
    return EventColor::EVENT_GREEN;
}

simtime_t cEventColorer::calculateEarliestEffectTime(cEvent *event)
{
    if (!event) return SIMTIME_ZERO;
    
    // 简化实现：返回事件到达时间加上固定延迟
    return event->getArrivalTime() + fixedDelay;
}

void cEventColorer::recolorAllEvents(cParallelEventSet *eventSet)
{
    if (!eventSet) return;
    
    // 简化实现：将所有事件标记为绿色
    int eventCount = eventSet->getLength();
    for (int i = 0; i < eventCount; i++) {
        cEvent *event = eventSet->get(i);
        if (event) {
            simtime_t earliestEffectTime = calculateEarliestEffectTime(event);
            eventSet->setEventColor(event, EventColor::EVENT_GREEN, earliestEffectTime);
        }
    }
}

void cEventColorer::recolorEvent(cEvent *event, cParallelEventSet *eventSet)
{
    if (!event || !eventSet) return;
    
    // 简化实现：将单个事件标记为绿色
    simtime_t earliestEffectTime = calculateEarliestEffectTime(event);
    eventSet->setEventColor(event, EventColor::EVENT_GREEN, earliestEffectTime);
}

simtime_t cEventColorer::getShortestDelay(cModule *from, cModule *to) const
{
    // 简化实现：总是返回固定延迟
    // 在实际实现中，应该计算两个模块之间的实际最短延迟
    return fixedDelay;
}

simtime_t cEventColorer::getMinDelayToModule(cModule *to) const
{
    // 简化实现：返回固定延迟
    return fixedDelay;
}

simtime_t cEventColorer::getMinDelayFromModule(cModule *from) const
{
    // 简化实现：返回固定延迟
    return fixedDelay;
}

}  // namespace omnetpp