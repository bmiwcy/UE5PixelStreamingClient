#pragma once

#include <opencv2/core.hpp>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <memory>
#include <vector>
#include <string>

class FrameSynchronizer {
public:
    // Define callback type for synchronized frames and JSON data
    using SyncCallback = std::function<void(
        const std::unordered_map<std::string, cv::Mat>&,
        const std::unordered_map<std::string, std::string>&
    )>;
    
    // Constructor
    FrameSynchronizer(const std::vector<std::string>& streamerIds, 
                     size_t queueSize = 10,
                     uint64_t syncThresholdMs = 100);
    
    // Destructor
    ~FrameSynchronizer();

    // Add a new frame from a specific streamer
    void addFrame(const std::string& streamerId, const cv::Mat& frame, uint64_t timestampUs);

    // Add JSON data from a specific streamer
    void addJsonData(const std::string& streamerId, const std::string& jsonData);

    // Set callback for synchronized data
    void setCallback(SyncCallback callback);

    // Stop synchronizer
    void stop();

private:
    // Internal synchronization check
    void trySync();

    // Clear all queues
    void clearQueues();

private:
    std::vector<std::string> m_streamerIds;
    std::unordered_map<std::string, std::queue<std::pair<uint64_t, cv::Mat>>> m_frameQueues;
    std::unordered_map<std::string, std::string> m_latestJsonData;  // Latest JSON data for each streamer
    size_t m_maxQueueSize;
    uint64_t m_syncThresholdMs;
    std::mutex m_mutex;
    SyncCallback m_callback;
    bool m_isRunning;
};