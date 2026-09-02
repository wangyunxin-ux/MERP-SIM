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
#include "omnetpp/cdelaychannel.h"
#include "omnetpp/cdataratechannel.h"


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
    i=j=0;
    al=RP;
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
    // parallelFES = new cParallelEventSet("parallel-fes");
    
    // // 创建事件着色器
    // eventColorer = new cEventColorer();
    
    // // 创建并行调度器
    // parallelScheduler = new cParallelScheduler();
    
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
            return threadsReady ==2;
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
    // concurrentEvents++;
}


void cParallelSimulation::distributeAndExecuteEvents(cEvent *yevent)
{
    // 确保线程池已创建
    if (workerThreads.empty()) {
        startSimulation();
    }
    // auto start = std::chrono::high_resolution_clock::now();

    if(al==FW)
    {
     cSimulation* sim=getSimulation();
     cModule*systemModule=sim->getSystemModule();
     for (cModule::SubmoduleIterator jt(systemModule); !jt.end(); ++jt)
    {
        cModule* module = *jt;
        if(module->hasGates()) 
        {
                for (cModule::GateIterator it(module); !it.end(); it++) {
                    cGate* gate = *it;
                    if(gate->getChannel() && gate->getNextGate()) {
                        cModule* oModule = gate->getNextGate()->getOwnerModule();
                        cChannel* channel = gate->getChannel();
                        simtime_t delay=0;
                        if(dynamic_cast<cDelayChannel*>(channel))
                        {
                         delay = dynamic_cast<cDelayChannel*>(channel)->getDelay();
                        }
                        else if(dynamic_cast<cDatarateChannel*>(channel))
                        {
                         delay = dynamic_cast<cDatarateChannel*>(channel)->getDelay();
                        }
                        // 标准化模块对
                        cModule* m1 = module;
                        cModule* m2 = oModule;
                        if (m2 < m1) std::swap(m1, m2);
                        auto key = std::make_pair(m1, m2);

                        // 如果已经存在，取最小值；否则插入
                        auto it = minDelayMap.find(key);
                        if (it != minDelayMap.end()) {
                            if (delay < it->second) {
                                it->second = delay;
                            }
                        } else {
                            minDelayMap[key] = delay;
                        }
                    }
                }
        }
    }
     fw.buildGraph(minDelayMap);
     al=FW_READY;
    //  auto end = std::chrono::high_resolution_clock::now();
    //  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    //  std::cout << "代码运行时间: " << duration.count() << " 微秒" << std::endl;
    //  std::cout << "代码运行时间: " << duration.count() / 1000.0 << " 毫秒" << std::endl;
    }


    if(al==RP)
        {
            cSimulation* sim = getSimulation();
            cModule* systemModule = sim->getSystemModule();
            std::map<std::pair<cModule*, cModule*>, simtime_t> minDelayMap;
            std::unordered_set<cModule*> gatedModules;
            // 收集所有直接连接
            for (cModule::SubmoduleIterator jt(systemModule); !jt.end(); ++jt)
            {
                cModule* module = *jt;
                if(module->hasGates())
                {
                    // 记录有门的模块
                    gatedModules.insert(module);

                    for (cModule::GateIterator it(module); !it.end(); it++)
                    {
                        cGate* gate = *it;
                        if(gate->getChannel() && gate->getNextGate())
                        {
                            cModule* oModule = gate->getNextGate()->getOwnerModule();
                            cChannel* channel = gate->getChannel();
                            simtime_t delay = 0;

                            if(dynamic_cast<cDelayChannel*>(channel))
                            {
                                delay = dynamic_cast<cDelayChannel*>(channel)->getDelay();
                            }
                            else if(dynamic_cast<cDatarateChannel*>(channel))
                            {
                                delay = dynamic_cast<cDatarateChannel*>(channel)->getDelay();
                            }

                            // 标准化模块对
                            cModule* m1 = module;
                            cModule* m2 = oModule;
                            if (m2 < m1) std::swap(m1, m2);
                            auto key = std::make_pair(m1, m2);

                            // 如果已经存在，取最小值；否则插入
                            auto it = minDelayMap.find(key);
                            if (it != minDelayMap.end())
                            {
                                if (delay < it->second)
                                {
                                    it->second = delay;
                                }
                            }
                            else
                            {
                                minDelayMap[key] = delay;
                            }
                        }
                    }
                }
            }
            estimator.build(minDelayMap, gatedModules);
            al=RP_READY;
            // auto end = std::chrono::high_resolution_clock::now();
            // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            // std::cout << "代码运行时间: " << duration.count() << " 微秒" << std::endl;
            // std::cout << "代码运行时间: " << duration.count() / 1000.0 << " 毫秒" << std::endl;
        }

    if(al==D)
    {
     cSimulation* sim=getSimulation();
     cModule*systemModule=sim->getSystemModule();
     for (cModule::SubmoduleIterator jt(systemModule); !jt.end(); ++jt)
    {
        cModule* module = *jt;
        if(module->hasGates()) 
        {
                for (cModule::GateIterator it(module); !it.end(); it++) {
                    cGate* gate = *it;
                    if(gate->getChannel() && gate->getNextGate()) {
                        cModule* oModule = gate->getNextGate()->getOwnerModule();
                        cChannel* channel = gate->getChannel();
                        simtime_t delay=0;
                        if(dynamic_cast<cDelayChannel*>(channel))
                        {
                         delay = dynamic_cast<cDelayChannel*>(channel)->getDelay();
                        }
                        else if(dynamic_cast<cDatarateChannel*>(channel))
                        {
                         delay = dynamic_cast<cDatarateChannel*>(channel)->getDelay();
                        }
                        // 标准化模块对
                        cModule* m1 = module;
                        cModule* m2 = oModule;
                        if (m2 < m1) std::swap(m1, m2);
                        auto key = std::make_pair(m1, m2);

                        // 如果已经存在，取最小值；否则插入
                        auto it = minDelayMap.find(key);
                        if (it != minDelayMap.end()) {
                            if (delay < it->second) {
                                it->second = delay;
                            }
                        } else {
                            minDelayMap[key] = delay;
                        }
                    }
                }
        }
    }
     d.buildGraph(minDelayMap);
     al=D_READY;
    //  auto end = std::chrono::high_resolution_clock::now();
    //  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    //  std::cout << "代码运行时间: " << duration.count() << " 微秒" << std::endl;
    //  std::cout << "代码运行时间: " << duration.count() / 1000.0 << " 毫秒" << std::endl;
    }
     

        // 重置状态
        pendingTasks = 1; // yevent
        
        // 获取FES中的所有事件
        std::vector<cEvent*> events;
        events.push_back(yevent);
        cModule* module;
        // int id=dynamic_cast<cMessage*>(yevent)->getArrivalModuleId();
        // std::vector<cModule*>currentModule;
        // currentModule.push_back(module);
        bool shouldBreakWhile = false;
        // std::vector<int> currentModuleIds;
        // currentModuleIds.push_back(id);
        while (!getFES()->isEmpty()) 
        {
            cEvent* event = getFES()->peekFirst();
            if (dynamic_cast<cMessage*>(event)) {
                module=dynamic_cast<cMessage*>(event)->getArrivalModule();
                if(!module)
                   break;
                if(!module->hasGates())
                   break;
                // if(module->hasGates())
                // {
                // id=dynamic_cast<cMessage*>(event)->getArrivalModuleId();
                // bool exists = std::find(currentModule.begin(), currentModule.end(), module) != currentModule.end();
                // f
                    // currentModule.push_back(module);
                    for (cEvent* currentevent : events) 
                    {
                        cModule*rootnode=findRootNode(module);
                        cModule*currentrootnode=findRootNode(dynamic_cast<cMessage*>(currentevent)->getArrivalModule());
                        if(rootnode == currentrootnode)
                        {
                            shouldBreakWhile=true;
                            break;   
                        }
                        simtime_t T;
                        if(al==RP_READY)
                        T=estimator.getLowerBoundDelay(rootnode, currentrootnode);
                        else if(al==FW_READY)
                        T=fw.getShortestDelay(rootnode,currentrootnode);
                        else if(al==D_READY)
                        T=d.getShortestDelay(rootnode,currentrootnode);
                        // std::cout<<T.dbl()<<endl;
                        if(std::min((currentevent->getArrivalTime()).dbl(),(event->getArrivalTime()).dbl())+T.dbl()<std::max((currentevent->getArrivalTime()).dbl(),(event->getArrivalTime()).dbl()))
                        {
                            shouldBreakWhile=true;
                            break;
                        }
                        
                    }
                    
                    if (shouldBreakWhile)
                    {
                        break;
                    }   
                    else
                    {
                        events.push_back(event);
                        getFES()->remove(event);
                        pendingTasks++;
                    }
            } 
            else 
            {
                break;
            }
        }
         recordPendingTaskCount(pendingTasks);
         if(pendingTasks>1){
          concurrentEvents= concurrentEvents+pendingTasks;   
          pcount=pcount+1;    // 并发执行事件计数
        //   std::cout<<"发现一组可并发执行事件，数量为："<<pendingTasks<<endl;
         }
         if(pendingTasks>maxpevent)
         maxpevent=pendingTasks;
         if(pendingTasks<minpevent)
         minpevent=pendingTasks;
        for (cEvent* event : events) {
            taskQueue.push(event);
        }
    // }
    //  if(taskQueue.size()>5)
    //  std::cout<<taskQueue.size()<<endl;
    // 唤醒所有工作线程
    workCondition.notify_all();
    // auto laststartTime=startTime;
    // startTime = high_resolution_clock::now();
    // auto duration1 = duration_cast<milliseconds>(startTime-laststartTime);
    // std::cout<<"s"<<duration1.count()<<endl;
    // 等待所有事件完成
    // auto start = std::chrono::high_resolution_clock::now();
    {
        std::unique_lock<std::mutex> lock(globalMutex);
        workCondition.wait(lock, [this] {
            return pendingTasks == 0;
        });
    }
    // auto end = std::chrono::high_resolution_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    //  std::cout << "代码运行时间: " << duration.count() << " 微秒" << std::endl;
    //  std::cout << "代码运行时间: " << duration.count() / 1000.0 << " 毫秒" << std::endl;
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
            // std::cout<<activeWorkers<<endl;
            // 更新状态
            // {
            //     std::lock_guard<std::mutex> lock(globalMutex);
            
              activeWorkers.fetch_sub(1, std::memory_order_relaxed);
            //    pendingTasks.fetch_sub(1, std::memory_order_relaxed);
               int previous = pendingTasks.fetch_sub(1, std::memory_order_acq_rel);
             if (previous == 1) {
                // 需要加锁通知，因为条件变量通知需要在锁内
                std::lock_guard<std::mutex> lock(globalMutex);
                workCondition.notify_all();
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
    std::cout<<concurrentEvents<<endl;
    std::cout<<"min="<<minpevent<<""<<"max="<<maxpevent<<endl;
    std::cout<<"并行执行批次:"<<pcount<<endl;

    std::cout << "\n=== Pending Task Statistics ===" << std::endl;
    std::cout << "Value | Count" << std::endl;
    std::cout << "--------------" << std::endl;
    for (const auto& entry : pendingTaskStats) 
    {
        std::cout << entry.first << "     | " << entry.second << std::endl;
    }
    std::cout << std::endl;
    // std::cout<<i<<"  "<<j<<endl;
    
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
    // {
    // std::lock_guard<std::mutex> lock(globalMutex);

   cSimulation::insertEvent(event);
    // }

}

cModule* cParallelSimulation::findRootNode(cModule*module)
{
    while(module->getParentModule()!=getSimulation()->getSystemModule())
    {
        module=module->getParentModule();
    }
    return module;
}
void cParallelSimulation::recordPendingTaskCount(int count) 
{
    pendingTaskStats[count]++;
}


}  // namespace omnetpp
