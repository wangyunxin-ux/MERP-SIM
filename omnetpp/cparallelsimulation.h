//==========================================================================
//   CPARALLELSIMULATION.H
//
//                     OMNeT++/OMNEST
//            Discrete System Simulation in C++
//
//        Parallel Simulation Extension Header
//
//==========================================================================

/*--------------------------------------------------------------*
  Copyright (C) 1992-2017 Andras Varga
  Copyright (C) 2006-2017 OpenSim Ltd.
  Copyright (C) 2025 [Your Name/Institution]
  
  This file is distributed WITHOUT ANY WARRANTY. See the file
  `license' for details on this and other legal matters.
*--------------------------------------------------------------*/

#ifndef __OMNETPP_CPARALLELSIMULATION_H
#define __OMNETPP_CPARALLELSIMULATION_H

#include "csimulation.h"
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>

namespace omnetpp {

// 前向声明
class cParallelEventSet;
class cEventColorer;
class cParallelScheduler;

/**
 * @brief Parallel simulation manager class.
 *
 * cParallelSimulation extends cSimulation to support concurrent event execution
 * using multiple threads in a shared-memory environment.
 *
 * @ingroup SimCore
 */
class SIM_API cParallelSimulation : public cSimulation
{
  private:
    // 并行仿真特有成员变量
    std::vector<std::thread> workerThreads;     // 工作线程池
    std::atomic<bool> simulationRunning;        // 仿真运行状态标志
    std::atomic<int> activeWorkers;             // 活跃工作线程计数
    
    // 同步原语
    mutable std::mutex fesMutex;                        // 保护FES访问的互斥锁
    std::condition_variable workAvailable;      // 工作可用条件变量
    std::condition_variable simulationDone;     // 仿真完成条件变量
    
    // 并行组件
    cParallelEventSet* parallelFES;             // 并行事件集
    cEventColorer* eventColorer;                // 事件着色器
    cParallelScheduler* parallelScheduler;       // 并行调度器
    
    // 配置参数
    unsigned int numThreads;                    // 工作线程数量
    bool useLockFreeFES;                        // 是否使用无锁FES实现
    
    // 统计信息
    std::atomic<long> eventsProcessed;          // 已处理事件计数
    std::atomic<long> concurrentEvents;         // 并发执行事件计数
    std::mutex startMutex;
    std::condition_variable startCondition;
    bool workersShouldStart = false;
    enum class ThreadPoolState {
        CREATED,    // 线程已创建但未启动
        RUNNING,    // 线程正在运行
        STOPPED     // 线程已停止
    };
    
    ThreadPoolState threadPoolState = ThreadPoolState::CREATED;
    std::atomic<int> pendingTasks{0};
    std::condition_variable queueCondition;
    std::atomic<bool> batchProcessing{false};
    std::condition_variable batchCompletionCondition;
    std::mutex batchMutex;
    std::queue<cEvent*> taskQueue;
    std::mutex queueMutex;
    // 添加线程启动同步机制
    std::mutex initMutex;
    std::condition_variable initCondition;
    std::atomic<int> threadsReady{0};

    std::atomic<bool> threadPoolRunning{true}; 
    std::mutex globalMutex;
    std::condition_variable  workCondition;
  private:
    // 内部方法
    void workerThreadFunction();     // 工作线程主函数
    void initializeParallelComponents();         // 初始化并行组件
    void shutdownParallelComponents();           // 关闭并行组件
    
  public:
    /** @name Constructor, destructor. */
    //@{
    /**
     * Constructor.
     */
    cParallelSimulation(const char *name, cEnvir *env);
    
    /**
     * Destructor.
     */
    virtual ~cParallelSimulation();
    //@}
    
    /** @name Configuration methods. */
    //@{
    /**
     * Sets the number of worker threads to use for parallel execution.
     */
    void setNumThreads(unsigned int num);
    
    /**
     * Returns the number of worker threads configured for parallel execution.
     */
    unsigned int getNumThreads() const;
    
    /**
     * Enables or disables lock-free FES implementation.
     */
    void setUseLockFreeFES(bool useLockFree);
    
    /**
     * Returns whether lock-free FES implementation is enabled.
     */
    bool getUseLockFreeFES() const;
    //@}
    
    /** @name Overridden public methods. */
    //@{
    /**
     * Sets up the simulation network for parallel execution.
     */
    virtual void setupNetwork(cModuleType *networkType) override;
    
    /**
     * Initializes the simulation for parallel execution.
     */
    virtual void callInitialize() override;
    
    /**
     * Main parallel simulation execution method.
     */
    virtual void executeEvent(cEvent *event) override;
    
    /**
     * Finishes the parallel simulation and collects results.
     */
    virtual void callFinish() override;
    //@}
    
    /** @name Parallel-specific methods. */
    //@{
    /**
     * Returns the number of events that can be executed concurrently.
     */
    int getConcurrentlyExecutableEventCount() const;
    
    /**
     * Returns the number of events processed so far.
     */
    long getEventsProcessedCount() const;
    
    /**
     * Returns the number of events that were executed concurrently.
     */
    long getConcurrentEventsCount() const;
    
    /**
     * Returns the parallel speedup achieved.
     */
    double getSpeedup() const;
    //@}
    
    /** @name Static utility methods. */
    //@{
    /**
     * Checks if parallel simulation is supported.
     */
    static bool isSupported();
     
    virtual void distributeAndExecuteEvents(cEvent*yevent);
    virtual void startSimulation();
    virtual void insertEvent(cEvent *event) override;
    
    /**
     * Returns the recommended number of threads for the current system.
     */
    static unsigned int getRecommendedThreadCount();
    //@}
};

}  // namespace omnetpp

#endif