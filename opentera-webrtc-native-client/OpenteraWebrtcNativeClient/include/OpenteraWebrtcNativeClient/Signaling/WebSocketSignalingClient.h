#ifndef OPENTERA_WEBRTC_NATIVE_CLIENT_SIGNALING_SIO_SIGNALING_CLIENT_H
#define OPENTERA_WEBRTC_NATIVE_CLIENT_SIGNALING_SIO_SIGNALING_CLIENT_H

#include <OpenteraWebrtcNativeClient/Signaling/SignalingClient.h>

#include <ixwebsocket/IXWebSocket.h>
#include <iostream>
#include <regex>


namespace opentera
{
    class WebSocketSignalingClient : public SignalingClient
    {
        ix::WebSocket m_ws;
        std::string m_sessionId;

    public:
        WebSocketSignalingClient(SignalingServerConfiguration configuration);
        ~WebSocketSignalingClient() override;

        DECLARE_NOT_COPYABLE(WebSocketSignalingClient);
        DECLARE_NOT_MOVABLE(WebSocketSignalingClient);

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

        void sendMessage(const std::string& message) {
            m_ws.send(message);
        }

        void setOnOfferReceived(const std::function<void(const std::string& sdp)>& callback);
        std::function<void(const std::string& fromId, const std::string& sdp)> m_onOfferReceived;
    private:
        std::string m_usernameFragment;

        void onStreamerListReceived(const nlohmann::json& data);

        void connectWsEvents();

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
    };
}

#endif
