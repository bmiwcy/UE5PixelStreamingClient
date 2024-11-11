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

#include <cmath>
#include <iostream>
#include <thread>
#include <vector>
#include <cstdlib>

using namespace opentera;
using namespace std;

std::queue<cv::Mat> frameQueue;
std::mutex queueMutex;
std::condition_variable frameAvailable;
std::atomic<bool> isRunning(true);

void onVideoFrameReceived(const Client& client, const cv::Mat& frame, uint64_t timestampUs)
{
    cout << "Received video frame from client: " << client.name() << " at timestamp " << timestampUs << endl;
    cv::imshow("Video Frame", frame);  // 显示视频帧
    cv::waitKey(1);  // 等待一毫秒
}


class CvVideoCaptureVideoSource : public VideoSource
{
    atomic_bool m_stopped;
    thread m_thread;
    string m_path;

public:
    CvVideoCaptureVideoSource(string path)
        : VideoSource(VideoSourceConfiguration::create(false, true)),
          m_stopped(false),
          m_thread(&CvVideoCaptureVideoSource::run, this),
          m_path(move(path))
    {
    }

    ~CvVideoCaptureVideoSource() override
    {
        m_stopped.store(true);
        m_thread.join();
    }

private:
    void run()
    {
        cv::VideoCapture cap;
        cv::Mat bgrImg;

        while (!m_stopped.load())
        {
            cap.open(m_path);
            if (!cap.isOpened())
            {
                cerr << "Invalid video file" << endl;
                exit(EXIT_FAILURE);
            }

            auto frameDuration = chrono::microseconds(static_cast<int>(1e6 / cap.get(cv::CAP_PROP_FPS)));
            auto frameTime = chrono::steady_clock::now();
            while (!m_stopped.load())
            {
                cap.read(bgrImg);
                if (bgrImg.empty())
                {
                    break;
                }

                int64_t timestampUs =
                    chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now().time_since_epoch()).count();
                sendFrame(bgrImg, timestampUs);

                frameTime += frameDuration;
                this_thread::sleep_until(frameTime);
            }
        }
    }
};

constexpr uint32_t SoundCardTotalDelayMs = 0;
constexpr int BitsPerSample = 16;
constexpr int SampleRate = 48000;
constexpr size_t NumberOfChannels = 1;
constexpr chrono::milliseconds SinAudioSourceFrameDuration = 10ms;
constexpr chrono::milliseconds SinAudioSourceSleepBuffer = 2ms;
constexpr int16_t SinAudioSourceAmplitude = 15000;

class SinAudioSource : public AudioSource
{
    atomic_bool m_stopped;
    thread m_thread;

public:
    SinAudioSource()
        : AudioSource(
              AudioSourceConfiguration::create(SoundCardTotalDelayMs),
              BitsPerSample,
              SampleRate,
              NumberOfChannels),
          m_stopped(false),
          m_thread(&SinAudioSource::run, this)
    {
    }

    ~SinAudioSource() override
    {
        m_stopped.store(true);
        m_thread.join();
    }

private:
    void run()
    {
        vector<int16_t> data(SinAudioSourceFrameDuration.count() * SampleRate / 1000, 0);
        double t = 0;
        for (size_t i = 0; i < data.size(); i++)
        {
            data[i] = static_cast<int16_t>(SinAudioSourceAmplitude * sin(t));
            t += 2 * M_PI / static_cast<double>(data.size());
        }

        while (!m_stopped.load())
        {
            sendFrame(data.data(), data.size() * sizeof(int16_t) / bytesPerFrame());

            auto start = chrono::steady_clock::now();
            this_thread::sleep_for(SinAudioSourceFrameDuration - SinAudioSourceSleepBuffer);
            while ((chrono::steady_clock::now() - start) < SinAudioSourceFrameDuration);
        }
    }
};

