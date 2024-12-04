#include "datachannel_observer.h"
#include <iostream>

CustomDataChannelObserver::CustomDataChannelObserver(const std::string& streamerId)
    : m_streamerId(streamerId) {
}

void CustomDataChannelObserver::OnStateChange() {
    std::cout << "DataChannel state changed for streamer: " << m_streamerId << std::endl;
}

void CustomDataChannelObserver::OnMessage(const webrtc::DataBuffer& buffer) {
    std::string data(
        reinterpret_cast<const char*>(buffer.data.data()), 
        buffer.data.size()
    );
    std::cout << "Received data from " << m_streamerId << ": " << data << std::endl;
}

void CustomDataChannelObserver::OnBufferedAmountChange(uint64_t previous_amount) {
    // 空实现
}