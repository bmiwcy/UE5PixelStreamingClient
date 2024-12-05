#include "connection_manager.h"
#include <iostream>
#include <cmath>

ConnectionManager::ConnectionManager(const std::string& streamerId)
    : m_streamerId(streamerId) {
}

opentera::WebrtcConfiguration ConnectionManager::createWebRTCConfig() {
    std::vector<opentera::IceServer> iceServers = {
        opentera::IceServer("stun:stun2.l.google.com:19302"),
    };
    return opentera::WebrtcConfiguration::create(iceServers);
}

opentera::SignalingServerConfiguration ConnectionManager::createSignalingConfig() {
    return opentera::SignalingServerConfiguration::create(
        "ws://192.168.0.165:80/signaling",  // Consider making this configurable
        "C++",
        "chat",
        "abc"
    );
}

void ConnectionManager::setupCallbacks(
    opentera::StreamClient* client,
    ConnectionState& state,
    std::mutex& mutex,
    std::condition_variable& cv) {
    
    // Signaling connection callbacks
    client->setOnSignalingConnectionOpened([&, this]() {
        std::lock_guard<std::mutex> lock(mutex);
        std::cout << "Signaling connection opened for streamer: " << m_streamerId << std::endl;
        state.state = ConnectionState::CONNECTED;
        state.lastConnectedTime = std::chrono::steady_clock::now();
        state.attemptCount = 0;
    });

    client->setOnSignalingConnectionClosed([&, this]() {
        std::lock_guard<std::mutex> lock(mutex);
        std::cout << "Signaling connection closed for streamer: " << m_streamerId << std::endl;
        state.state = ConnectionState::DISCONNECTED;
        cv.notify_one();
    });

    client->setOnSignalingConnectionError([&, this](const std::string& error) {
        std::lock_guard<std::mutex> lock(mutex);
        std::cout << "Signaling error for streamer " << m_streamerId << ": " << error << std::endl;
        state.state = ConnectionState::DISCONNECTED;
        cv.notify_one();
    });

    client->setOnClientConnected([&, this](const opentera::Client& client) {
        std::lock_guard<std::mutex> lock(mutex);
        state.state = ConnectionState::CONNECTED;
        std::cout << "Client connected: " << client.id() << " for streamer: " << m_streamerId << std::endl;
    });

    client->setOnClientDisconnected([&, this](const opentera::Client& client) {
        std::lock_guard<std::mutex> lock(mutex);
        state.state = ConnectionState::DISCONNECTED;
        std::cout << "Client disconnected: " << client.id() << " for streamer: " << m_streamerId << std::endl;
        cv.notify_one();
    });
}

int ConnectionManager::calculateRetryDelay(int attemptCount) {
    // Use exponential backoff with a maximum delay
    return std::min(
        INITIAL_RETRY_DELAY_MS * (1 << std::min(attemptCount, 5)),
        MAX_RETRY_DELAY_MS
    );
}