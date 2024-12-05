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
    vector<IceServer> iceServers = {
        IceServer("stun:stun2.l.google.com:19302"),
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

    client->setOnSignalingConnectionOpened([streamerId]() {
        std::cout << "Signaling connection opened for streamer: " << streamerId << std::endl;
    });

    client->setOnClientConnected([streamerId](const Client& client) {
        clientIdToStreamId[client.id()] = streamerId;
    });

    client->setOnClientDisconnected([](const Client& client) {
        clientIdToStreamId.erase(client.id());
        std::cout << "Disconnected client ID: " << client.id() << std::endl;
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

    client->connect();

    while (isRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <streamer_id1> [streamer_id2 ...]" << std::endl;
        return 1;
    }

    // Retrieve the list of streamers from command-line arguments
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

    // Create the main window
    std::unique_ptr<MainWindow> mainWindow = std::make_unique<MainWindow>(streamerList);

    // Create frame synchronizer with 10 frame queue size and 100ms sync threshold
    g_frameSynchronizer = std::make_shared<FrameSynchronizer>(streamerList, 10, 40);

    // Set up frame synchronizer callback
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

    // Start the OSC message handling thread
    std::thread oscThread(oscMessageHandler);

    // Launch a separate thread for each streamer
    std::vector<std::thread> streamerThreads;
    for (const auto& streamerId : streamerList) {
        streamerThreads.emplace_back(handleStreamer, mainWindow.get(), streamerId);
    }

    // Show the main window
    mainWindow->show();

    // Wait for the Qt event loop to finish
    int result = app.exec();

    // Clean up resources
    if (g_frameSynchronizer) {
        g_frameSynchronizer->stop();
    }

    isRunning = false;
    frameAvailable.notify_all();

    // Wait for all threads to finish
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