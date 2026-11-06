#pragma once
#include "common.h"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <vector>
#include <string>
#include <cstdint>

namespace ai_chat_sdk
{
    class SessionManager
    {
    public:
        // 创建新会话，modelName：模型名称，返回会话ID；失败返回空串
        std::string createSession(const std::string &modelName);
        // 通过会话ID获取会话，返回会话指针；不存在返回 nullptr
        std::shared_ptr<Session> getSession(const std::string &sessionId);
        // 添加消息到会话，成功返回 true；会话不存在返回 false
        bool addMessage(const std::string &sessionId, const Message &message);
        // 获取会话历史消息列表；失败返回空 vector
        std::vector<Message> getSessionHistory(const std::string &sessionId);
        // 获取所有会话ID列表
        std::vector<std::string> getSessionList() const;
        // 删除会话，成功返回 true；会话不存在返回 false
        bool deleteSession(const std::string &sessionId);
        // 更新会话的 updateTime 为当前时间戳
        void updateSessionTimestamp(const std::string &sessionId);
        // 清空所有会话
        void clearAllSessions();
        // 获取当前会话总数
        size_t getSessionCount() const;
    private:
        // 生成唯一会话ID
        std::string generateSessionId();
        // 生成唯一消息ID
        std::string generateMessageId();
    private:
        std::unordered_map<std::string, std::shared_ptr<Session>> m_sessions;
        mutable std::mutex m_mutex;
        // 两个 static 原子计数器，跨 SessionManager 实例也不会撞号
        static std::atomic<int64_t> m_messageCounter;
        static std::atomic<int64_t> m_sessionCounter;
    };
}