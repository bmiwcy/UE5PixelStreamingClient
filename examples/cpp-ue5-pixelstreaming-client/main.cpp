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

// OSC Configuration
#define ADDRESS "192.168.0.165"
#define PORT 8000
#define OUTPUT_BUFFER_SIZE 1024
using OscValue = std::variant<float, std::string, bool, int>;

// Helper function to parse boolean values
bool parseBoolean(const std::string& str) {
    return (str == "true" || str == "1" || str == "yes");
}

// Helper function to check if a string is a number
bool isNumber(const std::string& str) {
    if(str.empty()) return false;
    char* end = nullptr;
    std::strtof(str.c_str(), &end);
    return end != str.c_str() && *end == '\0';
}

// Validate if input values match the required types
bool validateInputTypes(const std::vector<OscValue>& values) {
    if (values.size() < 4) {  // Require at least 4 parameters
        std::cerr << "Error: Need at least 4 parameters (1 int + 3 strings)" << std::endl;
        return false;
    }

    // Check if first parameter is an integer
    if (!std::holds_alternative<int>(values[0])) {
        std::cerr << "Error: First parameter must be an integer" << std::endl;
        return false;
    }

    // Check if next three parameters are strings
    for (int i = 1; i < 4; i++) {
        if (!std::holds_alternative<std::string>(values[i])) {
            std::cerr << "Error: Parameters 2-4 must be strings" << std::endl;
            return false;
        }
    }

    return true;
}

// PixelStreaming Configuration
#define SINGALING_SERVER_ADDRESS "ws://192.168.0.165:80/signaling"

void oscMessageHandler() {
    UdpTransmitSocket transmitSocket(IpEndpointName(ADDRESS, PORT));
    
    while (isRunning) {
        std::cout << "Enter values (1 int + 3 strings): ";
        std::string input;
        
        if (std::getline(std::cin, input)) {
            std::istringstream iss(input);
            std::vector<OscValue> values;
            std::string token;
            
            // Parse each input value
            while (iss.good()) {
                // Check next character
                char next = iss.peek();
                if (next == '\"') {
                    // Read quoted string
                    iss.get(); // Skip opening quote
                    std::getline(iss, token, '\"');
                    iss.get(); // Skip closing quote
                    iss >> std::ws; // Skip whitespace
                    values.push_back(token);
                } else {
                    iss >> token;
                    if (isNumber(token)) {
                        if (values.empty()) {  // First parameter, force as int
                            values.push_back(std::stoi(token));
                        } else {  // Subsequent parameters, treat as string
                            values.push_back(token);
                        }
                    } else {
                        values.push_back(token);
                    }
                }
            }

            // Validate input types
            if (validateInputTypes(values)) {
                char buffer[OUTPUT_BUFFER_SIZE];
                osc::OutboundPacketStream p(buffer, OUTPUT_BUFFER_SIZE);

                p << osc::BeginBundleImmediate
                  << osc::BeginMessage("/unreal/move_vehicle_for_realtime");

                // Send int parameter first
                p << std::get<int>(values[0]);
                //std::cout << "Sending int: " << std::get<int>(values[0]) << std::endl;

                // Then send three string parameters
                for (size_t i = 1; i < 4; ++i) {
                    p << std::get<std::string>(values[i]).c_str();
                    //std::cout << "Sending string: " << std::get<std::string>(values[i]) << std::endl;
                }

                p << osc::EndMessage
                  << osc::EndBundle;

                transmitSocket.Send(p.Data(), p.Size());
            } else {
                std::cerr << "Format: [integer] [string1] [string2] [string3]" << std::endl;
                std::cerr << "Example: 2 left none true" << std::endl;
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

            // Set connection closed callback
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
                retryCount = 0;  // Reset retry counter
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

            // Attempt to connect
            std::cout << "Attempting to connect streamer: " << streamerId << std::endl;
            client->connect();

            // Wait for connection loss or error
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
                    std::this_thread::sleep_for(std::chrono::seconds(5));  // Wait longer before trying again
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

    // Set up command line parser
    QCommandLineParser parser;
    parser.setApplicationDescription("Multi-Stream Video Display Application");
    parser.addHelpOption();
    parser.addVersionOption();

    // Add display mode option
    QCommandLineOption displayModeOption(
        QStringList() << "d" << "display",
        "Display mode (full: fullscreen per monitor, grid: grid layout on primary monitor)",
        "mode",
        "grid"  // Default to grid layout
    );
    parser.addOption(displayModeOption);

    // Add streamer parameter support
    parser.addPositionalArgument("streamers", "Streamer IDs or 'all' for all cameras");

    parser.process(app);

    // Get streamer parameters
    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        std::cerr << "Usage: " << argv[0] << " [--display=grid|full] <streamer_id1> [streamer_id2 ...] or 'all'" << std::endl;
        return 1;
    }

    // Get display mode
    QString displayMode = parser.value(displayModeOption);
    MainWindow::DisplayMode initialMode = 
        (displayMode.toLower() == "full") ? MainWindow::FullScreen : MainWindow::GridLayout;

    std::string StreamerId = "JsonStreamerComponent";

    // Process streamer list
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

    // Create main window (only once)
    std::unique_ptr<MainWindow> mainWindow = std::make_unique<MainWindow>(streamerList, initialMode);

    // Create frame synchronizer
    g_frameSynchronizer = std::make_shared<FrameSynchronizer>(streamerList, 10, 40);

    // Set frame synchronizer callback
    g_frameSynchronizer->setCallback([](
        const std::unordered_map<std::string, cv::Mat>& frames,
        const std::unordered_map<std::string, std::string>& jsonData) {
        // Synchronization debug output code commented out
    });

    // Start OSC message handling thread
    std::thread oscThread(oscMessageHandler);

    // Start independent thread for each streamer
    std::vector<std::thread> streamerThreads;
    for (const auto& streamerId : streamerList) {
        streamerThreads.emplace_back(handleStreamer, mainWindow.get(), streamerId);
    }

    // Show main window
    mainWindow->show();

    // Wait for Qt event loop to end
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
