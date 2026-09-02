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
#include <chrono>
#include <algorithm>  
#include "cmodule.h"  
using namespace std::chrono;
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

class CommunityDelayEstimator {
private:
    // 数据结构
    struct Community {
        int id;
        std::unordered_set<cModule*> modules;
        std::unordered_set<cModule*> boundaryNodes;  // 边界节点
        std::map<std::pair<cModule*, cModule*>, simtime_t> internalDistances;  // 社区内距离
        std::unordered_map<cModule*, simtime_t> nodeToCenterDist;  // 节点到社区中心的距离
        cModule* center;  // 社区中心节点
    };

    // 社区相关
    std::vector<Community> communities;
    std::unordered_map<cModule*, int> moduleToCommunity;

    // 社区间距离
    std::vector<std::vector<simtime_t>> interCommunityDist;

    // 缓存
    std::map<std::pair<cModule*, cModule*>, simtime_t> delayCache;
    mutable std::mutex cacheMutex;

    // 最大社区大小
    int maxCommunitySize = 50;

    // 存储有门的模块集合
    std::unordered_set<cModule*> gatedModules;

public:
    void build(const std::map<std::pair<cModule*, cModule*>, simtime_t>& minDelayMap,
               const std::unordered_set<cModule*>& gatedModulesSet) {

        gatedModules = gatedModulesSet;


        std::set<cModule*> allGatedModules;
        for (auto& [pair, _] : minDelayMap) {

            if (gatedModules.find(pair.first) != gatedModules.end()) {
                allGatedModules.insert(pair.first);
            }
            if (gatedModules.find(pair.second) != gatedModules.end()) {
                allGatedModules.insert(pair.second);
            }
        }

        // 3. 社区划分
        clusterModules(allGatedModules, minDelayMap);

        // 4. 识别边界节点
        identifyBoundaryNodes(minDelayMap);

        // 5. 计算每个社区内部距离
        computeIntraCommunityDistances(minDelayMap);

        // 6. 计算社区间距离
        computeInterCommunityDistances(minDelayMap);

        // 7. 构建社区级别全连接
        buildCommunityLevelConnections();
    }


    bool isGatedModule(cModule* module) const {
        return gatedModules.find(module) != gatedModules.end();
    }

    // 获取延迟下界
    simtime_t getLowerBoundDelay(cModule* m1, cModule* m2) {
        if (m1 == m2) return 0;


        if (!isGatedModule(m1) || !isGatedModule(m2)) {
            return SIMTIME_MAX;
        }


        auto key = std::make_pair(m1, m2);
        if (m2 < m1) {
            key = std::make_pair(m2, m1);
        }


        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            auto it = delayCache.find(key);
            if (it != delayCache.end()) {
                return it->second;
            }
        }

        // 计算延迟下界
        simtime_t lowerBound = computeLowerBound(m1, m2);

        // 更新缓存
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            delayCache[key] = lowerBound;
        }

        return lowerBound;
    }

    // 获取所有社区
    const std::vector<Community>& getCommunities() const {
        return communities;
    }

