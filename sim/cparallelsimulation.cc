//=========================================================================
//  CPARALLELSIMULATION.CC
//
//                  OMNeT++/OMNEST
//           Discrete System Simulation in C++
//
//   Parallel Simulation Extension Implementation
//
//=========================================================================

/*--------------------------------------------------------------*
  Copyright (C) 1992-2017 Andras Varga
  Copyright (C) 2006-2017 OpenSim Ltd.
  Copyright (C) 2025 [Your Name/Institution]

  This file is distributed WITHOUT ANY WARRANTY. See the file
  `license' for details on this and other legal matters.
*--------------------------------------------------------------*/

#include "omnetpp/cparallelsimulation.h"
#include "omnetpp/csimulation.h"
#include "omnetpp/cscheduler.h"
#include "omnetpp/ceventheap.h"
#include "omnetpp/cenvir.h"
#include "omnetpp/cmodule.h"
#include "omnetpp/csimplemodule.h"
#include "omnetpp/cconfiguration.h"
#include "omnetpp/platdep/platmisc.h"
#include "omnetpp/cparalleleventset.h"
#include "omnetpp/ceventcolorer.h"
#include "omnetpp/cparallelscheduler.h"
#include "omnetpp/platdep/platmisc.h" 

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <algorithm>


namespace omnetpp {



cParallelSimulation::cParallelSimulation(const char *name, cEnvir *env) : cSimulation(name, env)
{
    // 初始化并行仿真特有成员
    numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 1;
    
    useLockFreeFES = true;
    
    parallelFES = nullptr;
    eventColorer = nullptr;
    parallelScheduler = nullptr;
    
    eventsProcessed = 0;
    concurrentEvents = 0;
    simulationRunning = false;
    activeWorkers = 0;
}

cParallelSimulation::~cParallelSimulation()
{
    shutdownParallelComponents();
}

void cParallelSimulation::setNumThreads(unsigned int num)
{
    if (getSystemModule())
        throw cRuntimeError("setNumThreads(): Cannot change thread count after network setup");
    
    numThreads = (num > 0) ? num : 1;
}

unsigned int cParallelSimulation::getNumThreads() const
{
    return numThreads;
}

void cParallelSimulation::setUseLockFreeFES(bool useLockFree)
{
    if (getSystemModule())
        throw cRuntimeError("setUseLockFreeFES(): Cannot change FES type after network setup");
    
    useLockFreeFES = useLockFree;
}

bool cParallelSimulation::getUseLockFreeFES() const
{
    return useLockFreeFES;
}

void cParallelSimulation::initializeParallelComponents()
{
    // 创建并行事件集
    parallelFES = new cParallelEventSet("parallel-fes");
    
    // 创建事件着色器
    eventColorer = new cEventColorer();
    
    // 创建并行调度器
    parallelScheduler = new cParallelScheduler();
    
    // 替换基类的FES和调度器
    // setFES(parallelFES);
    // setScheduler(parallelScheduler);
}

void cParallelSimulation::shutdownParallelComponents()
{
    // 停止所有工作线程
    simulationRunning = false;
    workAvailable.notify_all();
    
    // 等待所有线程完成
    for (auto& thread : workerThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    workerThreads.clear();
    
    // 清理并行组件
    delete eventColorer;
    eventColorer = nullptr;
    
    // 注意：parallelFES和parallelScheduler由基类负责删除
    parallelFES = nullptr;
    parallelScheduler = nullptr;
}

void cParallelSimulation::setupNetwork(cModuleType *networkType)
{
    // 初始化并行组件
    initializeParallelComponents();
    
    // 调用基类实现
    cSimulation::setupNetwork(networkType);
}

void cParallelSimulation::callInitialize()
{
   // 调用基类实现
   cSimulation::callInitialize();

   threadPoolRunning = true;
   simulationRunning = false;
        
   // 创建线程池
    {
        std::unique_lock<std::mutex> lock(initMutex);
        for (unsigned i = 0; i < 2; i++) {
            workerThreads.emplace_back([this] {
                // 通知线程已启动
                {
                    std::lock_guard<std::mutex> lock(initMutex);
                    threadsReady++;
                }
                initCondition.notify_one();
                
                // 进入工作循环
                workerThreadFunction();
            });
        }
        
        // 等待所有线程启动完成
        initCondition.wait(lock, [this] {
            return threadsReady == 2;
        });
    }
    
    threadPoolState = ThreadPoolState::CREATED;
}

void cParallelSimulation::executeEvent(cEvent *event)
{
    // 在并行仿真中，这个方法应该只被工作线程调用
    // 我们重写它以实现并行事件处理
    
    // 增加事件计数
    eventsProcessed++;
    
    // 调用基类实现
    cSimulation::executeEvent(event);
    
    // 增加并发事件计数
    concurrentEvents++;
}

// void cParallelSimulation::workerThreadFunction()
// {
 
    
//    while (threadPoolRunning) {  // 使用threadPoolRunning控制循环
//         cEvent* event = nullptr;
        
//         // 等待任务或退出信号
//         {
//             std::unique_lock<std::mutex> lock(queueMutex);
//             queueCondition.wait(lock, [this] {
//                 return !threadPoolRunning || !taskQueue.empty();
//             });
            
//             if (!threadPoolRunning) break;
            
//             if (!taskQueue.empty()) {
//                 event = taskQueue.front();
//                 taskQueue.pop();
//                 activeWorkers++;
//             }
//         }
        
//         if (event) 
//         {
//             // 执行事件
//             executeEvent(event);
            
//             // 更新状态
//             {
//                 std::lock_guard<std::mutex> lock(queueMutex);
//                 activeWorkers--;
//                 pendingTasks--;
//                 //  std::cout<<pendingTasks<<endl;
                
//                 if (pendingTasks == 0) {
//                     batchCompletionCondition.notify_one();
//                 }
//             }
//         }
//     }
// }
// void cParallelSimulation::distributeAndExecuteEvents(cEvent *yevent)
// {
//     // 确保线程池已创建
//     if (!simulationRunning) {
//         startSimulation();
//     }
    
//     // 重置状态
//     pendingTasks = 1;
    
//     // 获取FES中的所有事件
//     std::vector<cEvent*> events;
//     {
//         // std::lock_guard<std::mutex> fesLock(fesMutex);
//         while (!getFES()->isEmpty()) {
//             cEvent* event = getFES()->peekFirst();
//             if (dynamic_cast<cMessage*>(event)) {
//                 events.push_back(event);
//                 getFES()->remove(event);
//                 pendingTasks++;
//             }
//             else
//             {
//                 break;
//             }
//         }
//     }
    
//     // 将事件添加到任务队列
//     {
//         taskQueue.push(yevent);
//         // std::lock_guard<std::mutex> queueLock(queueMutex);
//         for (cEvent* event : events) {
//             taskQueue.push(event);
//         }
//     }
    
//     // 唤醒所有工作线程
//     queueCondition.notify_all();
    
//     // 等待所有事件完成
//     {
//         std::unique_lock<std::mutex> batchLock(batchMutex);
//         batchCompletionCondition.wait(batchLock, [this] {
//             return pendingTasks == 0;
//         });
//     }
    
//     // 重置线程池状态
//     threadPoolState = ThreadPoolState::CREATED;
//     //  std::cout<<1<<endl;
// }
void cParallelSimulation::distributeAndExecuteEvents(cEvent *yevent)
{
    // 确保线程池已创建
    if (workerThreads.empty()) {
        startSimulation();
    }
    
    // 在锁保护下操作共享资源
    // {
    //     std::lock_guard<std::mutex> lock(globalMutex);
        
        // 重置状态
        pendingTasks = 1; // yevent
        
        // 获取FES中的所有事件
        std::vector<cEvent*> events;
        while (!getFES()->isEmpty()) {
            cEvent* event = getFES()->peekFirst();
            if (dynamic_cast<cMessage*>(event)) {
                events.push_back(event);
                getFES()->remove(event);
                pendingTasks++; // 在锁内增加
                // std::cout<<pendingTasks<<endl;
            } else {
                break;
            }
        }
        // if(yevent->getArrivalTime().dbl()>10000)
        // {
        //   std::cout<<getFES()->getLength()<<endl;
        // }
        // 将事件添加到任务队列
        taskQueue.push(yevent);
        for (cEvent* event : events) {
            taskQueue.push(event);
        }
    // }
    
    // 唤醒所有工作线程
    workCondition.notify_all();
    
    // 等待所有事件完成
    {
        std::unique_lock<std::mutex> lock(globalMutex);
        workCondition.wait(lock, [this] {
            return pendingTasks == 0;
        });
    }
    setGVT(getCurrentSimTime());
}
void cParallelSimulation::workerThreadFunction()
{
    while (threadPoolRunning) {
        cEvent* event = nullptr;
        
        // 等待任务或退出信号
        {
            std::unique_lock<std::mutex> lock(globalMutex);
            workCondition.wait(lock, [this] {
                return !threadPoolRunning || !taskQueue.empty();
            });
            
            if (!threadPoolRunning) break;
            
            if (!taskQueue.empty()) {
                event = taskQueue.front();
                taskQueue.pop();
                activeWorkers++;
            }
        }
        
        if (event) 
        {
            // 执行事件
            executeEvent(event);
            
            // 更新状态
            // {
            //     std::lock_guard<std::mutex> lock(globalMutex);
               activeWorkers.fetch_sub(1, std::memory_order_relaxed);
               pendingTasks.fetch_sub(1, std::memory_order_relaxed);
             {
                std::lock_guard<std::mutex> lock(globalMutex);
                if (pendingTasks == 0) {
                    workCondition.notify_all(); // 通知主线程
                }
             }
        }
    }
}

void cParallelSimulation::startSimulation()
{
    if (threadPoolState != ThreadPoolState::CREATED) {
        throw cRuntimeError("Simulation already running or not initialized");
    }
    
    simulationRunning = true;
    threadPoolState = ThreadPoolState::RUNNING;
}



void cParallelSimulation::callFinish()
{
    // 停止线程池
    threadPoolRunning = false;
    simulationRunning = false;
    
    // 唤醒所有工作线程
    {
        std::lock_guard<std::mutex> lock(globalMutex);
        workCondition.notify_all();
    }
    
    // 等待所有线程完成
    for (auto& thread : workerThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    workerThreads.clear();
    
    threadPoolState = ThreadPoolState::STOPPED;
    
    // 调用基类实现
    cSimulation::callFinish();
}
int cParallelSimulation::getConcurrentlyExecutableEventCount() const
{
    if (!parallelFES) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(fesMutex);
    return parallelFES->getConcurrentEventCount();
}

long cParallelSimulation::getEventsProcessedCount() const
{
    return eventsProcessed;
}

long cParallelSimulation::getConcurrentEventsCount() const
{
    return concurrentEvents;
}

double cParallelSimulation::getSpeedup() const
{
    if (eventsProcessed == 0) {
        return 1.0;
    }
    
    return static_cast<double>(concurrentEvents) / eventsProcessed;
}

bool cParallelSimulation::isSupported()
{
    return true;
}

unsigned int cParallelSimulation::getRecommendedThreadCount()
{
    unsigned int threads = std::thread::hardware_concurrency();
    return (threads > 0) ? threads : 1;
}


void cParallelSimulation::insertEvent(cEvent*event)
{
//    std::lock_guard<std::mutex> fesLock(fesMutex);

   cSimulation::insertEvent(event);

}




}  // namespace omnetpp
