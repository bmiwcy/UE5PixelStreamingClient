#include <OpenteraWebrtcNativeClient/Signaling/UE5PixelStreamingSignalingClient.h>
#include <OpenteraWebrtcNativeClient/Handlers/PeerConnectionHandler.h>

#include <OpenteraWebrtcNativeClient/WebrtcClient.h>
#include <OpenteraWebrtcNativeClient/Codecs/VideoCodecFactories.h>
#include <api/audio_codecs/builtin_audio_decoder_factory.h>
#include <api/audio_codecs/builtin_audio_encoder_factory.h>
#include <api/create_peerconnection_factory.h>


#include <ixwebsocket/IXNetSystem.h>

#include <mutex>

using namespace opentera;
using namespace std;

constexpr int SignalingProtocolVersion = 2;

#define JSON_CHECK_RETURN(condition, message)                                                                          \
    if ((condition))                                                                                                   \
    {                                                                                                                  \
        invokeIfCallable(m_onError, (message));                                                                        \
        return;                                                                                                        \
    }                                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
    } while (false)

#define JSON_CHECK_CONTINUE(condition, message)                                                                        \
    if ((condition))                                                                                                   \
    {                                                                                                                  \
        invokeIfCallable(m_onError, (message));                                                                        \
        continue;                                                                                                      \
    }                                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
    } while (false)

string eventToMessage(const char* event)
{
    nlohmann::json message{{"event", event}};

    return message.dump();
}

string eventToMessage(const char* event, const nlohmann::json& data)
{
    nlohmann::json message{{"event", event}, {"data", data}};

    return message.dump();
}


once_flag initNetSystemOnceFlag;

UE5PixelStreamingSignalingClient::UE5PixelStreamingSignalingClient(
    SignalingServerConfiguration signalingServerConfiguration, 
    const std::vector<std::string>& streamerList,
    VideoStreamConfiguration&& videoStreamConfiguration)
    : SignalingClient(move(signalingServerConfiguration)), 
      m_targetStreamers(streamerList)
{
    //m_networkThread = move(rtc::Thread::CreateWithSocketServer());
    //m_networkThread->SetName(signalingServerConfiguration.clientName() + " - network", nullptr);
    //m_networkThread->Start();
    //m_workerThread = move(rtc::Thread::Create());
    //m_workerThread->SetName(signalingServerConfiguration.clientName() + " - worker", nullptr);
    //m_workerThread->Start();
    //m_signalingThread = move(rtc::Thread::Create());
    //m_signalingThread->SetName(signalingServerConfiguration.clientName() + " - signaling", nullptr);
    //m_signalingThread->Start();

    //m_peerConnectionFactory = webrtc::CreatePeerConnectionFactory(
    //    m_networkThread.get(),
    //    m_workerThread.get(),
    //    m_signalingThread.get(),
    //    nullptr,
    //    webrtc::CreateBuiltinAudioEncoderFactory(),
    //    webrtc::CreateBuiltinAudioDecoderFactory(),
    //    createVideoEncoderFactory(videoStreamConfiguration),
    //    createVideoDecoderFactory(videoStreamConfiguration),
    //    nullptr,  // Audio mixer,
    //    nullptr);

    //if (!m_peerConnectionFactory)
    //{
    //    throw runtime_error("CreatePeerConnectionFactory failed");
    //}

    constexpr int PingIntervalSecs = 10;
    m_ws.setPingInterval(PingIntervalSecs);

    call_once(initNetSystemOnceFlag, []() { ix::initNetSystem(); });
}

UE5PixelStreamingSignalingClient::~UE5PixelStreamingSignalingClient()
{
    m_ws.stop();
}



void UE5PixelStreamingSignalingClient::setTlsVerificationEnabled(bool isEnabled)
{
    // TODO
    ix::SocketTLSOptions options;
    if (isEnabled)
    {
        options.disable_hostname_validation = false;
        options.caFile = "SYSTEM";
    }
    else
    {
        options.disable_hostname_validation = true;
        options.caFile = "NONE";
    }
    m_ws.setTLSOptions(options);
}

