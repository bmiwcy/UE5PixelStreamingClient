#include "datachannel_observer.h"
#include <iostream>
#include <stdexcept>

CustomDataChannelObserver::CustomDataChannelObserver(const std::string& streamerId)
    : m_streamerId(streamerId) {
}

void CustomDataChannelObserver::OnStateChange() {
    std::cout << "DataChannel state changed for streamer: " << m_streamerId << std::endl;
}

void CustomDataChannelObserver::OnMessage(const webrtc::DataBuffer& buffer) {
    try {
        if (buffer.data.size() < 1) {
            std::cerr << "Received empty buffer" << std::endl;
            return;
        }

        uint8_t messageId = buffer.data[0];
        
        if (messageId == 123) {  // Transform data message
            const char* data = reinterpret_cast<const char*>(buffer.data.data() + 1);
            std::string jsonStr(data, buffer.data.size() - 1);
            
            // Add JSON data to synchronizer
            if (g_frameSynchronizer) {
                g_frameSynchronizer->addJsonData(m_streamerId, jsonStr);
                std::cout << "Added JSON data from " << m_streamerId << " to synchronizer" << std::endl;
            } else {
                std::cerr << "Warning: Frame synchronizer not initialized" << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error in OnMessage for streamer " << m_streamerId 
                  << ": " << e.what() << std::endl;
    }
}

void CustomDataChannelObserver::OnBufferedAmountChange(uint64_t previous_amount) {
    // Empty implementation
}