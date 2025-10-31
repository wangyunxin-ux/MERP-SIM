//==========================================================================
//  CPARALLELEVENTSET.H - part of
//                     OMNeT++/OMNEST
//            Discrete System Simulation in C++
//
//        Parallel Event Set Implementation
//
//==========================================================================

/*--------------------------------------------------------------*
  Copyright (C) 1992-2017 Andras Varga
  Copyright (C) 2006-2017 OpenSim Ltd.
  Copyright (C) 2025 [Your Name/Institution]

  This file is distributed WITHOUT ANY WARRANTY. See the file
  `license' for details on this and other legal matters.
*--------------------------------------------------------------*/

#ifndef __OMNETPP_CPARALLELEVENTSET_H
#define __OMNETPP_CPARALLELEVENTSET_H

#include "cfutureeventset.h"
#include <vector>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <unordered_set>

namespace omnetpp {

// 前向声明
class cEventColorer;
class cSimulation;

/**
 * @brief Event color types for parallel simulation.
 */
enum EventColor {
    EVENT_RED,    // Event cannot be executed concurrently (has dependencies)
    EVENT_GREEN   // Event can be executed concurrently (no dependencies)
};

/**
 * @brief Extended event information for parallel simulation.
 */
struct ParallelEventInfo {
    cEvent *event;
    EventColor color;
    simtime_t earliestEffectTime; // Earliest time this event can affect other events
    
    ParallelEventInfo(cEvent *e, EventColor c, simtime_t t) : event(e), color(c), earliestEffectTime(t) {}
};

/**
 * @brief Parallel event set implementation for concurrent event execution.
 *
 * cParallelEventSet extends cFutureEventSet to support concurrent event access
 * and execution in a multi-threaded environment. It implements event coloring
 * to determine which events can be executed in parallel.
 *
 * @ingroup SimCore
 */
class SIM_API cParallelEventSet : public cFutureEventSet
{
  private:
    // 主数据结构 - 使用多容器方法支持并行访问
    std::vector<ParallelEventInfo> events;          // 所有事件的向量
    mutable std::mutex eventsMutex;                 // 保护events的互斥锁
    
    // 用于快速访问的数据结构
    std::unordered_set<cEvent*> eventSet;           // 用于快速查找的事件集合
    std::vector<cEvent*> redEvents;                 // 红色事件列表（不可并行）
    std::vector<cEvent*> greenEvents;              // 绿色事件列表（可并行）
    
    // 同步原语
    mutable std::mutex fesMutex;                   // 保护整个FES的互斥锁
    mutable std::condition_variable workAvailable; // 工作可用条件变量
    
    // 配置和状态
    std::atomic<bool> useLockFree;                // 是否使用无锁实现
    std::atomic<bool> coloringEnabled;            // 是否启用事件着色
    std::atomic<int> concurrentEventCount;        // 当前可并发执行的事件数量
    
    // 相关组件
    cEventColorer *eventColorer;                  // 事件着色器
    cSimulation *simulation;                      // 关联的仿真对象

  private:
    // 内部方法
    void rebuildIndexes();                         // 重建索引结构
    void updateEventColor(cEvent *event, EventColor newColor, simtime_t earliestEffectTime);
    bool canExecuteConcurrently(cEvent *event1, cEvent *event2) const;
    
    // 无锁实现方法
    cEvent *takeNextConcurrentEventLockFree();
    void insertLockFree(cEvent *event);
    
    // 基于锁的实现方法
    cEvent *takeNextConcurrentEventWithLock();
    void insertWithLock(cEvent *event);

  public:
    /** @name Constructors, destructor, assignment */
    //@{
    /**
     * Copy constructor.
     */
    cParallelEventSet(const cParallelEventSet& other);
    
    /**
     * Constructor.
     */
    cParallelEventSet(const char *name=nullptr, cSimulation *sim=nullptr);
    
    /**
     * Destructor.
     */
    virtual ~cParallelEventSet();
    
    /**
     * Assignment operator.
     */
    cParallelEventSet& operator=(const cParallelEventSet& other);
    //@}
    
    /** @name Configuration methods */
    //@{
    /**
     * Sets whether to use lock-free implementation.
     */
    void setUseLockFree(bool use);
    
    /**
     * Returns whether lock-free implementation is enabled.
     */
    bool getUseLockFree() const;
    
    /**
     * Sets whether event coloring is enabled.
     */
    void setColoringEnabled(bool enabled);
    
    /**
     * Returns whether event coloring is enabled.
     */
    bool getColoringEnabled() const;
    
    /**
     * Sets the event colorer to use.
     */
    void setEventColorer(cEventColorer *colorer);
    
    /**
     * Returns the event colorer in use.
     */
    cEventColorer *getEventColorer() const;
    //@}
    
    /** @name Redefined cFutureEventSet methods */
    //@{
    /**
     * Creates and returns an exact copy of this object.
     */
    virtual cParallelEventSet *dup() const override;
    
    /**
     * Produces a one-line description of the object's contents.
     */
    virtual std::string str() const override;
    
    /**
     * Calls v->visit(this) for each contained object.
     */
    virtual void forEachChild(cVisitor *v) override;
    //@}
    
    /** @name Simulation-related operations */
    //@{
    /**
     * Insert an event into the FES.
     */
    virtual void insert(cEvent *event) override;
    
    /**
     * Peek the first event in the FES.
     */
    virtual cEvent *peekFirst() const override;
    
    /**
     * Removes and return the first event in the FES.
     */
    virtual cEvent *removeFirst() override;
    
    /**
     * Undo for removeFirst(): puts back an event to the front of the FES.
     */
    virtual void putBackFirst(cEvent *event) override;
    
    /**
     * Removes and returns the given event from the FES.
     */
    virtual cEvent *remove(cEvent *event) override;

void removeEventFromStructures(cEvent * event);
    
    /**
     * Returns true if the FES is empty.
     */
    virtual bool isEmpty() const override;
    
    /**
     * Deletes all events in the FES.
     */
    virtual void clear() override;
    //@}
    
    /** @name Random access */
    //@{
    /**
     * Returns the number of events in the FES.
     */
    virtual int getLength() const override;
    
    /**
     * Returns the kth event in the FES.
     */
    virtual cEvent *get(int k) override;
    
    /**
     * Sorts the contents of the FES.
     */
    virtual void sort() override;
    //@}
    
    /** @name Parallel-specific methods */
    //@{
    /**
     * Returns the next event that can be executed concurrently.
     * Returns nullptr if no such event is available.
     */
    virtual cEvent *takeNextConcurrentEvent();
    
    /**
     * Returns the number of events that can be executed concurrently.
     */
    virtual int getConcurrentEventCount() const;
    
    /**
     * Returns the color of the given event.
     */
    virtual EventColor getEventColor(cEvent *event) const;
    
    /**
     * Sets the color of the given event.
     */
    virtual void setEventColor(cEvent *event, EventColor color, simtime_t earliestEffectTime);
    
    /**
     * Recolors all events in the FES based on current simulation state.
     */
    virtual void recolorAllEvents();
    
    /**
     * Recolors a specific event based on current simulation state.
     */
    virtual void recolorEvent(cEvent *event);
    
    /**
     * Returns the earliest time the given event can affect other events.
     */
    virtual simtime_t getEarliestEffectTime(cEvent *event) const;
    //@}
};

}  // namespace omnetpp

#endif