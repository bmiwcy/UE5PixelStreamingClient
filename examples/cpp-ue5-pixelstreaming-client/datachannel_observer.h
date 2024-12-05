#pragma once
#include <api/data_channel_interface.h>
#include <rtc_base/ref_count.h>
#include <string>
#include "frame_synchronizer.h"

// Forward declaration of global frame synchronizer
extern std::shared_ptr<FrameSynchronizer> g_frameSynchronizer;

class CustomDataChannelObserver : 
    public webrtc::DataChannelObserver,
    public rtc::RefCountInterface {
public:
    explicit CustomDataChannelObserver(const std::string& streamerId);

    // Virtual functions declarations
    void OnStateChange() override;
    void OnMessage(const webrtc::DataBuffer& buffer) override;
    void OnBufferedAmountChange(uint64_t previous_amount) override;

    struct MessageHeader {
        uint8_t messageType;    
        uint32_t payloadLength;
    
        static MessageHeader Parse(const uint8_t* data) {
            MessageHeader header;
            if (!data) {
                throw std::runtime_error("Invalid data pointer for header parsing");
            }
            header.messageType = data[0];
            memcpy(&header.payloadLength, data + 1, sizeof(uint32_t));
            return header;
        }
    };

protected:
    ~CustomDataChannelObserver() override = default;

private:
    std::string m_streamerId;
};