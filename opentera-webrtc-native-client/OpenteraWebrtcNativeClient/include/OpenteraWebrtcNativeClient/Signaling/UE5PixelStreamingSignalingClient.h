#ifndef OPENTERA_WEBRTC_NATIVE_CLIENT_SIGNALING_SIO_SIGNALING_CLIENT_H
#define OPENTERA_WEBRTC_NATIVE_CLIENT_SIGNALING_SIO_SIGNALING_CLIENT_H

#include <OpenteraWebrtcNativeClient/Signaling/SignalingClient.h>
#include <OpenteraWebrtcNativeClient/Handlers/StreamPeerConnectionHandler.h>
#include <OpenteraWebrtcNativeClient/Configurations/VideoStreamConfiguration.h>

#include <ixwebsocket/IXWebSocket.h>

#include <iostream>



namespace opentera
{
    class UE5PixelStreamingSignalingClient : public SignalingClient
    {
        ix::WebSocket m_ws;
        std::string m_sessionId;

        std::unique_ptr<rtc::Thread> m_networkThread;
        std::unique_ptr<rtc::Thread> m_workerThread;
        std::unique_ptr<rtc::Thread> m_signalingThread;
        std::unique_ptr<SignalingClient> m_signalingClient;

    public:
        using VideoFrameReceivedCallback = std::function<void(const Client&, const cv::Mat&, uint64_t)>;

        UE5PixelStreamingSignalingClient(
            SignalingServerConfiguration configuration, 
            const std::vector<std::string>& streamerList,
            VideoStreamConfiguration&& videoStreamConfiguration
            );
        ~UE5PixelStreamingSignalingClient() override;

        DECLARE_NOT_COPYABLE(UE5PixelStreamingSignalingClient);
        DECLARE_NOT_MOVABLE(UE5PixelStreamingSignalingClient);

        void setTlsVerificationEnabled(bool isEnabled) override;

        bool isConnected() override;
        std::string sessionId() override;

        void connect() override;
        void close() override;
        void closeSync() override;

        void callAll() override;
        void callIds(const std::vector<std::string>& ids) override;
        void closeAllRoomPeerConnections() override;

        void callPeer(const std::string& toId, const std::string& sdp) override;
        void makePeerCallAnswer(const std::string& toId, const std::string& sdp) override;
        void rejectCall(const std::string& toId) override;
        void sendIceCandidate(
            const std::string& sdpMid,
            int sdpMLineIndex,
            const std::string& candidate,
            const std::string& toId) override;

    private:
        void connectWsEvents();

        void requestStreamerList();
        void onStreamerListReceived(const nlohmann::json& data);
        void subscribeToStreamer(const std::string& streamerId);

        void onWsOpenEvent();
        void onWsCloseEvent();
        void onWsErrorEvent(const std::string& error);
        void onWsMessage(const std::string& message);

        void onJoinRoomAnswerEvent(const nlohmann::json& data);

        void onRoomClientsEvent(const nlohmann::json& data);

        void onMakePeerCallEvent(const nlohmann::json& data);
        void onPeerCallReceivedEvent(const nlohmann::json& data);
        void onPeerCallAnswerReceivedEvent(const nlohmann::json& data);
        void onCloseAllPeerConnectionsRequestReceivedEvent();
        void onIceCandidateReceivedEvent(const nlohmann::json& data);

        std::vector<std::string> m_targetStreamers;
        VideoFrameReceivedCallback m_onVideoFrameReceived;
        std::unique_ptr<PeerConnectionHandler> m_peerConnectionHandler;
        std::unordered_map<std::string, std::unique_ptr<opentera::PeerConnectionHandler>> m_peerConnectionHandlers;
        rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> m_peerConnectionFactory;

        std::unique_ptr<opentera::StreamPeerConnectionHandler> createStreamConnection(
            const opentera::Client& peerClient, 
            bool isCaller, 
            const opentera::VideoFrameReceivedCallback& onVideoFrameReceived)
        {
            // 创建 StreamPeerConnectionHandler 实例并传入回调
            auto handler = std::make_unique<opentera::StreamPeerConnectionHandler>(
                "default_id", 
                peerClient, 
                isCaller, 
                false, 
                *this,
                [](const std::string& error) { std::cerr << "Error: " << error << std::endl; },
                [](const opentera::Client& client) { std::cout << "Client connected: " << client.name() << std::endl; },
                [](const opentera::Client& client) { std::cout << "Client disconnected: " << client.name() << std::endl; },
                [](const opentera::Client& client) { std::cout << "Client connection failed: " << client.name() << std::endl; },
                nullptr, 
                nullptr, 
                [](const opentera::Client& client) { std::cout << "Remote stream added: " << client.name() << std::endl; },
                [](const opentera::Client& client) { std::cout << "Remote stream removed: " << client.name() << std::endl; },
                onVideoFrameReceived,
                nullptr, 
                nullptr);
        
            // 使用 peerConnectionFactory 创建 PeerConnection
            webrtc::PeerConnectionInterface::RTCConfiguration configuration;
            auto peerConnection = m_peerConnectionFactory->CreatePeerConnectionOrError(
                configuration, webrtc::PeerConnectionDependencies(handler.get()));
        
            if (peerConnection.ok()) {
                handler->setPeerConnection(peerConnection.MoveValue());
                return handler;
            } else {
                throw std::runtime_error("Failed to create PeerConnection: " + std::string(peerConnection.error().message()));
            }
        }


    };
}

#endif
