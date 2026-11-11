#pragma once

#include "chat_sdk.h"

#include <httplib.h>
#include <string>

namespace chat_server
{
    // ChatServer 职责：把 HTTP 请求翻译成 ChatSDK 调用，再翻译成 JSON / SSE 响应
    // 不包含任何业务逻辑，所有会话与模型操作都通过 ChatSDK 完成
    class ChatServer
    {
    public:
        ChatServer(ai_chat_sdk::ChatSDK &sdk, int port, const std::string &staticDir);

        // 启动监听，阻塞直到 stop() 或进程退出；启动失败返回 false
        bool run();
        // 停止监听（可从其他线程调用）
        void stop();

    private:
        // 注册全部路由
        void setupRoutes();

        // 从请求头解析 token 并校验，返回 userId；无效返回空串
        std::string authenticate(const httplib::Request &req) const;
        // 从请求头解析 token（Authorization: Bearer <token>），空则返回空串
        static std::string extractToken(const httplib::Request &req);

        // --- 接口处理函数 ---
        void handleRegister(const httplib::Request &req, httplib::Response &res);
        void handleLogin(const httplib::Request &req, httplib::Response &res);
        void handleLogout(const httplib::Request &req, httplib::Response &res);
        void handleGetModels(const httplib::Request &req, httplib::Response &res);
        void handleGetSessions(const httplib::Request &req, httplib::Response &res);
        void handleCreateSession(const httplib::Request &req, httplib::Response &res);
        void handleGetHistory(const httplib::Request &req, httplib::Response &res);
        void handleSendMessage(const httplib::Request &req, httplib::Response &res);
        void handleSendMessageStream(const httplib::Request &req, httplib::Response &res);
        void handleDeleteSession(const httplib::Request &req, httplib::Response &res);

    private:
        ai_chat_sdk::ChatSDK &m_sdk;
        httplib::Server m_server;
        int m_port;
        std::string m_staticDir;
    };
}// namespace chat_server
