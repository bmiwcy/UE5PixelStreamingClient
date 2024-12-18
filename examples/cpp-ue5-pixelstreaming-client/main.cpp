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
#include <chrono>

#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QScreen>
#include <QPainter>   
#include <QKeyEvent>
#include <QCommandLineParser>
#include <QCommandLineOption>

#include "mainwindow.h"
#include "monitors.h"
#include "datachannel_observer.h"
#include "frame_synchronizer.h"

using namespace opentera;
using namespace std;

// Global variables
std::unordered_map<std::string, std::string> clientIdToStreamId;
std::mutex frameMutex;
std::condition_variable frameAvailable;
std::unordered_map<std::string, std::queue<cv::Mat>> frameQueues;
std::atomic<bool> isRunning{true};
std::shared_ptr<FrameSynchronizer> g_frameSynchronizer;

// OSC 
#define ADDRESS "192.168.0.165"
#define PORT 8000
#define OUTPUT_BUFFER_SIZE 1024
// PixelStreaming
#define SINGALING_SERVER_ADDRESS "ws://192.168.0.165:80/signaling"

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
                    std::cout << "Sending value: " << values[i] << std::endl;
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

void onVideoFrameReceived(MainWindow* mainWindow, const std::string& streamId, const cv::Mat& frame, uint64_t timestampUs)
{
    if (!mainWindow || frame.empty()) {
        return;
    }

    try {
        // Create a copy of the frame for display
        cv::Mat frameCopy = frame.clone();
        mainWindow->addFrame(streamId, frameCopy);

        // Add frame to synchronizer
        if (g_frameSynchronizer) {
            g_frameSynchronizer->addFrame(streamId, frame, timestampUs);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error in onVideoFrameReceived: " << e.what() << std::endl;
    }
}

void handleStreamer(MainWindow* mainWindow, const std::string& streamerId) {
    const int MAX_RETRY_COUNT = 3;    
    const int RETRY_DELAY_MS = 1000;  
    
    while (isRunning) {
        try {
            vector<IceServer> iceServers = {
                //IceServer("stun:stun2.l.google.com:19302"),
                //IceServer("turn:192.168.0.165:3478", "webrtc", "ue5test")
            };
            auto webrtcConfig = WebrtcConfiguration::create({iceServers});
            auto signalingServerConfiguration = SignalingServerConfiguration::create(
                SINGALING_SERVER_ADDRESS, "C++", "chat", "abc");

            auto client = std::make_unique<StreamClient>(
                signalingServerConfiguration,
                webrtcConfig,
                VideoStreamConfiguration::create(),
                std::vector<std::string>{streamerId},
                streamerId);

            bool connectionLost = false;
            std::mutex reconnectMutex;
            std::condition_variable reconnectCV;
            int retryCount = 0;

            // 设置连接断开回调
            client->setOnSignalingConnectionClosed([&]() {
                std::cout << "Signaling connection closed for streamer: " << streamerId << std::endl;
                std::lock_guard<std::mutex> lock(reconnectMutex);
                connectionLost = true;
                reconnectCV.notify_one();
            });

            client->setOnSignalingConnectionError([&](const std::string& error) {
                std::cout << "Signaling connection error for streamer: " << streamerId 
                         << " Error: " << error << std::endl;
                std::lock_guard<std::mutex> lock(reconnectMutex);
                connectionLost = true;
                reconnectCV.notify_one();
            });

            client->setOnSignalingConnectionOpened([&, streamerId]() {
                std::cout << "Signaling connection opened for streamer: " << streamerId << std::endl;
                std::lock_guard<std::mutex> lock(reconnectMutex);
                retryCount = 0;  // 重置重试计数
            });

            client->setOnClientConnected([streamerId](const Client& client) {
                clientIdToStreamId[client.id()] = streamerId;
                std::cout << "Client connected: " << client.id() << " for streamer: " << streamerId << std::endl;
            });

            client->setOnClientDisconnected([&](const Client& client) {
                clientIdToStreamId.erase(client.id());
                std::cout << "Disconnected client ID: " << client.id() << std::endl;
                std::lock_guard<std::mutex> lock(reconnectMutex);
                connectionLost = true;
                reconnectCV.notify_one();
            });

            client->setOnDataChannelOpened([streamerId](const Client& client, 
                rtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel) {
                std::cout << "DataChannel opened for streamer: " << streamerId << std::endl;
                auto observer = new rtc::RefCountedObject<CustomDataChannelObserver>(streamerId);
                dataChannel->RegisterObserver(observer);
            });

            client->setOnVideoFrameReceived(
                [mainWindow, streamerId](const Client& client, const cv::Mat& frame, uint64_t timestampUs) {
                    onVideoFrameReceived(mainWindow, streamerId, frame, timestampUs);
                });

            // 尝试连接
            std::cout << "Attempting to connect streamer: " << streamerId << std::endl;
            client->connect();

            // 等待连接断开或错误
            std::unique_lock<std::mutex> lock(reconnectMutex);
            while (!connectionLost && isRunning) {
                reconnectCV.wait_for(lock, std::chrono::milliseconds(100));
            }

            if (connectionLost && isRunning) {
                retryCount++;
                std::cout << "Connection lost for streamer: " << streamerId 
                         << " (Attempt " << retryCount << " of " << MAX_RETRY_COUNT << ")"
                         << std::endl;
                
                if (retryCount < MAX_RETRY_COUNT) {
                    std::cout << "Retrying in " << RETRY_DELAY_MS/1000.0 << " seconds..." << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
                    continue;
                } else {
                    std::cout << "Max retry attempts reached for streamer: " << streamerId << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(5));  // 等待较长时间后继续尝试
                    retryCount = 0;
                }
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error in handleStreamer for " << streamerId 
                     << ": " << e.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
        }
    }
}

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "xcb");
    QApplication app(argc, argv);

    // 设置命令行解析器
    QCommandLineParser parser;
    parser.setApplicationDescription("Multi-Stream Video Display Application");
    parser.addHelpOption();
    parser.addVersionOption();

    // 添加显示模式选项
    QCommandLineOption displayModeOption(
        QStringList() << "d" << "display",
        "Display mode (full: fullscreen per monitor, grid: grid layout on primary monitor)",
        "mode",
        "grid"  // 默认使用网格布局
    );
    parser.addOption(displayModeOption);

    // 添加streamer参数支持
    parser.addPositionalArgument("streamers", "Streamer IDs or 'all' for all cameras");

    parser.process(app);

    // 获取streamer参数
    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        std::cerr << "Usage: " << argv[0] << " [--display=grid|full] <streamer_id1> [streamer_id2 ...] or 'all'" << std::endl;
        return 1;
    }

    // 获取显示模式
    QString displayMode = parser.value(displayModeOption);
    MainWindow::DisplayMode initialMode = 
        (displayMode.toLower() == "full") ? MainWindow::FullScreen : MainWindow::GridLayout;

    std::string StreamerId = "JsonStreamerComponent";

    // 处理streamer列表
    std::vector<std::string> streamerList;
    if (args[0] == "all") {
        streamerList = {
            "Camera01_Default", "Camera02_Default", "Camera03_Default", "Camera04_Default",
            "Camera05_Fisheye", "Camera06_Fisheye", "Camera07_Fisheye", "Camera08_Fisheye"
        };
    } else {
        for (const QString& arg : args) {
            streamerList.emplace_back(arg.toStdString());
        }
    }

    std::string defaultStreamerId = "JsonStreamerComponent";
    streamerList.push_back(defaultStreamerId);

    // 创建主窗口（只创建一次）
    std::unique_ptr<MainWindow> mainWindow = std::make_unique<MainWindow>(streamerList, initialMode);

    // 创建帧同步器
    g_frameSynchronizer = std::make_shared<FrameSynchronizer>(streamerList, 10, 40);

    // 设置帧同步器回调
    g_frameSynchronizer->setCallback([](
        const std::unordered_map<std::string, cv::Mat>& frames,
        const std::unordered_map<std::string, std::string>& jsonData) {
        std::cout << "\n=== Synchronized Data ===" << std::endl;
        
        // Print frame information
        std::cout << "Frame sizes:" << std::endl;
        for (const auto& [streamerId, frame] : frames) {
            std::cout << "Camera " << streamerId 
                     << " - Size: " << frame.size().width 
                     << "x" << frame.size().height 
                     << " channels: " << frame.channels() << std::endl;
        }
        
        // Print JSON data
        std::cout << "\nJSON data:" << std::endl;
        for (const auto& [streamerId, json] : jsonData) {
            if (!json.empty()) {
                std::cout << "Camera " << streamerId 
                         << " - JSON: " << json << std::endl;
            }
        }
        
        std::cout << "========================\n" << std::endl;
    });

    // 启动OSC消息处理线程
    std::thread oscThread(oscMessageHandler);

    // 为每个streamer启动独立线程
    std::vector<std::thread> streamerThreads;
    for (const auto& streamerId : streamerList) {
        streamerThreads.emplace_back(handleStreamer, mainWindow.get(), streamerId);
    }

    // 显示主窗口
    mainWindow->show();

    // 等待Qt事件循环结束
    int result = app.exec();

    // 清理资源
    if (g_frameSynchronizer) {
        g_frameSynchronizer->stop();
    }

    isRunning = false;
    frameAvailable.notify_all();

    // 等待所有线程结束
    for (auto& t : streamerThreads) {
        if (t.joinable()) {
            t.join();
        }
    }

    if (oscThread.joinable()) {
        oscThread.join();
    }

    return result;
}


