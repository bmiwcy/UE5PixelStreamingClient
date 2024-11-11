#include <OpenteraWebrtcNativeClient/Signaling/WebSocketSignalingClient.h>

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

WebSocketSignalingClient::WebSocketSignalingClient(SignalingServerConfiguration configuration)
    : SignalingClient(move(configuration))
{
    constexpr int PingIntervalSecs = 10;
    m_ws.setPingInterval(PingIntervalSecs);

    call_once(initNetSystemOnceFlag, []() { ix::initNetSystem(); });
}

WebSocketSignalingClient::~WebSocketSignalingClient()
{
    m_ws.stop();
}

void WebSocketSignalingClient::setTlsVerificationEnabled(bool isEnabled)
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

bool WebSocketSignalingClient::isConnected()
{
    return !m_sessionId.empty();
}

string WebSocketSignalingClient::sessionId()
{
    return m_sessionId;
}

void WebSocketSignalingClient::connect()
{
    m_sessionId = "";
    m_ws.stop();
    m_ws.setUrl(m_configuration.url());
    connectWsEvents();
    m_ws.start();
}

void WebSocketSignalingClient::close()
{
    m_ws.close();
    m_sessionId = "";
}

void WebSocketSignalingClient::closeSync()
{
    m_ws.stop();
    m_sessionId = "";
}

void WebSocketSignalingClient::callAll()
{
    auto message = eventToMessage("call-all");
    m_ws.send(message);
}

void WebSocketSignalingClient::callIds(const vector<string>& ids)
{
    auto message = eventToMessage("call-ids", ids);
    m_ws.send(message);
}

void WebSocketSignalingClient::closeAllRoomPeerConnections()
{
    auto message = eventToMessage("close-all-room-peer-connections");
    m_ws.send(message);
}

void WebSocketSignalingClient::callPeer(const string& toId, const string& sdp)
{
    nlohmann::json offer{{"sdp", sdp}, {"type", "offer"}};
    nlohmann::json data{{"toId", toId}, {"offer", offer}};
    auto message = eventToMessage("call-peer", data);
    m_ws.send(message);
}

void WebSocketSignalingClient::makePeerCallAnswer(const string& toId, const string& sdp)
{
    //nlohmann::json offer{
    //    {"sdp", sdp},
    //    {"type", "answer"},
    //};
    //nlohmann::json data{{"streamerId", "Camera01_Default"}, {"answer", offer}};
    //auto message = eventToMessage("make-peer-call-answer", data);
    // 提取 ICE ufrag (usernameFragment)

    std::smatch match;
    if (std::regex_search(sdp, match, std::regex("a=ice-ufrag:(\\S+)")))
    {
        m_usernameFragment = match[1].str(); // 将 m_usernameFragment 定义为类的成员变量
        std::cout << "Extracted usernameFragment: " << m_usernameFragment << std::endl;
    }
    else
    {
        std::cerr << "Failed to extract usernameFragment from SDP." << std::endl;
    }
    // 构建简化的 answer 消息，仅包含 type 和 sdp 字段
    nlohmann::json answerMessage{
        {"type", "answer"},
        {"sdp", sdp}
    };
    
    // 将 JSON 数据转换为字符串格式并发送
    auto message = answerMessage.dump();
    m_ws.send(message);
}

void WebSocketSignalingClient::rejectCall(const string& toId)
{
    nlohmann::json data{{"toId", toId}};
    auto message = eventToMessage("make-peer-call-answer", data);
    m_ws.send(message);
}

void WebSocketSignalingClient::sendIceCandidate(
    const string& sdpMid,
    int sdpMLineIndex,
    const string& candidate,
    const string& toId)
{
    //nlohmann::json candidateJson{
    //    {"sdpMid", sdpMid},
    //    {"sdpMLineIndex", sdpMLineIndex},
    //    {"candidate", candidate},
    //};
    //nlohmann::json data{{"toId", toId}, {"candidate", candidateJson}};
    //auto message = eventToMessage("send-ice-candidate", data);
    // 构建符合要求的 ICE Candidate JSON 消息
    nlohmann::json candidateMessage = {
        {"type", "iceCandidate"},
        {"candidate", {
            {"candidate", candidate},
            {"sdpMid", sdpMid},
            {"sdpMLineIndex", sdpMLineIndex},
            {"usernameFragment", m_usernameFragment}  // 确认是否使用 toId 作为 usernameFragment，或使用正确的值替换
        }}
    };

    // 将目标对象设为 Camera01_Default
    auto message = candidateMessage.dump();
    m_ws.send(message);
}