bool UE5PixelStreamingSignalingClient::isConnected()
{
    return !m_sessionId.empty();
}

string UE5PixelStreamingSignalingClient::sessionId()
{
    return m_sessionId;
}

void UE5PixelStreamingSignalingClient::connect()
{
    m_sessionId = "";
    m_ws.stop();
    m_ws.setUrl(m_configuration.url());
    connectWsEvents();
    m_ws.start();
}

void UE5PixelStreamingSignalingClient::close()
{
    m_ws.close();
    m_sessionId = "";
}

void UE5PixelStreamingSignalingClient::closeSync()
{
    m_ws.stop();
    m_sessionId = "";
}

void UE5PixelStreamingSignalingClient::callAll()
{
    m_ws.send(eventToMessage("call-all"));
}

void UE5PixelStreamingSignalingClient::callIds(const vector<string>& ids)
{
    m_ws.send(eventToMessage("call-ids", ids));
}

void UE5PixelStreamingSignalingClient::closeAllRoomPeerConnections()
{
    m_ws.send(eventToMessage("close-all-room-peer-connections"));
}

void UE5PixelStreamingSignalingClient::callPeer(const string& toId, const string& sdp)
{
    nlohmann::json offer{{"sdp", sdp}, {"type", "offer"}};
    nlohmann::json data{{"toId", toId}, {"offer", offer}};
    m_ws.send(eventToMessage("call-peer", data));
}

void UE5PixelStreamingSignalingClient::makePeerCallAnswer(const string& toId, const string& sdp)
{
    nlohmann::json offer{
        {"sdp", sdp},
        {"type", "answer"},
    };
    nlohmann::json data{{"toId", toId}, {"answer", offer}};
    m_ws.send(eventToMessage("make-peer-call-answer", data));
}

void UE5PixelStreamingSignalingClient::rejectCall(const string& toId)
{
    nlohmann::json data{{"toId", toId}};
    m_ws.send(eventToMessage("make-peer-call-answer", data));
}

void UE5PixelStreamingSignalingClient::sendIceCandidate(
    const string& sdpMid,
    int sdpMLineIndex,
    const string& candidate,
    const string& toId)
{
    nlohmann::json candidateJson{
        {"sdpMid", sdpMid},
        {"sdpMLineIndex", sdpMLineIndex},
        {"candidate", candidate},
    };
    nlohmann::json data{{"toId", toId}, {"candidate", candidateJson}};
    m_ws.send(eventToMessage("send-ice-candidate", data));
}

void UE5PixelStreamingSignalingClient::connectWsEvents()
{
    m_ws.setOnMessageCallback(
        [this](const ix::WebSocketMessagePtr& msg)
        {
            switch (msg->type)
            {
                case ix::WebSocketMessageType::Open:
                    onWsOpenEvent();
                    break;
                case ix::WebSocketMessageType::Close:
                    onWsCloseEvent();
                    break;
                case ix::WebSocketMessageType::Error:
                    onWsErrorEvent(msg->errorInfo.reason);
                    break;
                case ix::WebSocketMessageType::Message:
                    onWsMessage(msg->str);
                    break;
                default:
                    break;
            }
        });
}

void UE5PixelStreamingSignalingClient::requestStreamerList()
{
    nlohmann::json message = {
        {"type", "listStreamers"}
    };

    m_ws.send(message.dump());  
}

void UE5PixelStreamingSignalingClient::onStreamerListReceived(const nlohmann::json& data)
{
    if (data.contains("ids") && data["ids"].is_array())
    {
        for (const auto& id : data["ids"])
        {
            if (id.is_string())
            {
                std::string streamerId = id.get<std::string>();
                std::cout << "Available streamer: " << streamerId << std::endl;
            }
        }
    }
}

void UE5PixelStreamingSignalingClient::subscribeToStreamer(const std::string& streamerId)
{
    nlohmann::json message = {
        {"type", "subscribe"},
        {"streamerId", streamerId}
    };

    m_ws.send(message.dump());  // 发送 JSON 格式的订阅请求
}

void UE5PixelStreamingSignalingClient::onWsOpenEvent()
{
    std::cout << "Connected to signaling server." << std::endl;
    requestStreamerList();
}

