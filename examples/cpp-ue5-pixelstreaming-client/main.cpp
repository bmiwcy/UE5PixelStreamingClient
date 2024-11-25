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

#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QScreen>
#include <QPainter>   
#include <QKeyEvent>
#include "mainwindow.h"

#include "monitors.h"

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
                    std::cout << "Sending value: " << values[i] << std::endl; // Debug output
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
    if (!mainWindow) {
        qDebug() << "Error: mainWindow is null";
        return;
    }

    if (frame.empty()) {
        qDebug() << "Error: Received empty frame for" << QString::fromStdString(streamId);
        return;
    }

    try {
        // Create a copy of the frame to ensure data safety
        cv::Mat frameCopy = frame.clone();
        mainWindow->addFrame(streamId, frameCopy);
    } catch (const std::exception& e) {
        qDebug() << "Exception in onVideoFrameReceived:" << e.what();
    }
}

// A function to handle a single StreamClient instance in a separate thread
void handleStreamer(MainWindow* mainWindow, const std::string& streamerId) {
    vector<IceServer> iceServers = {
        IceServer("stun:stun3.l.google.com:19302"),
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

    // Modify the video frame received callback to pass mainWindow
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

    // Create the main window (use smart pointers to ensure proper destruction)
    std::unique_ptr<MainWindow> mainWindow = std::make_unique<MainWindow>(streamerList);

    // Start the OSC message handling thread
    std::thread oscThread(oscMessageHandler);

    // Launch a separate thread for each streamer
    std::vector<std::thread> streamerThreads;
    for (const auto& streamerId : streamerList) {
        streamerThreads.emplace_back(handleStreamer, mainWindow.get(), streamerId);
    }

    // Wait for the Qt event loop to finish
    int result = app.exec();

    // Clean up resources
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