private:
    // 社区划分算法
    void clusterModules(const std::set<cModule*>& allGatedModules,
                       const std::map<std::pair<cModule*, cModule*>, simtime_t>& minDelayMap) {
        std::vector<cModule*> unassigned(allGatedModules.begin(), allGatedModules.end());


        std::sort(unassigned.begin(), unassigned.end(),
                  [](cModule* a, cModule* b) {
                      return a->getId() < b->getId();
                  });

        // 构建邻接表
        std::unordered_map<cModule*, std::vector<std::pair<cModule*, simtime_t>>> adjacency;
        for (const auto& [pair, delay] : minDelayMap) {
            cModule* m1 = pair.first;
            cModule* m2 = pair.second;


            if (gatedModules.find(m1) != gatedModules.end() &&
                gatedModules.find(m2) != gatedModules.end()) {
                adjacency[m1].emplace_back(m2, delay);
                adjacency[m2].emplace_back(m1, delay);
            }
        }

        int communityId = 0;
        std::unordered_set<cModule*> assigned;

        // 基于模块度最大化的简单聚类
        for (cModule* start : unassigned) {
            if (assigned.find(start) != assigned.end()) continue;

            Community community;
            community.id = communityId++;
            community.center = start;

            // BFS扩展社区
            std::queue<cModule*> q;
            q.push(start);
            assigned.insert(start);
            community.modules.insert(start);

            while (!q.empty() && community.modules.size() < maxCommunitySize) {
                cModule* current = q.front();
                q.pop();

                if (adjacency.find(current) != adjacency.end()) {
                    for (const auto& [neighbor, delay] : adjacency[current]) {
                        if (assigned.find(neighbor) == assigned.end() &&
                            community.modules.size() < maxCommunitySize) {
                            q.push(neighbor);
                            assigned.insert(neighbor);
                            community.modules.insert(neighbor);
                        }
                    }
                }
            }

            communities.push_back(community);
        }


        for (int i = 0; i < communities.size(); ++i) {
            for (cModule* module : communities[i].modules) {
                moduleToCommunity[module] = i;
            }
        }
    }

    // 识别边界节点
    void identifyBoundaryNodes(const std::map<std::pair<cModule*, cModule*>, simtime_t>& minDelayMap) {
        for (auto& [pair, delay] : minDelayMap) {
            cModule* m1 = pair.first;
            cModule* m2 = pair.second;


            if (gatedModules.find(m1) == gatedModules.end() ||
                gatedModules.find(m2) == gatedModules.end()) {
                continue;
            }

            int c1 = moduleToCommunity[m1];
            int c2 = moduleToCommunity[m2];

            if (c1 != c2) {
                communities[c1].boundaryNodes.insert(m1);
                communities[c2].boundaryNodes.insert(m2);
            }
        }
    }

    // 计算社区内部距离
    void computeIntraCommunityDistances(const std::map<std::pair<cModule*, cModule*>, simtime_t>& minDelayMap) {
        for (int i = 0; i < communities.size(); ++i) {
            auto& community = communities[i];

            if (community.modules.size() <= 50) {
                // 小社区：计算精确全对最短路径
                computeExactIntraCommunityDistances(community, minDelayMap);
            } else {
                // 大社区：近似计算
                computeApproximateIntraCommunityDistances(community, minDelayMap);
            }
        }
    }

    // 精确计算社区内距离
    void computeExactIntraCommunityDistances(Community& community,
                                           const std::map<std::pair<cModule*, cModule*>, simtime_t>& minDelayMap) {
        std::vector<cModule*> nodes(community.modules.begin(), community.modules.end());
        std::map<cModule*, int> nodeIndex;

        // 构建索引
        for (size_t i = 0; i < nodes.size(); ++i) {
            nodeIndex[nodes[i]] = i;
        }

        // 初始化距离矩阵
        int n = nodes.size();
        std::vector<std::vector<simtime_t>> dist(n,
            std::vector<simtime_t>(n, SIMTIME_MAX));

        for (int i = 0; i < n; ++i) {
            dist[i][i] = 0;
        }

        // 添加社区内的边
        for (const auto& [pair, delay] : minDelayMap) {
            cModule* m1 = pair.first;
            cModule* m2 = pair.second;

            if (community.modules.find(m1) != community.modules.end() &&
                community.modules.find(m2) != community.modules.end()) {
                int i = nodeIndex[m1];
                int j = nodeIndex[m2];
                dist[i][j] = std::min(dist[i][j], delay);
                dist[j][i] = std::min(dist[j][i], delay);
            }
        }

        // Floyd-Warshall
        for (int k = 0; k < n; ++k) {
            for (int i = 0; i < n; ++i) {
                for (int j = 0; j < n; ++j) {
                    if (dist[i][k] < SIMTIME_MAX && dist[k][j] < SIMTIME_MAX) {
                        simtime_t newDist = dist[i][k] + dist[k][j];
                        if (newDist < dist[i][j]) {
                            dist[i][j] = newDist;
                        }
                    }
                }
            }
        }

        // 存储结果
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                if (dist[i][j] < SIMTIME_MAX) {
                    auto key = std::make_pair(nodes[i], nodes[j]);
                    community.internalDistances[key] = dist[i][j];
                }
            }
        }

        for (int i = 0; i < n; ++i) {
        cModule* node = nodes[i];
        simtime_t minDistToBoundary = SIMTIME_MAX;
        
        // 如果是边界节点，距离为0
        if (community.boundaryNodes.find(node) != community.boundaryNodes.end()) {
            community.nodeToCenterDist[node] = 0;
            continue;
        }
        
        // 遍历所有边界节点，找到最小距离
        for (cModule* boundaryNode : community.boundaryNodes) {
            int boundaryIdx = nodeIndex[boundaryNode];
            if (dist[i][boundaryIdx] < SIMTIME_MAX) {
                minDistToBoundary = std::min(minDistToBoundary, dist[i][boundaryIdx]);
            }
        }
        
        if (minDistToBoundary < SIMTIME_MAX) {
            community.nodeToCenterDist[node] = minDistToBoundary;
        }
    }
    }

    // 近似计算社区内距离
    void computeApproximateIntraCommunityDistances(Community& community,
                                                 const std::map<std::pair<cModule*, cModule*>, simtime_t>& minDelayMap) {
        // 计算到边界的距离
        for (cModule* module : community.modules) {
            simtime_t minDistToBoundary = SIMTIME_MAX;

            for (cModule* boundary : community.boundaryNodes) {
                if (module == boundary) {
                    minDistToBoundary = 0;
                    break;
                }

                // 尝试获取直接距离
                auto it = minDelayMap.find(std::make_pair(module, boundary));
                if (it != minDelayMap.end()) {
                    minDistToBoundary = std::min(minDistToBoundary, it->second);
                }
            }

            if (minDistToBoundary < SIMTIME_MAX) {
                community.nodeToCenterDist[module] = minDistToBoundary;
            }
        }

        // 使用边界节点作为距离估计
        std::vector<cModule*> boundaryList(community.boundaryNodes.begin(),
                                           community.boundaryNodes.end());
        int b = boundaryList.size();

        // 边界节点间的距离
        std::vector<std::vector<simtime_t>> boundaryDist(b,
            std::vector<simtime_t>(b, SIMTIME_MAX));

        for (int i = 0; i < b; ++i) {
            boundaryDist[i][i] = 0;
        }

        for (int i = 0; i < b; ++i) {
            for (int j = i + 1; j < b; ++j) {
                auto it = minDelayMap.find(std::make_pair(boundaryList[i], boundaryList[j]));
                if (it != minDelayMap.end()) {
                    boundaryDist[i][j] = it->second;
                    boundaryDist[j][i] = it->second;
                }
            }
        }

        // 计算边界节点最短路径
        for (int k = 0; k < b; ++k) {
            for (int i = 0; i < b; ++i) {
                for (int j = 0; j < b; ++j) {
                    if (boundaryDist[i][k] < SIMTIME_MAX && boundaryDist[k][j] < SIMTIME_MAX) {
                        simtime_t newDist = boundaryDist[i][k] + boundaryDist[k][j];
                        if (newDist < boundaryDist[i][j]) {
                            boundaryDist[i][j] = newDist;
                        }
                    }
                }
            }
        }
    }
    // 计算社区间最小延迟
        void computeInterCommunityDistances(const std::map<std::pair<cModule*, cModule*>, simtime_t>& minDelayMap) {
            int numCommunities = communities.size();
            interCommunityDist.assign(numCommunities,
                std::vector<simtime_t>(numCommunities, SIMTIME_MAX));

            for (int i = 0; i < numCommunities; ++i) {
                interCommunityDist[i][i] = 0;
            }

            // 初始化直接连接
            for (const auto& [pair, delay] : minDelayMap) {
                cModule* m1 = pair.first;
                cModule* m2 = pair.second;


                if (gatedModules.find(m1) == gatedModules.end() ||
                    gatedModules.find(m2) == gatedModules.end()) {
                    continue;
                }

                int c1 = moduleToCommunity[m1];
                int c2 = moduleToCommunity[m2];

                if (c1 != c2) {
                    interCommunityDist[c1][c2] = std::min(interCommunityDist[c1][c2], delay);
                    interCommunityDist[c2][c1] = std::min(interCommunityDist[c2][c1], delay);
                }
            }
        }

        // 构建社区级别全连接
        void buildCommunityLevelConnections() {
            int n = communities.size();

            // Floyd-Warshall在社区级别
            for (int k = 0; k < n; ++k) {
                for (int i = 0; i < n; ++i) {
                    for (int j = 0; j < n; ++j) {
                        if (interCommunityDist[i][k] < SIMTIME_MAX &&
                            interCommunityDist[k][j] < SIMTIME_MAX) {
                            simtime_t newDist = interCommunityDist[i][k] + interCommunityDist[k][j];
                            if (newDist < interCommunityDist[i][j]) {
                                interCommunityDist[i][j] = newDist;
                            }
                        }
                    }
                }
            }
        }

        // 计算延迟下界
        simtime_t computeLowerBound(cModule* m1, cModule* m2) {
            int c1 = moduleToCommunity[m1];
            int c2 = moduleToCommunity[m2];

            // 同一社区
            if (c1 == c2) {
                return getIntraCommunityDistance(m1, m2, c1);
            }

            // 不同社区
            const auto& community1 = communities[c1];
            const auto& community2 = communities[c2];

            // 获取社区间距离
            simtime_t communityDist = interCommunityDist[c1][c2];
            if (communityDist >= SIMTIME_MAX) {
                return SIMTIME_MAX;
            }

            // 计算m1到社区1边界的距离
            simtime_t distToBoundary1 = getDistanceToBoundary(m1, c1);

            // 计算m2到社区2边界的距离
            simtime_t distToBoundary2 = getDistanceToBoundary(m2, c2);

            // 下界公式
            return distToBoundary1 + communityDist + distToBoundary2;
        }

        // 获取社区内距离
        simtime_t getIntraCommunityDistance(cModule* m1, cModule* m2, int communityId) {
            const auto& community = communities[communityId];

            // 小社区：查表
            if (community.modules.size() <= 50) {
                auto key = std::make_pair(m1, m2);
                // if (m2 < m1) {
                //     key = std::make_pair(m2, m1);
                // }

                auto it = community.internalDistances.find(key);
                if (it != community.internalDistances.end()) {
                    return it->second;
                }
                key=std::make_pair(m2, m1);
                it = community.internalDistances.find(key);
                if (it != community.internalDistances.end()) {
                    return it->second;
                }
            }

            // 大社区：近似计算
            if (community.nodeToCenterDist.find(m1) != community.nodeToCenterDist.end() &&
                community.nodeToCenterDist.find(m2) != community.nodeToCenterDist.end()) {
                // 通过社区中心估算
                return community.nodeToCenterDist.at(m1) + community.nodeToCenterDist.at(m2);
            }

            // 通过边界节点估算
            return getDistanceToBoundary(m1, communityId) + getDistanceToBoundary(m2, communityId);
        }

        // 获取节点到社区边界的距离
        simtime_t getDistanceToBoundary(cModule* module, int communityId) {
            const auto& community = communities[communityId];

            // 如果是边界节点，距离为0
            if (community.boundaryNodes.find(module) != community.boundaryNodes.end()) {
                return 0;
            }

            // 返回节点到边界的距离
            auto it = community.nodeToCenterDist.find(module);
            if (it != community.nodeToCenterDist.end()) {
                return it->second;
            }

            // 如果没有计算，返回保守估计
            return SIMTIME_MAX / 4;
        }
    };
