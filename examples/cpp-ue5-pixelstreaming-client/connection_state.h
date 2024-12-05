// connection_state.h
#pragma once

#include <atomic>
#include <chrono>

class ConnectionState {
public:
    enum State {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        RECONNECTING
    };

    // Current connection state
    std::atomic<State> state{DISCONNECTED};
    
    // Last successful connection time
    std::chrono::steady_clock::time_point lastConnectedTime;
    
    // Connection attempt count
    std::atomic<int> attemptCount{0};
    
    // Constructor
    ConnectionState() : state(DISCONNECTED), attemptCount(0) {
        lastConnectedTime = std::chrono::steady_clock::now();
    }
};

