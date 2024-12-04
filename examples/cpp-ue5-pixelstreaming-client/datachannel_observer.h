#pragma once
#include <api/data_channel_interface.h>
#include <rtc_base/ref_count.h>
#include <string>

class CustomDataChannelObserver : 
    public webrtc::DataChannelObserver,
    public rtc::RefCountInterface {
public:
    explicit CustomDataChannelObserver(const std::string& streamerId);

    // 声明虚函数，但不实现
    void OnStateChange() override;
    void OnMessage(const webrtc::DataBuffer& buffer) override;
    void OnBufferedAmountChange(uint64_t previous_amount) override;

protected:
    ~CustomDataChannelObserver() override = default;

private:
    std::string m_streamerId;
};