//=========================================================================
//  CPARALLELEVENTSET.CC - part of
//
//                  OMNeT++/OMNEST
//           Discrete System Simulation in C++
//
//   Parallel Event Set Implementation
//
//=========================================================================

/*--------------------------------------------------------------*
  Copyright (C) 1992-2017 Andras Varga
  Copyright (C) 2006-2017 OpenSim Ltd.
  Copyright (C) 2025 [Your Name/Institution]

  This file is distributed WITHOUT ANY WARRANTY. See the file
  `license' for details on this and other legal matters.
*--------------------------------------------------------------*/

#include "omnetpp/cparalleleventset.h"
#include "omnetpp/csimulation.h"
#include "omnetpp/cenvir.h"
#include "omnetpp/cmodule.h"
#include "omnetpp/csimplemodule.h"
#include "omnetpp/cconfiguration.h"
#include "omnetpp/platdep/platmisc.h"
#include "omnetpp/cevent.h"
#include "omnetpp/cmessage.h"
#include "omnetpp/ceventcolorer.h"

#include <algorithm>
#include <sstream>
#include <thread>

namespace omnetpp {

Register_Class(cParallelEventSet);

// 辅助函数：比较事件的时间戳和优先级
static int compareEvents(const cEvent *a, const cEvent *b)
{
    return cEvent::compareBySchedulingOrder(a, b);
}

// 辅助函数：比较事件信息的时间戳
static bool compareEventInfo(const ParallelEventInfo &a, const ParallelEventInfo &b)
{
    return compareEvents(a.event, b.event) < 0;
}

cParallelEventSet::cParallelEventSet(const char *name, cSimulation *sim) : cFutureEventSet(name),
    simulation(sim),
    useLockFree(false),
    coloringEnabled(true),
    concurrentEventCount(0)
{
    eventColorer = new cEventColorer();
}

cParallelEventSet::cParallelEventSet(const cParallelEventSet& other) : cFutureEventSet(other)
{
    std::lock_guard<std::mutex> lock(other.fesMutex);
    
    // 复制事件
    for (const auto& info : other.events) {
        events.emplace_back(info.event->dup(), info.color, info.earliestEffectTime);
        take(events.back().event);
        eventSet.insert(events.back().event);
        
        if (info.color == EVENT_RED) {
            redEvents.push_back(events.back().event);
        } else {
            greenEvents.push_back(events.back().event);
        }
    }
    
    // 复制配置
    useLockFree.store(other.useLockFree.load());
    coloringEnabled.store(other.coloringEnabled.load());
    concurrentEventCount.store(other.concurrentEventCount.load());
    
    // 复制事件着色器
    if (other.eventColorer) {
        eventColorer = other.eventColorer->dup();
    } else {
        eventColorer = new cEventColorer();
    }
    
    // 重新计算并发事件计数
    concurrentEventCount.store(greenEvents.size());
}

cParallelEventSet::~cParallelEventSet()
{
    clear();
    delete eventColorer;
}



cParallelEventSet& cParallelEventSet::operator=(const cParallelEventSet& other)
{
    if (this == &other)
        return *this;
    
    // 先清理当前对象
    clear();
    delete eventColorer;
    
    // 复制基类部分
    cFutureEventSet::operator=(other);
    
    // 复制派生类部分
    std::lock_guard<std::mutex> lock(other.fesMutex);
    
    // 复制事件
    for (const auto& info : other.events) {
        events.emplace_back(info.event->dup(), info.color, info.earliestEffectTime);
        take(events.back().event);
        eventSet.insert(events.back().event);
        
        if (info.color == EVENT_RED) {
            redEvents.push_back(events.back().event);
        } else {
            greenEvents.push_back(events.back().event);
        }
    }
    
    // 复制配置
    useLockFree.store(other.useLockFree.load());
    coloringEnabled.store(other.coloringEnabled.load());
    concurrentEventCount.store(other.concurrentEventCount.load());
    
    // 复制事件着色器
    if (other.eventColorer) {
        eventColorer = other.eventColorer->dup();
    } else {
        eventColorer = new cEventColorer();
    }
    
    // 重新计算并发事件计数
    concurrentEventCount.store(greenEvents.size());
    
    return *this;
}


cParallelEventSet *cParallelEventSet::dup() const
{
    return new cParallelEventSet(*this);
}

std::string cParallelEventSet::str() const
{
    std::lock_guard<std::mutex> lock(fesMutex);
    
    if (events.empty())
        return "empty";
    
    std::stringstream out;
    out << "length=" << events.size() 
        << ", concurrent=" << concurrentEventCount.load()
        << ", red=" << redEvents.size()
        << ", green=" << greenEvents.size();
    return out.str();
}

void cParallelEventSet::forEachChild(cVisitor *v)
{
    std::lock_guard<std::mutex> lock(fesMutex);
    
    for (auto& info : events) {
        if (!v->visit(info.event))
            return;
    }
}

void cParallelEventSet::clear()
{
    std::lock_guard<std::mutex> lock(fesMutex);
    
    for (auto& info : events) {
        dropAndDelete(info.event);
    }
    
    events.clear();
    eventSet.clear();
    redEvents.clear();
    greenEvents.clear();
    concurrentEventCount.store(0);
}

cEvent *cParallelEventSet::get(int k)
{
    std::lock_guard<std::mutex> lock(fesMutex);
    
    if (k < 0 || k >= static_cast<int>(events.size()))
        return nullptr;
    
    return events[k].event;
}

void cParallelEventSet::sort()
{
    std::lock_guard<std::mutex> lock(fesMutex);
    std::sort(events.begin(), events.end(), compareEventInfo);
    rebuildIndexes();
}

void cParallelEventSet::insert(cEvent *event)
{
    if (useLockFree.load()) {
        insertLockFree(event);
    } else {
        insertWithLock(event);
    }
}

void cParallelEventSet::insertWithLock(cEvent *event)
{
    std::lock_guard<std::mutex> lock(fesMutex);
    
    take(event);
    
    // 确定事件颜色
    EventColor color = EventColor::EVENT_RED;
    simtime_t earliestEffectTime = simTime();
    
    if (coloringEnabled.load() && eventColorer) {
        color = eventColorer->determineEventColor(event);
        earliestEffectTime = eventColorer->calculateEarliestEffectTime(event);
    }
    
    // 创建事件信息
    ParallelEventInfo info(event, color, earliestEffectTime);
    
    // 插入到主事件列表
    events.push_back(info);
    
    // 插入到快速查找集合
    eventSet.insert(event);
    
    // 插入到颜色特定列表
    if (color == EVENT_RED) {
        redEvents.push_back(event);
    } else {
        greenEvents.push_back(event);
        concurrentEventCount++;
    }
    
    // 通知等待的线程有新事件可用
    workAvailable.notify_one();
}

void cParallelEventSet::insertLockFree(cEvent *event)
{
    // 简化版的无锁实现 - 实际实现需要更复杂的无锁数据结构
    // 这里我们回退到使用锁的实现
    insertWithLock(event);
}

cEvent *cParallelEventSet::peekFirst() const
{
    std::lock_guard<std::mutex> lock(fesMutex);
    
    if (events.empty())
        return nullptr;
    
    // 找到时间最早的事件
    auto minIt = std::min_element(events.begin(), events.end(), compareEventInfo);
    return minIt->event;
}

cEvent *cParallelEventSet::removeFirst()
{
    std::lock_guard<std::mutex> lock(fesMutex);
    
    if (events.empty())
        return nullptr;
    
    // 找到时间最早的事件
    auto minIt = std::min_element(events.begin(), events.end(), compareEventInfo);
    cEvent *event = minIt->event;
    
    // 从所有数据结构中移除
    removeEventFromStructures(event);
    
    drop(event);
    return event;
}

cEvent *cParallelEventSet::remove(cEvent *event)
{
    std::lock_guard<std::mutex> lock(fesMutex);
    
    if (eventSet.find(event) == eventSet.end())
        return nullptr;
    
    removeEventFromStructures(event);
    
    drop(event);
    return event;
}

void cParallelEventSet::removeEventFromStructures(cEvent *event)
{
    // 从主事件列表中移除
    auto it = std::find_if(events.begin(), events.end(), 
                          [event](const ParallelEventInfo& info) { return info.event == event; });
    if (it != events.end()) {
        events.erase(it);
    }
    
    // 从快速查找集合中移除
    eventSet.erase(event);
    
    // 从颜色特定列表中移除
    auto redIt = std::find(redEvents.begin(), redEvents.end(), event);
    if (redIt != redEvents.end()) {
        redEvents.erase(redIt);
    } else {
        auto greenIt = std::find(greenEvents.begin(), greenEvents.end(), event);
        if (greenIt != greenEvents.end()) {
            greenEvents.erase(greenIt);
            concurrentEventCount--;
        }
    }
}

void cParallelEventSet::putBackFirst(cEvent *event)
{
    std::lock_guard<std::mutex> lock(fesMutex);
    
    take(event);
    
    // 确定事件颜色
    EventColor color = EventColor::EVENT_RED;
    simtime_t earliestEffectTime = simTime();
    
    if (coloringEnabled.load() && eventColorer) {
        color = eventColorer->determineEventColor(event);
        earliestEffectTime = eventColorer->calculateEarliestEffectTime(event);
    }
    
    // 创建事件信息并插入到开头
    ParallelEventInfo info(event, color, earliestEffectTime);
    events.insert(events.begin(), info);
    
    // 插入到快速查找集合
    eventSet.insert(event);
    
    // 插入到颜色特定列表
    if (color == EVENT_RED) {
        redEvents.push_back(event);
    } else {
        greenEvents.push_back(event);
        concurrentEventCount++;
    }
}

cEvent *cParallelEventSet::takeNextConcurrentEvent()
{
    if (useLockFree.load()) {
        return takeNextConcurrentEventLockFree();
    } else {
        return takeNextConcurrentEventWithLock();
    }
}

cEvent *cParallelEventSet::takeNextConcurrentEventWithLock()
{
    std::unique_lock<std::mutex> lock(fesMutex);
    
    // 等待有可并发执行的事件
    workAvailable.wait(lock, [this] {
        return !greenEvents.empty() || events.empty();
    });
    
    if (greenEvents.empty())
        return nullptr;
    
    // 获取第一个绿色事件
    cEvent *event = greenEvents.front();
    greenEvents.erase(greenEvents.begin());
    concurrentEventCount--;
    
    // 从其他数据结构中移除
    auto it = std::find_if(events.begin(), events.end(), 
                          [event](const ParallelEventInfo& info) { return info.event == event; });
    if (it != events.end()) {
        events.erase(it);
    }
    eventSet.erase(event);
    
    drop(event);
    return event;
}

cEvent *cParallelEventSet::takeNextConcurrentEventLockFree()
{
    // 简化版的无锁实现 - 实际实现需要更复杂的无锁数据结构
    // 这里我们回退到使用锁的实现
    return takeNextConcurrentEventWithLock();
}

void cParallelEventSet::rebuildIndexes()
{
    // 清空辅助数据结构
    eventSet.clear();
    redEvents.clear();
    greenEvents.clear();
    concurrentEventCount.store(0);
    
    // 重新构建辅助数据结构
    for (auto& info : events) {
        eventSet.insert(info.event);
        
        if (info.color == EVENT_RED) {
            redEvents.push_back(info.event);
        } else {
            greenEvents.push_back(info.event);
            concurrentEventCount++;
        }
    }
}

void cParallelEventSet::updateEventColor(cEvent *event, EventColor newColor, simtime_t earliestEffectTime)
{
    std::lock_guard<std::mutex> lock(fesMutex);
    
    // 查找事件
    auto it = std::find_if(events.begin(), events.end(), 
                          [event](const ParallelEventInfo& info) { return info.event == event; });
    if (it == events.end())
        return;
    
    // 更新颜色和最早影响时间
    EventColor oldColor = it->color;
    it->color = newColor;
    it->earliestEffectTime = earliestEffectTime;
    
    // 如果颜色改变，更新辅助数据结构
    if (oldColor != newColor) {
        // 从旧颜色列表中移除
        if (oldColor == EVENT_RED) {
            auto redIt = std::find(redEvents.begin(), redEvents.end(), event);
            if (redIt != redEvents.end()) {
                redEvents.erase(redIt);
            }
        } else {
            auto greenIt = std::find(greenEvents.begin(), greenEvents.end(), event);
            if (greenIt != greenEvents.end()) {
                greenEvents.erase(greenIt);
                concurrentEventCount--;
            }
        }
        
        // 添加到新颜色列表
        if (newColor == EVENT_RED) {
            redEvents.push_back(event);
        } else {
            greenEvents.push_back(event);
            concurrentEventCount++;
        }
    }
}

bool cParallelEventSet::canExecuteConcurrently(cEvent *event1, cEvent *event2) const
{
    // 简化版：检查两个事件是否可以并发执行
    // 实际实现需要更复杂的依赖分析
    if (!event1 || !event2)
        return false;
        
    // 如果两个事件的目标模块相同，不能并发执行
    if (dynamic_cast<cMessage*>(event1)->getArrivalModule() == dynamic_cast<omnetpp::cMessage*>(event2)->getArrivalModule())
        return false;
    
    // 如果两个事件的时间戳不同，可以并发执行
    if (event1->getArrivalTime() != event2->getArrivalTime())
        return true;
    
    // 更复杂的依赖分析可以在这里实现
    return true;
}

int cParallelEventSet::getLength() const
{
    std::lock_guard<std::mutex> lock(fesMutex);
    return events.size();
}

bool cParallelEventSet::isEmpty() const
{
    std::lock_guard<std::mutex> lock(fesMutex);
    return events.empty();
}

int cParallelEventSet::getConcurrentEventCount() const
{
    return concurrentEventCount.load();
}

EventColor cParallelEventSet::getEventColor(cEvent *event) const
{
    std::lock_guard<std::mutex> lock(fesMutex);
    
    auto it = std::find_if(events.begin(), events.end(), 
                          [event](const ParallelEventInfo& info) { return info.event == event; });
    if (it == events.end())
        return EventColor::EVENT_RED;
    
    return it->color;
}

void cParallelEventSet::setEventColor(cEvent *event, EventColor color, simtime_t earliestEffectTime)
{
    updateEventColor(event, color, earliestEffectTime);
}

simtime_t cParallelEventSet::getEarliestEffectTime(cEvent *event) const
{
    std::lock_guard<std::mutex> lock(fesMutex);
    
    auto it = std::find_if(events.begin(), events.end(), 
                          [event](const ParallelEventInfo& info) { return info.event == event; });
    if (it == events.end())
        return simTime();
    
    return it->earliestEffectTime;
}

void cParallelEventSet::recolorAllEvents()
{
    std::lock_guard<std::mutex> lock(fesMutex);
    
    if (!eventColorer)
        return;
    
    for (auto& info : events) {
        EventColor newColor = eventColorer->determineEventColor(info.event);
        simtime_t earliestEffectTime = eventColorer->calculateEarliestEffectTime(info.event);
        
        if (info.color != newColor) {
            // 更新颜色
            EventColor oldColor = info.color;
            info.color = newColor;
            info.earliestEffectTime = earliestEffectTime;
            
            // 更新辅助数据结构
            if (oldColor == EVENT_RED) {
                auto redIt = std::find(redEvents.begin(), redEvents.end(), info.event);
                if (redIt != redEvents.end()) {
                    redEvents.erase(redIt);
                }
            } else {
                auto greenIt = std::find(greenEvents.begin(), greenEvents.end(), info.event);
                if (greenIt != greenEvents.end()) {
                    greenEvents.erase(greenIt);
                    concurrentEventCount--;
                }
            }
            
            if (newColor == EVENT_RED) {
                redEvents.push_back(info.event);
            } else {
                greenEvents.push_back(info.event);
                concurrentEventCount++;
            }
        }
    }
}

void cParallelEventSet::recolorEvent(cEvent *event)
{
    std::lock_guard<std::mutex> lock(fesMutex);
    
    if (!eventColorer)
        return;
    
    auto it = std::find_if(events.begin(), events.end(), 
                          [event](const ParallelEventInfo& info) { return info.event == event; });
    if (it == events.end())
        return;
    
    EventColor newColor = eventColorer->determineEventColor(event);
    simtime_t earliestEffectTime = eventColorer->calculateEarliestEffectTime(event);
    updateEventColor(event, newColor, earliestEffectTime);
}

void cParallelEventSet::setUseLockFree(bool use)
{
    useLockFree.store(use);
}

bool cParallelEventSet::getUseLockFree() const
{
    return useLockFree.load();
}

void cParallelEventSet::setColoringEnabled(bool enabled)
{
    coloringEnabled.store(enabled);
}

bool cParallelEventSet::getColoringEnabled() const
{
    return coloringEnabled.load();
}

void cParallelEventSet::setEventColorer(cEventColorer *colorer)
{
    std::lock_guard<std::mutex> lock(fesMutex);
    
    if (eventColorer)
        delete eventColorer;
    
    eventColorer = colorer;
}

cEventColorer *cParallelEventSet::getEventColorer() const
{
    return eventColorer;
}

}  // namespace omnetpp