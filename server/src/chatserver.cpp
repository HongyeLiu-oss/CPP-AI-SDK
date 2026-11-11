#include "chatserver.h"
#include "util/log.h"

#include <jsoncpp/json/json.h>
#include <jsoncpp/json/reader.h>
#include <jsoncpp/json/writer.h>

#include <ctime>
#include <memory>

namespace chat_server
{
    namespace
    {
        // 统一响应格式：{ "success": bool, "message": string, "data": ... }
        std::string toJson(const Json::Value &value)
        {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            return Json::writeString(builder, value);
        }

        void sendJson(httplib::Response &res, int status,
                      const std::string &message, const Json::Value &data)
        {
            Json::Value root;
            root["success"] = (status == 200);
            root["message"] = message;
            root["data"] = data;
            res.status = status;
            res.set_content(toJson(root), "application/json; charset=utf-8");
        }

        void sendError(httplib::Response &res, int status, const std::string &message)
        {
            sendJson(res, status, message, Json::Value(Json::nullValue));
        }

        // 解析请求体 JSON，失败时直接写好 400 响应并返回 false
        bool parseBody(const httplib::Request &req, httplib::Response &res, Json::Value &out)
        {
            if (req.body.empty())
            {
                sendError(res, 400, "request body is empty");
                return false;
            }

            Json::CharReaderBuilder builder;
            std::string errs;
            std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
            if (!reader->parse(req.body.data(), req.body.data() + req.body.size(), &out, &errs))
            {
                sendError(res, 400, "invalid json: " + errs);
                return false;
            }
            return true;
        }

        // SSE 单条事件：data: {"content":"..."} + 空行
        std::string makeSseContentEvent(const std::string &content)
        {
            Json::Value value;
            value["content"] = content;
            return "data: " + toJson(value) + "\n\n";
        }
    }// namespace

    ChatServer::ChatServer(ai_chat_sdk::ChatSDK &sdk, int port, const std::string &staticDir)
        : m_sdk(sdk)
        , m_port(port)
        , m_staticDir(staticDir)
    {
        setupRoutes();
    }

    void ChatServer::setupRoutes()
    {
        m_server.Get("/api/models",
            [this](const httplib::Request &req, httplib::Response &res) {
                handleGetModels(req, res);
            });
        m_server.Get("/api/sessions",
            [this](const httplib::Request &req, httplib::Response &res) {
                handleGetSessions(req, res);
            });
        m_server.Post("/api/session",
            [this](const httplib::Request &req, httplib::Response &res) {
                handleCreateSession(req, res);
            });
        m_server.Get(R"(/api/session/([^/]+)/history)",
            [this](const httplib::Request &req, httplib::Response &res) {
                handleGetHistory(req, res);
            });
        m_server.Post("/api/message",
            [this](const httplib::Request &req, httplib::Response &res) {
                handleSendMessage(req, res);
            });
        m_server.Post("/api/message/async",
            [this](const httplib::Request &req, httplib::Response &res) {
                handleSendMessageStream(req, res);
            });
        m_server.Delete(R"(/api/session/([^/]+))",
            [this](const httplib::Request &req, httplib::Response &res) {
                handleDeleteSession(req, res);
            });

        // 静态页面放最后：httplib 先匹配路由，匹配不到才会落到静态文件
        if (!m_server.set_mount_point("/", m_staticDir))
        {
            LOG_WARN("Static directory not found: {}", m_staticDir);
        }
    }

    bool ChatServer::run()
    {
        LOG_INFO("Chat server listening on 0.0.0.0:{}", m_port);
        return m_server.listen("0.0.0.0", m_port);
    }

    void ChatServer::stop()
    {
        m_server.stop();
    }

    void ChatServer::handleGetModels(const httplib::Request &, httplib::Response &res)
    {
        Json::Value data(Json::arrayValue);
        for (const auto &model : m_sdk.getAvailableModels())
        {
            Json::Value item;
            item["modelName"] = model.modelName;
            item["description"] = model.description;
            item["provider"] = model.provider;
            item["endpoint"] = model.endpoint;
            item["isAvailable"] = model.isAvailable;
            data.append(item);
        }
        sendJson(res, 200, "ok", data);
    }

    void ChatServer::handleGetSessions(const httplib::Request &, httplib::Response &res)
    {
        Json::Value data(Json::arrayValue);
        for (const auto &sessionId : m_sdk.getSessionList())
        {
            auto session = m_sdk.getSession(sessionId);
            if (!session)
            {
                continue;
            }

            // 列表只要元信息和首条用户消息，不用把全部消息塞回去
            std::string firstUserMessage;
            size_t messageCount = 0;
            for (const auto &message : m_sdk.getSessionHistory(sessionId))
            {
                ++messageCount;
                if (firstUserMessage.empty() && message.role == "user")
                {
                    firstUserMessage = message.content;
                }
            }

            Json::Value item;
            item["sessionId"] = session->sessionId;
            item["modelName"] = session->modelName;
            item["createTime"] = static_cast<Json::Int64>(session->createTime);
            item["updateTime"] = static_cast<Json::Int64>(session->updateTime);
            item["messageCount"] = static_cast<Json::UInt64>(messageCount);
            item["firstUserMessage"] = firstUserMessage;
            data.append(item);
        }
        sendJson(res, 200, "ok", data);
    }

