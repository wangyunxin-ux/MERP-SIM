//=========================================================================
//  CPARALLELSCHEDULER.CC - part of
//
//                  OMNeT++/OMNEST
//           Discrete System Simulation in C++
//
//   Parallel Scheduler Implementation for Concurrent Event Execution
//
//=========================================================================

/*--------------------------------------------------------------*
  Copyright (C) 1992-2017 Andras Varga
  Copyright (C) 2006-2017 OpenSim Ltd.
  Copyright (C) 2025 [Your Name/Institution]

  This file is distributed WITHOUT ANY WARRANTY. See the file
  `license' for details on this and other legal matters.
*--------------------------------------------------------------*/

#include "omnetpp/cparallelscheduler.h"
#include "omnetpp/cparallelsimulation.h"
#include "omnetpp/csimulation.h"
#include "omnetpp/cevent.h"
#include "omnetpp/cenvir.h"
#include "omnetpp/cfutureeventset.h"
#include "omnetpp/globals.h"
#include "omnetpp/cconfiguration.h"
#include "omnetpp/cconfigoption.h"
#include "omnetpp/platdep/platmisc.h"

namespace omnetpp {

Register_Class(cParallelScheduler);

cParallelScheduler::cParallelScheduler() : 
    parallelFES(nullptr),
    simulationRunning(false),
    waitingForEvents(false),
    eventsProcessed(0)
{
    // 创建并行事件集
    parallelFES = new cParallelEventSet("parallel-fes");
}

cParallelScheduler::~cParallelScheduler()
{
    // 清理资源
    if (parallelFES) {
        delete parallelFES;
        parallelFES = nullptr;
    }
}

std::string cParallelScheduler::str() const
{
    std::stringstream out;
    out << "Parallel Scheduler, events=" << eventsProcessed.load() 
        << ", concurrent=" << (parallelFES ? parallelFES->getConcurrentEventCount() : 0);
    return out.str();
}

void cParallelScheduler::setSimulation(cSimulation *_sim)
{
    // 调用基类实现
    cScheduler::setSimulation(_sim);
    
    // 设置并行事件集的仿真对象
    if (parallelFES && _sim) {
        // 如果使用的是cParallelSimulation，可以设置更多参数
        cParallelSimulation *parallelSim = dynamic_cast<cParallelSimulation*>(_sim);
        if (parallelSim) {
            // 可以在这里设置事件着色器等
        }
    }
}

void cParallelScheduler::startRun()
{
    // 调用基类实现
    cScheduler::startRun();
    
    // 重置状态
    simulationRunning = true;
    waitingForEvents = false;
    eventsProcessed = 0;
    
    // 清空事件集
    if (parallelFES) {
        parallelFES->clear();
    }
}

void cParallelScheduler::endRun()
{
    // 停止仿真
    simulationRunning = false;
    
    // 通知可能正在等待的线程
    eventAvailable.notify_all();
    
    // 调用基类实现
    cScheduler::endRun();
}

cEvent *cParallelScheduler::guessNextEvent()
{
    if (!parallelFES || !sim) {
        return nullptr;
    }
    
    // 返回事件集中的第一个事件（近似值）
    std::lock_guard<std::mutex> lock(schedulerMutex);
    return parallelFES->peekFirst();
}

cEvent *cParallelScheduler::takeNextEvent()
{
    if (!parallelFES || !sim) {
        return nullptr;
    }
    
    std::unique_lock<std::mutex> lock(schedulerMutex);
    
    // 等待有可用事件或仿真结束
    waitingForEvents = true;
    eventAvailable.wait(lock, [this] {
        return !simulationRunning || !parallelFES->isEmpty();
    });
    waitingForEvents = false;
    
    if (!simulationRunning) {
        return nullptr;
    }
    
    // 获取可并发执行的事件
    cEvent *event = parallelFES->takeNextConcurrentEvent();
    if (event) {
        eventsProcessed++;
    }
    
    return event;
}

void cParallelScheduler::putBackEvent(cEvent *event)
{
    if (!parallelFES || !event) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(schedulerMutex);
    
    // 将事件放回事件集
    parallelFES->putBackFirst(event);
    
    // 通知可能正在等待的线程
    eventAvailable.notify_one();
}

long cParallelScheduler::getEventsProcessedCount() const
{
    return eventsProcessed.load();
}

int cParallelScheduler::getConcurrentEventCount() const
{
    if (!parallelFES) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(schedulerMutex);
    return parallelFES->getConcurrentEventCount();
}

void cParallelScheduler::notifyEventAvailable()
{
    // 通知可能正在等待的线程
    eventAvailable.notify_one();
}

}  // namespace omnetpp