class Dijkstra {
private:
    std::map<cModule*, int> moduleToIndex;
    std::vector<cModule*> indexToModule;
    std::vector<std::vector<simtime_t>> dist;
    std::vector<std::vector<std::pair<int, simtime_t>>> adjList;
    std::map<std::pair<cModule*, cModule*>, simtime_t> result;
    
    // Dijkstra算法实现
    void dijkstra(int src, std::vector<simtime_t>& distances) {
        int n = indexToModule.size();
        std::vector<bool> visited(n, false);
        
        // 优先队列，存储{距离, 节点索引}
        std::priority_queue<std::pair<simtime_t, int>, 
                           std::vector<std::pair<simtime_t, int>>, 
                           std::greater<std::pair<simtime_t, int>>> pq;
        
        distances.assign(n, SIMTIME_MAX);
        distances[src] = 0;
        pq.push({0, src});
        
        while (!pq.empty()) {
            auto [currentDist, u] = pq.top();
            pq.pop();
            
            if (visited[u]) continue;
            visited[u] = true;
            
            // 遍历所有邻居
            for (const auto& [v, weight] : adjList[u]) {
                if (currentDist < SIMTIME_MAX && weight < SIMTIME_MAX) {
                    simtime_t newDist = currentDist + weight;
                    
                    if (newDist < distances[v]) {
                        distances[v] = newDist;
                        pq.push({newDist, v});
                    }
                }
            }
        }
    }
    
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
        adjList.resize(n);
        dist.assign(n, std::vector<simtime_t>(n, SIMTIME_MAX));
        
