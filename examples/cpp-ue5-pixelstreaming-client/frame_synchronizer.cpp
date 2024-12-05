#include "frame_synchronizer.h"
#include <iostream>

FrameSynchronizer::FrameSynchronizer(const std::vector<std::string>& streamerIds,
                                   size_t queueSize,
                                   uint64_t syncThresholdMs)
    : m_streamerIds(streamerIds)
    , m_maxQueueSize(queueSize)
    , m_syncThresholdMs(syncThresholdMs)
    , m_isRunning(true)
{
    // Initialize frame queues for each streamer
    for (const auto& id : streamerIds) {
        m_frameQueues[id] = std::queue<std::pair<uint64_t, cv::Mat>>();
        m_latestJsonData[id] = "";  // Initialize JSON data
    }
}

FrameSynchronizer::~FrameSynchronizer() {
    stop();
}

void FrameSynchronizer::stop() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_isRunning = false;
    clearQueues();
}

void FrameSynchronizer::clearQueues() {
    for (auto& queue : m_frameQueues) {
        std::queue<std::pair<uint64_t, cv::Mat>> empty;
        std::swap(queue.second, empty);
    }
    m_latestJsonData.clear();
}

void FrameSynchronizer::setCallback(SyncCallback callback) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_callback = callback;
}

void FrameSynchronizer::addJsonData(const std::string& streamerId, const std::string& jsonData) {
    std::unique_lock<std::mutex> lock(m_mutex);
    
    if (!m_isRunning) {
        return;
    }

    // Update JSON data for this streamer
    auto it = m_latestJsonData.find(streamerId);
    if (it != m_latestJsonData.end()) {
        it->second = jsonData;
    }
}

void FrameSynchronizer::addFrame(const std::string& streamerId, 
                               const cv::Mat& frame, 
                               uint64_t timestampUs) {
    std::unique_lock<std::mutex> lock(m_mutex);
    
    if (!m_isRunning) {
        return;
    }

    // Check if this streamer is being tracked
    auto it = m_frameQueues.find(streamerId);
    if (it == m_frameQueues.end()) {
        std::cerr << "Unknown streamer ID: " << streamerId << std::endl;
        return;
    }

    // Add frame to queue
    auto& queue = it->second;
    queue.push({timestampUs, frame.clone()});

    // Remove old frames if queue is too large
    while (queue.size() > m_maxQueueSize) {
        queue.pop();
    }

    // Try to find synchronized frames
    trySync();
}

void FrameSynchronizer::trySync() {
    // Check if we have frames in all queues
    for (const auto& queue : m_frameQueues) {
        if (queue.second.empty()) {
            return;
        }
    }

    // Find the latest timestamp among the oldest frames
    uint64_t latestTimestamp = 0;
    for (const auto& queue : m_frameQueues) {
        latestTimestamp = std::max(latestTimestamp, queue.second.front().first);
    }

    // Check if all frames are within the sync threshold
    bool canSync = true;
    for (const auto& queue : m_frameQueues) {
        uint64_t timestamp = queue.second.front().first;
        uint64_t diffMs = (latestTimestamp - timestamp) / 1000; // Convert to milliseconds
        if (diffMs > m_syncThresholdMs) {
            canSync = false;
            // Remove this outdated frame
            auto& mutableQueue = m_frameQueues[queue.first];
            mutableQueue.pop();
            return;
        }
    }

    if (canSync && m_callback) {
        // Collect synchronized frames
        std::unordered_map<std::string, cv::Mat> syncedFrames;
        for (auto& pair : m_frameQueues) {
            syncedFrames[pair.first] = pair.second.front().second.clone();
            pair.second.pop();
        }

        // Call callback with synchronized frames and current JSON data
        m_callback(syncedFrames, m_latestJsonData);
    }
}