void WebSocketSignalingClient::connectWsEvents()
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

void WebSocketSignalingClient::onWsOpenEvent()
{
    //nlohmann::json data{
    //    {"name", m_configuration.clientName()},
    //    {"data", m_configuration.clientData()},
    //    {"room", m_configuration.room()},
    //    {"password", m_configuration.password()},
    //    {"protocolVersion", SignalingProtocolVersion}};
    //m_ws.send(eventToMessage("join-room", data));
    nlohmann::json listStreamersMessage = {{"type", "listStreamers"}};
    m_ws.send(listStreamersMessage.dump());
}

void WebSocketSignalingClient::onWsCloseEvent()
{
    invokeIfCallable(m_onSignalingConnectionClosed);
}

void WebSocketSignalingClient::onWsErrorEvent(const string& error)
{
    invokeIfCallable(m_onSignalingConnectionError, error);
}

void WebSocketSignalingClient::onStreamerListReceived(const nlohmann::json& data)
{
    if (data.contains("ids") && data["ids"].is_array())
    {
        auto availableStreamers = data["ids"];
        
        if (!availableStreamers.empty())
        {
            // 选择要订阅的 streamerId
            std::string streamerId = availableStreamers[0];
            
            // 构建并发送订阅请求
            nlohmann::json subscribeMessage = {
                {"type", "subscribe"},
                {"streamerId", streamerId}
            };

            m_ws.send(subscribeMessage.dump());
            std::cout << "Subscribed to streamerId: " << streamerId << std::endl;
        }
        else
        {
            std::cerr << "No available streamers to subscribe." << std::endl;
        }
    }
    else
    {
        std::cerr << "Invalid 'streamerList' message format." << std::endl;
    }
}

void WebSocketSignalingClient::onWsMessage(const string& message)
{
     nlohmann::json parsedMessage = nlohmann::json::parse(message, nullptr, false);

    // 检查消息是否为有效的 JSON 格式，包含所需的 "type" 字段
    if (parsedMessage.is_discarded() || !parsedMessage.is_object() || !parsedMessage.contains("type"))
    {
        std::cerr << "Received invalid message: " << message << std::endl;
        return;
    }

    // 获取消息的类型
    std::string messageType = parsedMessage["type"];

    // 根据消息类型进行处理
    if (messageType == "streamerList")
    {
        onStreamerListReceived(parsedMessage);  // 处理 streamerList 消息
    }
    else if (messageType == "offer")
    {
        std::string sdp = parsedMessage["sdp"];
        // 如果没有 `fromId`，使用一个默认值
        std::string fromId = "default_id";  // 或者空字符串 ""

        if (m_onOfferReceived)
        {
            m_onOfferReceived(fromId, sdp);
        }

        std::cout << "Received offer with default fromId" << std::endl;

    }
    else if (messageType == "iceCandidate")
    {

    }
    else
    {
        std::cerr << "Unknown message type received: " << messageType << std::endl;
    }
}

void WebSocketSignalingClient::onJoinRoomAnswerEvent(const nlohmann::json& data)
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

void WebSocketSignalingClient::onRoomClientsEvent(const nlohmann::json& data)
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

void WebSocketSignalingClient::onMakePeerCallEvent(const nlohmann::json& data)
{
    JSON_CHECK_RETURN(!data.is_array(), "Invalid onMakePeerCallEvent data (global type)");

    for (const auto& id : data)
    {
        JSON_CHECK_CONTINUE(!id.is_string(), "Invalid onMakePeerCallEvent peer id");
        invokeIfCallable(m_makePeerCall, id);
    }
}

void WebSocketSignalingClient::onPeerCallReceivedEvent(const nlohmann::json& data)
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

void WebSocketSignalingClient::onPeerCallAnswerReceivedEvent(const nlohmann::json& data)
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

void WebSocketSignalingClient::onCloseAllPeerConnectionsRequestReceivedEvent()
{
    invokeIfCallable(m_closeAllPeerConnections);
}

void WebSocketSignalingClient::onIceCandidateReceivedEvent(const nlohmann::json& data)
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