        // 构建邻接表
        for (auto& [pair, delay] : minDelayMap) {
            int i = moduleToIndex[pair.first];
            int j = moduleToIndex[pair.second];
            
            // 添加双向边
            adjList[i].emplace_back(j, delay);
            adjList[j].emplace_back(i, delay);
        }
        
        // 为每个节点运行Dijkstra算法
        for (int i = 0; i < n; i++) {
            std::vector<simtime_t> distances;
            dijkstra(i, distances);
            dist[i] = distances;
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
        result.clear();
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
class FloydWarshall {
private:
    std::map<cModule*, int> moduleToIndex;
    std::vector<cModule*> indexToModule;
    std::vector<std::vector<simtime_t>> dist;
    std::map<std::pair<cModule*, cModule*>, simtime_t> result;
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
    high_resolution_clock::time_point startTime;
    high_resolution_clock::time_point endTime;
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
    int maxpevent=0;
    int minpevent=10000;
    int pcount=0;
    std::mutex startMutex;
    std::condition_variable startCondition;
    bool workersShouldStart = false;
    enum class ThreadPoolState {
        CREATED,    // 线程已创建但未启动
        RUNNING,    // 线程正在运行
        STOPPED     // 线程已停止
    };
    int i;
    int j;    
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
      enum D_algorithm
      {
       RP=0,
       RP_READY=1,
       FW=2,
       FW_READY=3,
       D=4,
       D_READY=5
      };
      D_algorithm al;
      struct UndirectedConnection 
      {
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
   CommunityDelayEstimator estimator;
   Dijkstra d;
   simtime_t getConnectionDelay(cModule* m1, cModule* m2) {
        if (m1 == nullptr || m2 == nullptr) {
            throw std::invalid_argument("模块指针不能为空");
        }
        if (m1 == m2) {
            return SIMTIME_ZERO;
        }
        for (auto it = uniqueConnections.begin(); it != uniqueConnections.end(); ++it) 
        {
          const auto& conn = *it;
          if((conn.module1->getId()==m1->getId()&&conn.module2->getId()==m2->getId())||(conn.module1->getId()==m2->getId()&&conn.module2->getId()==m1->getId()))
          {
            return conn.delay;
          }
        } 
        throw std::runtime_error("模块之间无连接");
    }
    std::map<int, long> pendingTaskStats;
    void recordPendingTaskCount(int count);
    public:
    void setAlgorithm(int t) {
        al = static_cast<D_algorithm>(t);
    }
};


}  // namespace omnetpp

#endif