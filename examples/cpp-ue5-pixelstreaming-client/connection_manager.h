#pragma once

#include <OpenteraWebrtcNativeClient/StreamClient.h>
#include <mutex>
#include <condition_variable>
#include <string>
#include <memory>
#include "connection_state.h"

class ConnectionManager {
public:
    static constexpr int MAX_RETRY_COUNT = 30000;
    static constexpr int INITIAL_RETRY_DELAY_MS = 1000;
    static constexpr int MAX_RETRY_DELAY_MS = 30000;

    ConnectionManager(const std::string& streamerId);
    
    // Create WebRTC configuration
    opentera::WebrtcConfiguration createWebRTCConfig();
    
    // Create signaling configuration
    opentera::SignalingServerConfiguration createSignalingConfig();
    
    // Setup callbacks for the StreamClient
    void setupCallbacks(
        opentera::StreamClient* client,
        ConnectionState& state,
        std::mutex& mutex,
        std::condition_variable& cv
    );
    
    // Calculate retry delay using exponential backoff
    int calculateRetryDelay(int attemptCount);
    
    const std::string& getStreamerId() const { return m_streamerId; }

private:
    std::string m_streamerId;
};