void UE5PixelStreamingSignalingClient::onWsCloseEvent()
{
    invokeIfCallable(m_onSignalingConnectionClosed);
}

void UE5PixelStreamingSignalingClient::onWsErrorEvent(const string& error)
{
    invokeIfCallable(m_onSignalingConnectionError, error);
}

void UE5PixelStreamingSignalingClient::onWsMessage(const string& message)
{
    nlohmann::json parsedMessage = nlohmann::json::parse(message, nullptr, false);

    if (parsedMessage["type"] == "streamerList")
    {
        auto availableStreamers = parsedMessage["ids"];
        for (const auto& targetStreamer : m_targetStreamers)
        {
            if (std::find(availableStreamers.begin(), availableStreamers.end(), targetStreamer) != availableStreamers.end())
            {
                // Send subscribe message
                nlohmann::json subscribeMessage = {
                    {"type", "subscribe"},
                    {"streamerId", targetStreamer}
                };
                m_ws.send(subscribeMessage.dump());

                std::cout << "Subscribed to streamerId: " << targetStreamer << std::endl;
            }
        }
    }
    else if (parsedMessage["type"] == "offer")
    {
        if (parsedMessage.contains("sdp") && parsedMessage["sdp"].is_string())
        {
            std::string sdp = parsedMessage["sdp"];

            // 创建一个默认的 `peerClient`
            auto peerClient = opentera::Client("default_id", "client_name", nlohmann::json::object());

            // 使用 `createStreamConnection` 函数创建 `StreamPeerConnectionHandler` 并存储在 `m_peerConnectionHandler`
            m_peerConnectionHandler = createStreamConnection(
                peerClient,
                false,  // 假设此连接非呼叫者
                [](const opentera::Client& client, const cv::Mat& frame, uint64_t timestampUs) {
                    // 处理接收到的视频帧，比如显示或处理 frame 数据
                    std::cout << "Received video frame from client: " << client.name() << std::endl;
                });

            m_peerConnectionHandler->receivePeerCall(sdp);
        }
        else
        {
            std::cerr << "Error: 'sdp' field is missing or not a string." << std::endl;
        }
    }


}

void UE5PixelStreamingSignalingClient::onJoinRoomAnswerEvent(const nlohmann::json& data)
{
    if (data.is_string())
    {
        if (data != nlohmann::json(""))
        {
            m_sessionId = data;
            invokeIfCallable(m_onSignalingConnectionOpened);
        }
        else
        {
            close();
            invokeIfCallable(m_onSignalingConnectionError, "Invalid password or invalid protocol version");
        }
    }
    else
    {
        close();
        invokeIfCallable(m_onSignalingConnectionError, "Invalid join-room response");
    }
}

void UE5PixelStreamingSignalingClient::onRoomClientsEvent(const nlohmann::json& data)
{
    vector<Client> clients;
    if (!data.is_array())
    {
        invokeIfCallable(m_onError, "Invalid room clients data");
        invokeIfCallable(m_onRoomClientsChanged, clients);
        return;
    }

    for (const auto& roomClient : data)
    {
        if (Client::isValid(roomClient))
        {
            clients.emplace_back(roomClient);
        }
    }
    invokeIfCallable(m_onRoomClientsChanged, clients);
}

void UE5PixelStreamingSignalingClient::onMakePeerCallEvent(const nlohmann::json& data)
{
    JSON_CHECK_RETURN(!data.is_array(), "Invalid onMakePeerCallEvent data (global type)");

    for (const auto& id : data)
    {
        JSON_CHECK_CONTINUE(!id.is_string(), "Invalid onMakePeerCallEvent peer id");
        invokeIfCallable(m_makePeerCall, id);
    }
}

