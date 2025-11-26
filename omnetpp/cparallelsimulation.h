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
#include <map>
#include <utility> // for std::pair
#include <unordered_map>
#include <set>

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



class FloydWarshall {
private:
    std::map<cModule*, int> moduleToIndex;
    std::vector<cModule*> indexToModule;
    std::vector<std::vector<simtime_t>> dist;

public:
    void buildGraph(const std::map<std::pair<cModule*, cModule*>, simtime_t>& minDelayMap) {
        // 收集所有模块
        std::set<cModule*> allModules;
        for (auto& [pair, delay] : minDelayMap) {
            allModules.insert(pair.first);
            allModules.insert(pair.second);
        }

        // 创建索引映射
        int index = 0;
        for (cModule* module : allModules) {
            moduleToIndex[module] = index;
            indexToModule.push_back(module);
            index++;
        }

        int n = allModules.size();
        dist.assign(n, std::vector<simtime_t>(n, SIMTIME_MAX));

        // 初始化距离矩阵
        for (int i = 0; i < n; i++) {
            dist[i][i] = 0;
        }

        // 添加直接连接
        for (auto& [pair, delay] : minDelayMap) {
            int i = moduleToIndex[pair.first];
            int j = moduleToIndex[pair.second];
            dist[i][j] = delay;
            dist[j][i] = delay;
        }

        // Floyd-Warshall算法
        for (int k = 0; k < n; k++) {
            for (int i = 0; i < n; i++) {
                for (int j = 0; j < n; j++) {
                    if (dist[i][k] < SIMTIME_MAX && dist[k][j] < SIMTIME_MAX) {
                        simtime_t newDist = dist[i][k] + dist[k][j];
                        if (newDist < dist[i][j]) {
                            dist[i][j] = newDist;
                        }
                    }
                }
            }
        }
    }

    simtime_t getShortestDelay(cModule* m1, cModule* m2) {
        if (moduleToIndex.find(m1) == moduleToIndex.end() ||
            moduleToIndex.find(m2) == moduleToIndex.end()) {
            return SIMTIME_MAX;
        }
        int i = moduleToIndex[m1];
        int j = moduleToIndex[m2];
        return dist[i][j];
    }

    std::map<std::pair<cModule*, cModule*>, simtime_t> getAllPairsShortestDelay() {
        std::map<std::pair<cModule*, cModule*>, simtime_t> result;
        int n = indexToModule.size();

        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) {
                if (dist[i][j] < SIMTIME_MAX) {
                    auto key = std::make_pair(indexToModule[i], indexToModule[j]);
                    result[key] = dist[i][j];
                }
            }
        }

        return result;
    }
};



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

    cModule*findRootNode(cModule*module);
     
    virtual void distributeAndExecuteEvents(cEvent*yevent);
    virtual void startSimulation();
    virtual void insertEvent(cEvent *event) override;
    
    /**
     * Returns the recommended number of threads for the current system.
     */
    static unsigned int getRecommendedThreadCount();
    //@}
    private:
      struct UndirectedConnection {
      cModule* module1;
      cModule* module2;
      simtime_t delay;

      UndirectedConnection(cModule* m1, cModule* m2, simtime_t d)
          : delay(d) {
          if (m1 < m2) {
              module1 = m1;
              module2 = m2;
          } else {
              module1 = m2;
              module2 = m1;
          }
      }

      bool operator<(const UndirectedConnection& other) const {
          if (module1 != other.module1) return module1 < other.module1;
          if (module2 != other.module2) return module2 < other.module2;
          return delay < other.delay;
      }
   };
   std::set<UndirectedConnection> uniqueConnections;
   std::map<std::pair<cModule*, cModule*>, simtime_t> minDelayMap;
   FloydWarshall fw;

};


}  // namespace omnetpp

#endif