int main(int argc, char* argv[])
{
    // 配置 ICE Servers，尝试直接连接到信令服务器
    vector<IceServer> iceServers = {
        IceServer("stun:stun.l.google.com:19302")
    };

    // WebRTC 配置
    auto webrtcConfig = WebrtcConfiguration::create({iceServers});

    // 信令服务器配置
    auto signalingServerConfiguration =
        SignalingServerConfiguration::create("ws://192.168.0.165:80/signaling", "C++", "chat", "abc");

    // 初始化 UE5PixelStreamingSignalingClient 并设置流列表
    std::vector<std::string> streamerList = {"Camera01_Default", "Camera02_Default"};

    //auto videoStreamConfiguration = VideoStreamConfiguration::create();
    //auto signalingClient = make_unique<UE5PixelStreamingSignalingClient>(
    //    signalingServerConfiguration, 
    //    streamerList,
    //    std::move(videoStreamConfiguration));

    //// 设置信令客户端的回调
    //signalingClient->setOnSignalingConnectionOpened([]() {
    //    cout << "Signaling connection opened." << endl;
    //});
    //signalingClient->setOnSignalingConnectionClosed([]() {
    //    cout << "Signaling connection closed." << endl;
    //});
    //signalingClient->setOnSignalingConnectionError([](const string& error) {
    //    cerr << "Signaling error: " << error << endl;
    //});
    //signalingClient->setOnRoomClientsChanged([](const vector<Client>& clients) {
    //    cout << "Room clients updated:" << endl;
    //    for (const auto& client : clients) {
    //        cout << " - ID: " << client.id() << ", Name: " << client.name() << endl;
    //    }
    //});

    //// 连接信令服务器
    //signalingClient->connect();

    //// 保持程序运行，直到手动关闭
    //cin.get();

    //// 关闭连接
    //signalingClient->close();

    //return 0;

    auto webrtcConfiguration = WebrtcConfiguration::create(iceServers);
    auto videoStreamConfiguration = VideoStreamConfiguration::create();
    //auto videoSource = make_shared<CvVideoCaptureVideoSource>(argv[1]);
    //auto audioSource = make_shared<SinAudioSource>();
    StreamClient
        //client(signalingServerConfiguration, webrtcConfiguration, videoStreamConfiguration, videoSource, audioSource);
        client(signalingServerConfiguration, webrtcConfiguration, videoStreamConfiguration);

    client.setOnSignalingConnectionOpened(
        []()
        {
            // This callback is called from the internal client thread.
            cout << "OnSignalingConnectionOpened" << endl;
        });
    client.setOnSignalingConnectionClosed(
        []()
        {
            // This callback is called from the internal client thread.
            cout << "OnSignalingConnectionClosed" << endl;
        });
    client.setOnSignalingConnectionError(
        [](const string& error)
        {
            // This callback is called from the internal client thread.
            cout << "OnSignalingConnectionError:" << endl << "\t" << error;
        });

    client.setOnClientConnected(
        [](const Client& client)
        {
            // This callback is called from the internal client thread.
            cout << "OnClientConnected:" << endl;
            cout << "\tid=" << client.id() << ", name=" << client.name() << endl;
            //cv::namedWindow("DefaultWindow", cv::WINDOW_AUTOSIZE);
        });
    client.setOnClientDisconnected(
        [](const Client& client)
        {
            // This callback is called from the internal client thread.
            cout << "OnClientDisconnected:" << endl;
            cout << "\tid=" << client.id() << ", name=" << client.name() << endl;
            cv::destroyWindow(client.id());
        });
    client.setOnClientConnectionFailed(
        [](const Client& client)
        {
            // This callback is called from the internal client thread.
            cout << "OnClientConnectionFailed:" << endl;
            cout << "\tid=" << client.id() << ", name=" << client.name() << endl;
        });

    client.setOnError(
        [](const string& error)
        {
            // This callback is called from the internal client thread.
            cout << "error:" << endl;
            cout << "\t" << error << endl;
        });

    client.setLogger(
        [](const string& message)
        {
            // This callback is called from the internal client thread.
            cout << "log:" << endl;
            cout << "\t" << message << endl;
        });

    client.setOnRemoveRemoteStream(
        [](const Client& client)
        {
            // This callback is called from the internal client thread.
            cout << "OnRemoveRemoteStream:" << endl;
            cout << "\tid=" << client.id() << ", name=" << client.name() << endl;
        });
    client.setOnVideoFrameReceived(
        [](const Client& client, const cv::Mat& bgrImg, uint64_t timestampUs)
        {
            // This callback is called from a WebRTC processing thread.
            cout << "OnVideoFrameReceived:" << endl;
            cv::imshow("DefaultWindow", bgrImg);
            cv::waitKey(1);
        });
    //client.setOnAudioFrameReceived(
    //    [](const Client& client,
    //       const void* audioData,
    //       int bitsPerSample,
    //       int sampleRate,
    //       size_t numberOfChannels,
    //       size_t numberOfFrames)
    //    {
    //        // This callback is called from a WebRTC processing thread.
    //        cout << "OnAudioFrameReceived:" << endl;
    //        cout << "\tid=" << client.id() << ", name=" << client.name() << endl;
    //        cout << "\tbitsPerSample=" << bitsPerSample << ", sampleRate = " << sampleRate;
    //        cout << ", numberOfChannels = " << numberOfChannels << ", numberOfFrames=" << numberOfFrames << endl;
    //    });
    //client.setOnMixedAudioFrameReceived(
    //    [](const void* audioData, int bitsPerSample, int sampleRate, size_t numberOfChannels, size_t numberOfFrames)
    //    {
    //        // This callback is called from the audio device module thread.
    //        cout << "OnMixedAudioFrameReceived:" << endl;
    //        cout << "\tbitsPerSample=" << bitsPerSample << ", sampleRate=" << sampleRate;
    //        cout << ", numberOfChannels=" << numberOfChannels << ", numberOfFrames=" << numberOfFrames << endl;
    //    });

    client.connect();

    cin.get();

    return 0;
}
