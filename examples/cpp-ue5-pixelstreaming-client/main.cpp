#include <OpenteraWebrtcNativeClient/StreamClient.h>
#include <api/peer_connection_interface.h>
#include <rtc_base/ref_counted_object.h>
#include <OpenteraWebrtcNativeClient/Handlers/PeerConnectionHandler.h>
#include <OpenteraWebrtcNativeClient/Configurations/SignalingServerConfiguration.h>
#include <OpenteraWebrtcNativeClient/Signaling/SignalingClient.h>
#include <OpenteraWebrtcNativeClient/Signaling/UE5PixelStreamingSignalingClient.h>

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>

#include "oscpack/osc/OscOutboundPacketStream.h"
#include "oscpack/ip/UdpSocket.h"

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>

using namespace opentera;
using namespace std;

// Global variables
std::unordered_map<std::string, std::string> clientIdToStreamId;
std::mutex frameMutex;
std::condition_variable frameAvailable;
std::unordered_map<std::string, std::queue<cv::Mat>> frameQueues;
std::atomic<bool> isRunning{true};

// OSC 
#define ADDRESS "192.168.0.165"
#define PORT 8000
#define OUTPUT_BUFFER_SIZE 1024

void oscMessageHandler() {
    UdpTransmitSocket transmitSocket(IpEndpointName(ADDRESS, PORT));
    
    while (isRunning) {
        std::cout << "Enter 6 coordinates (separated by spaces): ";
        std::string input;
        
        if (std::getline(std::cin, input)) {
            std::istringstream iss(input);
            float values[6];
            bool validInput = true;

            for (int i = 0; i < 6; ++i) {
                int tempValue;
                if (iss >> tempValue) {
                    values[i] = static_cast<float>(tempValue);
                } else {
                    validInput = false;
                    break;
                }
            }

            if (validInput) {
                char buffer[OUTPUT_BUFFER_SIZE];
                osc::OutboundPacketStream p(buffer, OUTPUT_BUFFER_SIZE);

                p << osc::BeginBundleImmediate
                  << osc::BeginMessage("/unreal/move");

                for (int i = 0; i < 6; ++i) {
                    p << values[i];
                    std::cout << "Sending value: " << values[i] << std::endl; // 调试输出
                }

                p << osc::EndMessage
                  << osc::EndBundle;

                transmitSocket.Send(p.Data(), p.Size());
            } else {
                std::cerr << "Invalid input. Please enter 6 integer values." << std::endl;
            }
        } else {
            break;
        }
    }
}



// Function to display video frames, creating a window for each streamId
void displayThread(const std::vector<std::string>& streamerList)
{
    // 为每个 streamId 创建窗口
    for (const auto& streamId : streamerList)
    {
        cv::namedWindow(streamId, cv::WINDOW_AUTOSIZE);
    }

    while (isRunning)
    {
        std::unique_lock<std::mutex> lock(frameMutex);
        frameAvailable.wait(lock, [] { return !frameQueues.empty() || !isRunning; });

        for (auto& [streamId, queue] : frameQueues)
        {
            // 保留队列中的最新帧
            if (queue.size() > 1)
            {
                while (queue.size() > 1)
                {
                    queue.pop();  // 移除旧帧，只保留最新帧
                }
            }

            if (!queue.empty())
            {
                cv::Mat frame = queue.front();
                queue.pop();

                lock.unlock();  // 解锁以便其他线程继续处理

                if (!frame.empty())
                {
                    cv::imshow(streamId, frame);  // 在对应的窗口中显示帧
                    cv::waitKey(10);  // 增加等待时间，降低刷新率
                }

                lock.lock();  // 重新上锁
            }
        }
    }

    // 销毁所有窗口
    for (const auto& streamId : streamerList)
    {
        cv::destroyWindow(streamId);
    }
}