void UE5PixelStreamingSignalingClient::onPeerCallReceivedEvent(const nlohmann::json& data)
{
    JSON_CHECK_RETURN(!data.is_object(), "Invalid onPeerCallReceivedEvent data (global type)");

    JSON_CHECK_RETURN(
        !data.contains("fromId") || !data.contains("offer"),
        "Invalid onPeerCallReceivedEvent data (fromId or offer are missing)");

    auto fromId = data["fromId"];
    auto offer = data["offer"];
    JSON_CHECK_RETURN(
        !fromId.is_string() || !offer.is_object(),
        "Invalid onPeerCallReceivedEvent data (fromId or offer types)");

    JSON_CHECK_RETURN(
        !offer.contains("sdp") || !offer.contains("type"),
        "Invalid onPeerCallReceivedEvent message (sdp or type are missing)");

    auto sdp = offer["sdp"];
    auto type = offer["type"];
    JSON_CHECK_RETURN(
        !sdp.is_string() || !type.is_string(),
        "Invalid onPeerCallReceivedEvent message (sdp or type wrong types)");

    JSON_CHECK_RETURN(type != "offer", "Invalid onPeerCallReceivedEvent message (invalid offer type)");
    invokeIfCallable(m_receivePeerCall, fromId, sdp);
}

void UE5PixelStreamingSignalingClient::onPeerCallAnswerReceivedEvent(const nlohmann::json& data)
{
    JSON_CHECK_RETURN(!data.is_object(), "Invalid onPeerCallAnswerReceivedEvent message (global type)");

    auto fromIdIt = data.find("fromId");
    auto answerIt = data.find("answer");

    JSON_CHECK_RETURN(
        fromIdIt == data.end() || !fromIdIt->is_string(),
        "Invalid onPeerCallAnswerReceivedEvent message (fromId type)");
    auto fromId = *fromIdIt;

    if (answerIt == data.end() || !answerIt->is_object())
    {
        invokeIfCallable(m_onCallRejected, fromId);
        return;
    }

    auto answer = *answerIt;
    JSON_CHECK_RETURN(
        !answer.contains("sdp") || !answer.contains("type"),
        "Invalid onPeerCallAnswerReceivedEvent message (sdp or type are missing)");

    auto sdp = answer["sdp"];
    auto type = answer["type"];
    JSON_CHECK_RETURN(
        !sdp.is_string() || !type.is_string(),
        "Invalid onPeerCallAnswerReceivedEvent message (sdp or type types)");

    JSON_CHECK_RETURN(type != "answer", "Invalid onPeerCallAnswerReceivedEvent message (invalid answer type)");
    invokeIfCallable(m_receivePeerCallAnswer, fromId, sdp);
}

void UE5PixelStreamingSignalingClient::onCloseAllPeerConnectionsRequestReceivedEvent()
{
    invokeIfCallable(m_closeAllPeerConnections);
}

void UE5PixelStreamingSignalingClient::onIceCandidateReceivedEvent(const nlohmann::json& data)
{
    JSON_CHECK_RETURN(!data.is_object(), "Invalid onIceCandidateReceivedEvent message (global type)");
    JSON_CHECK_RETURN(
        !data.contains("fromId") || !data.contains("candidate"),
        "Invalid onIceCandidateReceivedEvent message (fromId or candidate are missing)");

    auto fromId = data["fromId"];
    auto candidate = data["candidate"];
    if (candidate.is_null())
    {
        return;
    }
    JSON_CHECK_RETURN(
        !fromId.is_string() || !candidate.is_object(),
        "Invalid onIceCandidateReceivedEvent message (fromId or candidate wrong types)");
    JSON_CHECK_RETURN(
        !candidate.contains("sdpMid") || !candidate.contains("sdpMLineIndex") || !candidate.contains("candidate"),
        "Invalid onIceCandidateReceivedEvent message (sdpMid, sdpMLineIndex or candidate are missing)");

    auto sdpMid = candidate["sdpMid"];
    auto sdpMLineIndex = candidate["sdpMLineIndex"];
    auto sdp = candidate["candidate"];
    JSON_CHECK_RETURN(
        !sdpMid.is_string() || !sdpMLineIndex.is_number_integer() || !sdp.is_string(),
        "Invalid onIceCandidateReceivedEvent message (sdpMid, sdpMLineIndex or candidate wrong types)");

    invokeIfCallable(m_receiveIceCandidate, fromId, sdpMid, static_cast<int>(sdpMLineIndex), sdp);
}