    void ChatServer::handleCreateSession(const httplib::Request &req, httplib::Response &res)
    {
        Json::Value body;
        if (!parseBody(req, res, body))
        {
            return;
        }

        const std::string modelName = body.get("model", "").asString();
        if (modelName.empty())
        {
            sendError(res, 400, "model is required");
            return;
        }

        const std::string sessionId = m_sdk.createSession(modelName);
        if (sessionId.empty())
        {
            sendError(res, 400, "failed to create session, model may be unavailable");
            return;
        }

        auto session = m_sdk.getSession(sessionId);
        Json::Value data;
        data["sessionId"] = sessionId;
        data["modelName"] = modelName;
        data["createTime"] = static_cast<Json::Int64>(session ? session->createTime : std::time(nullptr));
        data["updateTime"] = static_cast<Json::Int64>(session ? session->updateTime : std::time(nullptr));
        sendJson(res, 200, "session created", data);
    }

    void ChatServer::handleGetHistory(const httplib::Request &req, httplib::Response &res)
    {
        const std::string sessionId = req.matches[1];
        auto session = m_sdk.getSession(sessionId);
        if (!session)
        {
            sendError(res, 404, "session not found");
            return;
        }

        Json::Value messages(Json::arrayValue);
        for (const auto &message : m_sdk.getSessionHistory(sessionId))
        {
            Json::Value item;
            item["messageId"] = message.messageId;
            item["role"] = message.role;
            item["content"] = message.content;
            item["timestamp"] = static_cast<Json::Int64>(message.timestamp);
            messages.append(item);
        }

        Json::Value data;
        data["sessionId"] = sessionId;
        data["modelName"] = session->modelName;
        data["messages"] = messages;
        sendJson(res, 200, "ok", data);
    }

    void ChatServer::handleSendMessage(const httplib::Request &req, httplib::Response &res)
    {
        Json::Value body;
        if (!parseBody(req, res, body))
        {
            return;
        }

        const std::string sessionId = body.get("sessionId", "").asString();
        const std::string content = body.get("content", "").asString();
        if (sessionId.empty() || content.empty())
        {
            sendError(res, 400, "sessionId and content are required");
            return;
        }

        const std::string reply = m_sdk.sendMessage(sessionId, content);
        if (reply.empty())
        {
            sendError(res, 500, "failed to get reply from model");
            return;
        }

        Json::Value data;
        data["reply"] = reply;
        sendJson(res, 200, "ok", data);
    }

    void ChatServer::handleSendMessageStream(const httplib::Request &req, httplib::Response &res)
    {
        Json::Value body;
        if (!parseBody(req, res, body))
        {
            return;
        }

        const std::string sessionId = body.get("sessionId", "").asString();
        const std::string content = body.get("content", "").asString();
        if (sessionId.empty() || content.empty())
        {
            sendError(res, 400, "sessionId and content are required");
            return;
        }

        res.set_header("Cache-Control", "no-cache");
        // content_provider 在 handler 返回之后才执行，所以只能按值捕获，不能捕获局部变量引用
        res.set_chunked_content_provider("text/event-stream",
            [this, sessionId, content](size_t, httplib::DataSink &sink) {
                const std::string reply = m_sdk.sendMessageStream(sessionId, content,
                    [&sink](const std::string &chunk, ai_chat_sdk::StreamStatus) {
                        if (chunk.empty())
                        {
                            return;
                        }
                        const std::string event = makeSseContentEvent(chunk);
                        sink.write(event.data(), event.size());
                    });

                if (reply.empty())
                {
                    static const std::string errorEvent =
                        "data: {\"error\":\"failed to get reply from model\"}\n\n";
                    sink.write(errorEvent.data(), errorEvent.size());
                }

                static const std::string doneEvent = "data: [DONE]\n\n";
                sink.write(doneEvent.data(), doneEvent.size());
                sink.done();
                return true;
            });
    }

    void ChatServer::handleDeleteSession(const httplib::Request &req, httplib::Response &res)
    {
        const std::string sessionId = req.matches[1];
        if (!m_sdk.deleteSession(sessionId))
        {
            sendError(res, 404, "session not found");
            return;
        }
        sendJson(res, 200, "session deleted", Json::Value(Json::nullValue));
    }
}// namespace chat_server