// Callback function to receive video frames and push frames into the corresponding queue
void onVideoFrameReceived(const std::string& streamId, const cv::Mat& frame, uint64_t timestampUs)
{
    std::lock_guard<std::mutex> lock(frameMutex);
    frameQueues[streamId].push(frame);  // Use streamId as the key for the queue
    frameAvailable.notify_one();
}

// A function to handle a single StreamClient instance in a separate thread
void handleStreamer(const std::string& streamerId) {
    // 配置 ICE Servers 和信令服务器
    vector<IceServer> iceServers = {
        //IceServer("stun:stun.l.google.com:19302"),
        IceServer("stun:stun3.l.google.com:19302"),
        //IceServer("turn:192.168.0.165:3478", "webrtc", "ue5test")
        };
    auto webrtcConfig = WebrtcConfiguration::create({iceServers});
    auto signalingServerConfiguration = SignalingServerConfiguration::create(
        "ws://192.168.0.165:80/signaling", "C++", "chat", "abc");

    // 创建 StreamClient 实例并关联 streamId
    auto client = std::make_unique<StreamClient>(
        signalingServerConfiguration,
        webrtcConfig,
        VideoStreamConfiguration::create(),
        std::vector<std::string>{streamerId},
        streamerId);

    client->setOnSignalingConnectionOpened([streamerId]() {
        std::cout << "Signaling connection opened for streamer: " << streamerId << std::endl;
    });

    client->setOnClientConnected([streamerId](const Client& client) {
        clientIdToStreamId[client.id()] = streamerId;
        //std::cout << "Connected to streamer: " << streamerId << " with Client ID: " << client.id() << std::endl;
    });

    client->setOnClientDisconnected([](const Client& client) {
        clientIdToStreamId.erase(client.id());
        std::cout << "Disconnected client ID: " << client.id() << std::endl;
    });

    // 设置视频帧接收回调，使用 Lambda 绑定 streamerId
    client->setOnVideoFrameReceived(
        [streamerId](const Client& client, const cv::Mat& frame, uint64_t timestampUs) {
            onVideoFrameReceived(streamerId, frame, timestampUs);
        });

    // 连接信令服务器
    client->connect();

    // 保持线程运行直到程序退出
    while (isRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main(int argc, char* argv[]) {
    //UdpTransmitSocket transmitSocket(IpEndpointName(ADDRESS, PORT));
    //std::string input;
    //std::cout << "Enter a message to send: ";
    //std::getline(std::cin, input);

    //char buffer[OUTPUT_BUFFER_SIZE];
    //osc::OutboundPacketStream p(buffer, OUTPUT_BUFFER_SIZE);

    //p << osc::BeginBundleImmediate
    //        << osc::BeginMessage("/unreal/move")
    //        << input.c_str() << osc::EndMessage
    //        << osc::EndBundle;

    //    transmitSocket.Send(p.Data(), p.Size());

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <streamer1> <streamer2> ..." << std::endl;
        return 1;
    }

    // 获取命令行参数中的 streamer 列表
    std::vector<std::string> streamerList;
    if (std::string(argv[1]) == "all") {
        streamerList = {
            "Camera01_Default", "Camera02_Default", "Camera03_Default", "Camera04_Default",
            "Camera05_Fisheye", "Camera06_Fisheye", "Camera07_Fisheye", "Camera08_Fisheye"
        };
    } else {
        for (int i = 1; i < argc; ++i) {
            streamerList.emplace_back(argv[i]);
        }
    }
    // 启动 OSC 消息处理线程
    std::thread oscThread(oscMessageHandler);

    // 启动显示线程
    std::thread display(displayThread, streamerList);

    // 启动每个 streamer 的独立线程
    std::vector<std::thread> streamerThreads;
    for (const auto& streamerId : streamerList) {
        streamerThreads.emplace_back(handleStreamer, streamerId);
    }

    //// 等待用户输入以退出程序
    //std::cin.get();

    display.join();
    // 停止所有线程
    isRunning = false;
    frameAvailable.notify_all();

    for (auto& t : streamerThreads) {
        if (t.joinable()) {
            t.join();
        }
    }

    return 0;
}